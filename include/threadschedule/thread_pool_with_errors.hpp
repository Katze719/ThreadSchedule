#pragma once

#include "error_handler.hpp"
#include "thread_pool.hpp"
#include <memory>

namespace threadschedule
{

/**
 * @brief High-performance thread pool with built-in error handling
 *
 * Extends HighPerformancePool with automatic exception catching and error callbacks.
 */
class HighPerformancePoolWithErrors
{
  public:
    explicit HighPerformancePoolWithErrors(size_t num_threads = std::thread::hardware_concurrency())
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

    /**
     * @brief Add a global error callback for all tasks
     */
    auto add_error_callback(ErrorCallback callback) -> size_t
    {
        return error_handler_->add_callback(std::move(callback));
    }

    /**
     * @brief Clear all error callbacks
     */
    void clear_error_callbacks()
    {
        error_handler_->clear_callbacks();
    }

    /**
     * @brief Get total error count
     */
    [[nodiscard]] auto error_count() const -> size_t
    {
        return error_handler_->error_count();
    }

    /**
     * @brief Reset error count
     */
    void reset_error_count()
    {
        error_handler_->reset_error_count();
    }

    /**
     * @brief Get the underlying pool
     */
    [[nodiscard]] auto pool() -> HighPerformancePool&
    {
        return pool_;
    }

    /**
     * @brief Get statistics from underlying pool
     */
    [[nodiscard]] auto get_statistics() const -> HighPerformancePool::Statistics
    {
        return pool_.get_statistics();
    }

    /**
     * @brief Configure threads
     */
    auto configure_threads(std::string const& name_prefix, SchedulingPolicy policy = SchedulingPolicy::OTHER,
                           ThreadPriority priority = ThreadPriority::normal()) -> expected<void, std::error_code>
    {
        return pool_.configure_threads(name_prefix, policy, priority);
    }

    /**
     * @brief Distribute threads across CPUs
     */
    auto distribute_across_cpus() -> expected<void, std::error_code>
    {
        return pool_.distribute_across_cpus();
    }

    /**
     * @brief Shutdown the pool
     */
    void shutdown()
    {
        pool_.shutdown();
    }

    /**
     * @brief Wait for all tasks to complete
     */
    void wait_for_tasks()
    {
        pool_.wait_for_tasks();
    }

    /**
     * @brief Get pool size
     */
    [[nodiscard]] auto size() const noexcept -> size_t
    {
        return pool_.size();
    }

    /**
     * @brief Get pending task count
     */
    [[nodiscard]] auto pending_tasks() const -> size_t
    {
        return pool_.pending_tasks();
    }

  private:
    HighPerformancePool pool_;
    std::shared_ptr<ErrorHandler> error_handler_;
};

/**
 * @brief Fast thread pool with built-in error handling
 */
class FastThreadPoolWithErrors
{
  public:
    explicit FastThreadPoolWithErrors(size_t num_threads = std::thread::hardware_concurrency())
        : pool_(num_threads), error_handler_(std::make_shared<ErrorHandler>())
    {
    }

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

    [[nodiscard]] auto pool() -> FastThreadPool&
    {
        return pool_;
    }

    [[nodiscard]] auto get_statistics() const -> FastThreadPool::Statistics
    {
        return pool_.get_statistics();
    }

    auto configure_threads(std::string const& name_prefix, SchedulingPolicy policy = SchedulingPolicy::OTHER,
                           ThreadPriority priority = ThreadPriority::normal()) -> bool
    {
        return pool_.configure_threads(name_prefix, policy, priority);
    }

    auto distribute_across_cpus() -> bool
    {
        return pool_.distribute_across_cpus();
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
    FastThreadPool pool_;
    std::shared_ptr<ErrorHandler> error_handler_;
};

/**
 * @brief Simple thread pool with built-in error handling
 */
class ThreadPoolWithErrors
{
  public:
    explicit ThreadPoolWithErrors(size_t num_threads = std::thread::hardware_concurrency())
        : pool_(num_threads), error_handler_(std::make_shared<ErrorHandler>())
    {
    }

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

    [[nodiscard]] auto pool() -> ThreadPool&
    {
        return pool_;
    }

    [[nodiscard]] auto get_statistics() const -> ThreadPool::Statistics
    {
        return pool_.get_statistics();
    }

    auto configure_threads(std::string const& name_prefix, SchedulingPolicy policy = SchedulingPolicy::OTHER,
                           ThreadPriority priority = ThreadPriority::normal()) -> bool
    {
        return pool_.configure_threads(name_prefix, policy, priority);
    }

    auto set_affinity(ThreadAffinity const& affinity) -> bool
    {
        return pool_.set_affinity(affinity);
    }

    auto distribute_across_cpus() -> bool
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
    ThreadPool pool_;
    std::shared_ptr<ErrorHandler> error_handler_;
};

} // namespace threadschedule
