#pragma once

/**
 * @file thread_pool_with_errors.hpp
 * @brief PoolWithErrors wrapper that combines any pool with an ErrorHandler.
 */

#include "error_handler.hpp"
#include "thread_pool.hpp"
#include <memory>

namespace threadschedule
{

/**
 * @brief Thread pool wrapper that combines any pool type with an @ref ErrorHandler.
 *
 * Non-copyable; implicitly movable (default move operations).
 * Thread-safe (delegates to the underlying pool).
 *
 * submit() wraps every task so that exceptions are both reported to
 * the @ref ErrorHandler (via registered callbacks) **and** re-thrown, making
 * them accessible through the returned @ref FutureWithErrorHandler.
 * submit_with_description() additionally attaches a user-supplied
 * description string to the error report for easier diagnostics.
 *
 * @see FutureWithErrorHandler, ErrorHandler, TaskError
 *
 * @tparam PoolType The underlying pool type (e.g. ThreadPool,
 *         FastThreadPool, HighPerformancePool).
 */
template <typename PoolType>
class PoolWithErrors
{
  public:
    explicit PoolWithErrors(size_t num_threads = std::thread::hardware_concurrency())
        : pool_(num_threads), error_handler_(std::make_shared<ErrorHandler>())
    {
    }

    /**
     * @brief Construct with forwarded pool arguments.
     *
     * Enables passing pool-specific constructor arguments (e.g.
     * @c deque_capacity for @ref HighPerformancePool) while still
     * attaching the error handler.
     *
     * @code
     * PoolWithErrors<HighPerformancePool> pool(4, 2048, true);
     * @endcode
     */
    template <typename Arg1, typename Arg2, typename... Args>
    explicit PoolWithErrors(Arg1&& arg1, Arg2&& arg2, Args&&... args)
        : pool_(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Args>(args)...),
          error_handler_(std::make_shared<ErrorHandler>())
    {
    }

    /**
     * @brief Submit a task with automatic error handling
     */
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> FutureWithErrorHandler<std::invoke_result_t<F, Args...>>
    {
        return submit_impl({}, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /**
     * @brief Submit a task with a description for better error messages
     */
    template <typename F, typename... Args>
    auto submit_with_description(std::string const& description, F&& f, Args&&... args)
        -> FutureWithErrorHandler<std::invoke_result_t<F, Args...>>
    {
        return submit_impl(description, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /**
     * @brief Submit a task, returning an error instead of throwing on shutdown.
     */
    template <typename F, typename... Args>
    auto try_submit(F&& f, Args&&... args)
        -> expected<FutureWithErrorHandler<std::invoke_result_t<F, Args...>>, std::error_code>
    {
        return try_submit_impl({}, std::forward<F>(f), std::forward<Args>(args)...);
    }

    auto add_error_callback(ErrorCallback callback) -> size_t
    {
        return error_handler_->add_callback(std::move(callback));
    }

    auto remove_error_callback(size_t id) -> bool
    {
        return error_handler_->remove_callback(id);
    }

    void clear_error_callbacks()
    {
        error_handler_->clear_callbacks();
    }

    [[nodiscard]] auto error_count() const -> size_t
    {
        return error_handler_->error_count();
    }

    void reset_error_count()
    {
        error_handler_->reset_error_count();
    }

    [[nodiscard]] auto pool() -> PoolType&
    {
        return pool_;
    }

    [[nodiscard]] auto get_statistics() const -> decltype(auto)
    {
        return pool_.get_statistics();
    }

    auto configure_threads(std::string const& name_prefix, SchedulingPolicy policy = SchedulingPolicy::OTHER,
                           ThreadPriority priority = ThreadPriority::normal()) -> decltype(auto)
    {
        return pool_.configure_threads(name_prefix, policy, priority);
    }

    auto set_affinity(ThreadAffinity const& affinity) -> decltype(auto)
    {
        return pool_.set_affinity(affinity);
    }

    auto distribute_across_cpus() -> decltype(auto)
    {
        return pool_.distribute_across_cpus();
    }

    void wait_for_tasks()
    {
        pool_.wait_for_tasks();
    }

    void shutdown()
    {
        pool_.shutdown();
    }

    [[nodiscard]] auto size() const noexcept -> size_t
    {
        return pool_.size();
    }

    [[nodiscard]] auto pending_tasks() const -> size_t
    {
        return pool_.pending_tasks();
    }

  private:
    template <typename F, typename... Args>
    auto submit_impl(std::string description, F&& f, Args&&... args)
        -> FutureWithErrorHandler<std::invoke_result_t<F, Args...>>
    {
        auto handler = error_handler_;
        auto wrapped_task = [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...), handler,
                             desc = std::move(description)]() {
            try
            {
                return std::apply(f, args);
            }
            catch (...)
            {
                handler->handle_error(TaskError::capture(desc));
                throw;
            }
        };
        auto future = pool_.submit(std::move(wrapped_task));
        return FutureWithErrorHandler<std::invoke_result_t<F, Args...>>(std::move(future));
    }

    template <typename F, typename... Args>
    auto try_submit_impl(std::string description, F&& f, Args&&... args)
        -> expected<FutureWithErrorHandler<std::invoke_result_t<F, Args...>>, std::error_code>
    {
        auto handler = error_handler_;
        auto wrapped_task = [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...), handler,
                             desc = std::move(description)]() {
            try
            {
                return std::apply(f, args);
            }
            catch (...)
            {
                handler->handle_error(TaskError::capture(desc));
                throw;
            }
        };
        auto result = pool_.try_submit(std::move(wrapped_task));
        if (!result.has_value())
            return unexpected(result.error());
        return FutureWithErrorHandler<std::invoke_result_t<F, Args...>>(std::move(result.value()));
    }

    PoolType pool_;
    std::shared_ptr<ErrorHandler> error_handler_;
};

/** @brief @ref HighPerformancePool with integrated error handling. */
using HighPerformancePoolWithErrors = PoolWithErrors<HighPerformancePool>;

/** @brief @c FastThreadPool with integrated error handling. */
using FastThreadPoolWithErrors = PoolWithErrors<FastThreadPool>;

/** @brief @c ThreadPool with integrated error handling. */
using ThreadPoolWithErrors = PoolWithErrors<ThreadPool>;

} // namespace threadschedule
