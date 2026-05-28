#pragma once

/**
 * @file error_handler.hpp
 * @brief Error handling primitives: TaskError, ErrorHandler, and ErrorHandledTask.
 */

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace threadschedule
{

/**
 * @brief Holds diagnostic information captured from a failed task.
 *
 * TaskError is a value type (copyable and movable) that bundles the original
 * exception together with context about where and when the failure occurred.
 *
 * Instances are typically created by ErrorHandledTask and forwarded to
 * registered ErrorCallback functions through an ErrorHandler.
 */
struct TaskError
{
    /** @brief The captured exception. Never null when produced by the library. */
    std::exception_ptr exception;

    /** @brief Optional human-readable label supplied when the task was submitted. */
    std::string task_description;

    /** @brief Id of the thread on which the exception was thrown. */
    std::thread::id thread_id;

    /** @brief Monotonic timestamp recorded immediately after the exception was caught. */
    std::chrono::steady_clock::time_point timestamp;

    /**
     * @brief Capture the current in-flight exception into a TaskError.
     *
     * Must be called inside a @c catch block. Fills exception, thread_id,
     * and timestamp; optionally sets task_description.
     */
    static auto capture(std::string description = {}) -> TaskError
    {
        TaskError err;
        err.exception = std::current_exception();
        err.task_description = std::move(description);
        err.thread_id = std::this_thread::get_id();
        err.timestamp = std::chrono::steady_clock::now();
        return err;
    }

    /**
     * @brief Extract the message string from the stored exception.
     *
     * Internally re-throws the exception and catches it as @c std::exception
     * to call @c what().  This is safe but incurs the overhead of a throw /
     * catch round-trip; avoid calling in tight loops.
     *
     * @return The exception message, @c "Unknown exception" if the stored
     *         exception is not derived from @c std::exception, or
     *         @c "No exception" if the pointer is empty.
     */
    [[nodiscard]] auto what() const -> std::string
    {
        try
        {
            if (exception)
            {
                std::rethrow_exception(exception);
            }
        }
        catch (std::exception const& e)
        {
            return e.what();
        }
        catch (...)
        {
            return "Unknown exception";
        }
        return "No exception";
    }

    /**
     * @brief Re-throw the original exception.
     *
     * If the stored @c exception pointer is non-null the exception is
     * re-thrown via @c std::rethrow_exception.  This will terminate the
     * program if called outside a try / catch block.
     *
     * @throws The original exception stored in @ref exception.
     */
    void rethrow() const
    {
        if (exception)
        {
            std::rethrow_exception(exception);
        }
    }
};

/**
 * @brief Signature for error-handling callbacks registered with ErrorHandler.
 *
 * Callbacks receive a const reference to the TaskError describing the failure.
 */
using ErrorCallback = std::function<void(TaskError const&)>;

/**
 * @brief Central registry and dispatcher for task-error callbacks.
 *
 * ErrorHandler maintains an ordered list of ErrorCallback functions and invokes
 * them whenever a task reports a failure through handle_error().
 *
 * @par Thread safety
 * All public methods are guarded by an internal @c std::mutex, so the handler
 * can be shared across threads (typically via @c std::shared_ptr).
 *
 * @par Callback execution
 * - Callbacks are invoked in the order they were registered (FIFO).
 * - Callbacks run **under the lock** - keep them short and non-blocking to
 *   avoid contention with other threads that may call handle_error() or
 *   add_callback() concurrently.
 * - If a callback itself throws, the exception is silently swallowed so that
 *   remaining callbacks still execute.
 *
 * @par Callback management
 * add_callback() returns a stable ID that can be passed to remove_callback()
 * to unregister a single callback.  clear_callbacks() removes all at once.
 * The error count returned by error_count() is monotonically increasing and
 * is only reset by an explicit call to reset_error_count().
 */
class ErrorHandler
{
  public:
    /**
     * @brief Register an error callback.
     *
     * @param callback Callable to invoke when a task throws.
     * @return Stable ID for the callback, usable with remove_callback().
     */
    auto add_callback(ErrorCallback callback) -> size_t
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t const id = next_callback_id_++;
        callbacks_.emplace(id, std::move(callback));
        return id;
    }

    /**
     * @brief Remove a single callback by its ID.
     *
     * @param id The ID returned by add_callback().
     * @return @c true if the callback was found and removed, @c false otherwise.
     */
    auto remove_callback(size_t id) -> bool
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return callbacks_.erase(id) > 0;
    }

    /**
     * @brief Check whether a callback with the given ID is registered.
     */
    [[nodiscard]] auto has_callback(size_t id) const -> bool
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return callbacks_.count(id) > 0;
    }

    /**
     * @brief Remove all registered error callbacks.
     *
     * After this call, handle_error() will still increment the error count
     * but no callbacks will be invoked.
     */
    void clear_callbacks()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.clear();
    }

    /**
     * @brief Dispatch an error to all registered callbacks.
     *
     * Increments the internal error counter and then invokes every registered
     * callback in insertion order.  If any callback throws, the exception is
     * caught and silently discarded so that subsequent callbacks still run.
     *
     * @param error Diagnostic information about the failed task.
     */
    void handle_error(TaskError const& error)
    {
        std::vector<ErrorCallback> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            error_count_++;
            snapshot.reserve(callbacks_.size());
            for (auto const& [id, callback] : callbacks_)
                snapshot.push_back(callback);
        }

        for (auto const& callback : snapshot)
        {
            try
            {
                callback(error);
            }
            catch (...)
            {
            }
        }
    }

    /**
     * @brief Return the total number of errors handled since the last reset.
     *
     * The count is monotonically increasing and is only set back to zero by
     * an explicit call to reset_error_count().
     *
     * @return Cumulative number of handle_error() invocations.
     */
    [[nodiscard]] auto error_count() const -> size_t
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_count_;
    }

    /**
     * @brief Reset the cumulative error count to zero.
     */
    void reset_error_count()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_count_ = 0;
    }

  private:
    mutable std::mutex mutex_;
    std::map<size_t, ErrorCallback> callbacks_;
    size_t next_callback_id_{0};
    size_t error_count_{0};
};

/**
 * @brief Callable wrapper that catches exceptions and routes them to an @ref ErrorHandler.
 *
 * ErrorHandledTask wraps an arbitrary callable @p Func and invokes it inside a
 * try / catch block.  Any exception thrown by the callable is captured into a
 * @ref TaskError and forwarded to the associated @ref ErrorHandler; the exception is
 * **not** re-thrown, so from the caller's perspective the task completes
 * normally (silently succeeds).
 *
 * @tparam Func Callable type.  Must be invocable with @c operator()() (no
 *         arguments, return value is discarded).
 *
 * @par Ownership
 * The ErrorHandler is held via @c std::shared_ptr, making it safe to copy or
 * move ErrorHandledTask across thread boundaries without lifetime issues.
 *
 * @see make_error_handled_task
 */
template <typename Func>
class ErrorHandledTask
{
  public:
    ErrorHandledTask(Func&& func, std::shared_ptr<ErrorHandler> handler, std::string description = "")
        : func_(std::forward<Func>(func)), handler_(std::move(handler)), description_(std::move(description))
    {
    }

    void operator()()
    {
        try
        {
            func_();
        }
        catch (...)
        {
            if (handler_)
                handler_->handle_error(TaskError::capture(description_));
        }
    }

  private:
    Func func_;
    std::shared_ptr<ErrorHandler> handler_;
    std::string description_;
};

/**
 * @brief Factory function that creates an @ref ErrorHandledTask with perfect forwarding.
 *
 * @tparam Func Callable type (deduced).
 * @param func        The callable to wrap.
 * @param handler     Shared pointer to the ErrorHandler that will receive errors.
 * @param description Optional human-readable label stored in TaskError::task_description.
 * @return An ErrorHandledTask<Func> ready to be submitted to a thread pool.
 */
template <typename Func>
auto make_error_handled_task(Func&& func, std::shared_ptr<ErrorHandler> handler, std::string description = "")
{
    return ErrorHandledTask<Func>(std::forward<Func>(func), std::move(handler), std::move(description));
}

/**
 * @brief A move-only future wrapper that supports an error callback.
 *
 * FutureWithErrorHandler<T> wraps a @c std::future<T> and adds an optional
 * error callback that fires when get() encounters an exception.
 *
 * @tparam T The value type of the underlying future.
 *
 * @par Move semantics
 * Like @c std::future, this type is move-only (copy construction and copy
 * assignment are deleted).
 *
 * @par Error callback behaviour
 * - Attach a callback with on_error().  At most one callback is supported;
 *   a subsequent call to on_error() replaces the previous callback.
 * - The callback is invoked **before** the exception is re-thrown from get().
 * - wait(), wait_for(), and wait_until() do **not** trigger the error callback.
 * - valid() delegates directly to the underlying @c std::future::valid().
 */
template <typename T>
class FutureWithErrorHandler
{
  public:
    explicit FutureWithErrorHandler(std::future<T> future)
        : future_(std::move(future)), error_callback_(nullptr), has_callback_(false)
    {
    }

    FutureWithErrorHandler(FutureWithErrorHandler const&) = delete;
    auto operator=(FutureWithErrorHandler const&) -> FutureWithErrorHandler& = delete;
    FutureWithErrorHandler(FutureWithErrorHandler&&) = default;
    auto operator=(FutureWithErrorHandler&&) -> FutureWithErrorHandler& = default;

    /**
     * @brief Attach an error callback.
     *
     * The callback will be invoked with the current @c std::exception_ptr if
     * get() encounters an exception.  Only one callback is stored; calling
     * on_error() again replaces the previous callback.
     *
     * @param callback Callable invoked with the exception pointer on failure.
     * @return Reference to @c *this, allowing fluent chaining.
     */
    auto on_error(std::function<void(std::exception_ptr)> callback) -> FutureWithErrorHandler&
    {
        error_callback_ = std::move(callback);
        has_callback_ = true;
        return *this;
    }

    /**
     * @brief Retrieve the result, invoking the error callback on failure.
     *
     * If the underlying future holds an exception, the error callback (if any)
     * is called **before** the exception is re-thrown to the caller.
     *
     * @return The stored value of type @p T (void when @p T is @c void).
     * @throws Any exception stored in the underlying @c std::future.
     */
    auto get() -> T
    {
        try
        {
            if constexpr (std::is_void_v<T>)
                future_.get();
            else
                return future_.get();
        }
        catch (...)
        {
            if (has_callback_ && error_callback_)
            {
                error_callback_(std::current_exception());
            }
            throw;
        }
    }

    /**
     * @brief Block until the result is ready.
     *
     * Does **not** trigger the error callback regardless of the stored state.
     */
    void wait() const
    {
        future_.wait();
    }

    /**
     * @brief Block until the result is ready or the timeout elapses.
     *
     * Does **not** trigger the error callback.
     *
     * @return The @c std::future_status indicating whether the result is ready.
     */
    template <typename Rep, typename Period>
    auto wait_for(std::chrono::duration<Rep, Period> const& timeout_duration) const
    {
        return future_.wait_for(timeout_duration);
    }

    /**
     * @brief Block until the result is ready or the given time point is reached.
     *
     * Does **not** trigger the error callback.
     *
     * @return The @c std::future_status indicating whether the result is ready.
     */
    template <typename Clock, typename Duration>
    auto wait_until(std::chrono::time_point<Clock, Duration> const& timeout_time) const
    {
        return future_.wait_until(timeout_time);
    }

    /**
     * @brief Check whether the future refers to a shared state.
     *
     * Delegates directly to @c std::future::valid().
     */
    [[nodiscard]] auto valid() const -> bool
    {
        return future_.valid();
    }

  private:
    std::future<T> future_;
    std::function<void(std::exception_ptr)> error_callback_;
    bool has_callback_{false};
};

} // namespace threadschedule
