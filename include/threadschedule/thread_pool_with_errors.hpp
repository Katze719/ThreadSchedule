#pragma once

#include "error_handler.hpp"
#include "thread_pool.hpp"
#include <memory>

namespace threadschedule
{

/**
 * @brief Thread pool wrapper that combines any pool type with an @ref ErrorHandler.
 *
 * Non-copyable, non-movable. Thread-safe (delegates to the underlying pool).
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
     * @brief Submit a task with automatic error handling
     */
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> FutureWithErrorHandler<std::invoke_result_t<F, Args...>>
    {
        auto handler = error_handler_;
        auto wrapped_task = [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...), handler]() {
            try
            {
                return std::apply(f, args);
            }
            catch (...)
            {
                TaskError error;
                error.exception = std::current_exception();
                error.thread_id = std::this_thread::get_id();
                error.timestamp = std::chrono::steady_clock::now();
                handler->handle_error(error);
                throw;
            }
        };

        auto future = pool_.submit(std::move(wrapped_task));
        return FutureWithErrorHandler<std::invoke_result_t<F, Args...>>(std::move(future));
    }

    /**
     * @brief Submit a task with a description for better error messages
     */
    template <typename F, typename... Args>
    auto submit_with_description(std::string const& description, F&& f, Args&&... args)
        -> FutureWithErrorHandler<std::invoke_result_t<F, Args...>>
    {
        auto handler = error_handler_;
        auto wrapped_task = [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...), handler,
                             description]() {
            try
            {
                return std::apply(f, args);
            }
            catch (...)
            {
                TaskError error;
                error.exception = std::current_exception();
                error.task_description = description;
                error.thread_id = std::this_thread::get_id();
                error.timestamp = std::chrono::steady_clock::now();
                handler->handle_error(error);
                throw;
            }
        };

        auto future = pool_.submit(std::move(wrapped_task));
        return FutureWithErrorHandler<std::invoke_result_t<F, Args...>>(std::move(future));
    }

    auto add_error_callback(ErrorCallback callback) -> size_t
    {
        return error_handler_->add_callback(std::move(callback));
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
    PoolType pool_;
    std::shared_ptr<ErrorHandler> error_handler_;
};

/** @brief @ref HighPerformancePool with integrated error handling. */
using HighPerformancePoolWithErrors = PoolWithErrors<HighPerformancePool>;

/** @brief @ref FastThreadPool with integrated error handling. */
using FastThreadPoolWithErrors = PoolWithErrors<FastThreadPool>;

/** @brief @ref ThreadPool with integrated error handling. */
using ThreadPoolWithErrors = PoolWithErrors<ThreadPool>;

} // namespace threadschedule
