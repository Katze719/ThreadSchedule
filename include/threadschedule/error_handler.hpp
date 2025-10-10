#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace threadschedule
{

/**
 * @brief Information about a task exception
 */
struct TaskError
{
    std::exception_ptr exception;
    std::string task_description;
    std::thread::id thread_id;
    std::chrono::steady_clock::time_point timestamp;

    /**
     * @brief Get the exception message if it's a std::exception
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
     * @brief Rethrow the exception
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
 * @brief Error handler callback type
 */
using ErrorCallback = std::function<void(TaskError const&)>;

/**
 * @brief Global error handler for thread pool tasks
 *
 * Allows registering callbacks that will be invoked when tasks throw exceptions.
 * Multiple handlers can be registered and they will be called in order.
 */
class ErrorHandler
{
  public:
    /**
     * @brief Add an error callback
     * @param callback Function to call when a task throws
     * @return Handle (index) that can be used to remove the callback
     */
    auto add_callback(ErrorCallback callback) -> size_t
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push_back(std::move(callback));
        return callbacks_.size() - 1;
    }

    /**
     * @brief Remove all error callbacks
     */
    void clear_callbacks()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.clear();
    }

    /**
     * @brief Handle an exception from a task
     * @param error Error information
     */
    void handle_error(TaskError const& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_count_++;

        for (auto const& callback : callbacks_)
        {
            try
            {
                callback(error);
            }
            catch (...)
            {
                // Error handlers should not throw, but we catch just in case
            }
        }
    }

    /**
     * @brief Get total number of errors handled
     */
    [[nodiscard]] auto error_count() const -> size_t
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_count_;
    }

    /**
     * @brief Reset error count
     */
    void reset_error_count()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_count_ = 0;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<ErrorCallback> callbacks_;
    size_t error_count_{0};
};

/**
 * @brief Task wrapper that provides error handling
 *
 * Wraps a task function and handles exceptions according to the provided error handler.
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
            {
                TaskError error;
                error.exception = std::current_exception();
                error.task_description = description_;
                error.thread_id = std::this_thread::get_id();
                error.timestamp = std::chrono::steady_clock::now();

                handler_->handle_error(error);
            }
        }
    }

  private:
    Func func_;
    std::shared_ptr<ErrorHandler> handler_;
    std::string description_;
};

/**
 * @brief Helper function to create an error-handled task
 */
template <typename Func>
auto make_error_handled_task(Func&& func, std::shared_ptr<ErrorHandler> handler, std::string description = "")
{
    return ErrorHandledTask<Func>(std::forward<Func>(func), std::move(handler), std::move(description));
}

/**
 * @brief Future wrapper that provides error callback support
 *
 * Extends std::future with the ability to attach error callbacks.
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
     * @brief Attach an error callback
     * @param callback Function to call if the future throws
     * @return Reference to this for chaining
     */
    auto on_error(std::function<void(std::exception_ptr)> callback) -> FutureWithErrorHandler&
    {
        error_callback_ = std::move(callback);
        has_callback_ = true;
        return *this;
    }

    /**
     * @brief Get the result, calling error callback if exception is thrown
     */
    auto get() -> T
    {
        try
        {
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
     * @brief Wait for the future to complete
     */
    void wait() const
    {
        future_.wait();
    }

    /**
     * @brief Wait for the future with timeout
     */
    template <typename Rep, typename Period>
    auto wait_for(std::chrono::duration<Rep, Period> const& timeout_duration) const
    {
        return future_.wait_for(timeout_duration);
    }

    /**
     * @brief Wait until a specific time point
     */
    template <typename Clock, typename Duration>
    auto wait_until(std::chrono::time_point<Clock, Duration> const& timeout_time) const
    {
        return future_.wait_until(timeout_time);
    }

    /**
     * @brief Check if the future is valid
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

/**
 * @brief Specialization for void futures
 */
template <>
class FutureWithErrorHandler<void>
{
  public:
    explicit FutureWithErrorHandler(std::future<void> future) : future_(std::move(future)), error_callback_(nullptr)
    {
    }

    FutureWithErrorHandler(FutureWithErrorHandler const&) = delete;
    auto operator=(FutureWithErrorHandler const&) -> FutureWithErrorHandler& = delete;
    FutureWithErrorHandler(FutureWithErrorHandler&&) = default;
    auto operator=(FutureWithErrorHandler&&) -> FutureWithErrorHandler& = default;

    auto on_error(std::function<void(std::exception_ptr)> callback) -> FutureWithErrorHandler&
    {
        error_callback_ = std::move(callback);
        has_callback_ = true;
        return *this;
    }

    void get()
    {
        try
        {
            future_.get();
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

    void wait() const
    {
        future_.wait();
    }

    template <typename Rep, typename Period>
    auto wait_for(std::chrono::duration<Rep, Period> const& timeout_duration) const
    {
        return future_.wait_for(timeout_duration);
    }

    template <typename Clock, typename Duration>
    auto wait_until(std::chrono::time_point<Clock, Duration> const& timeout_time) const
    {
        return future_.wait_until(timeout_time);
    }

    [[nodiscard]] auto valid() const -> bool
    {
        return future_.valid();
    }

  private:
    std::future<void> future_;
    std::function<void(std::exception_ptr)> error_callback_;
    bool has_callback_{};
};

} // namespace threadschedule
