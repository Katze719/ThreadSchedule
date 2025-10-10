#pragma once

#include "expected.hpp"
#include "thread_pool.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace threadschedule
{

/**
 * @brief Handle for scheduled tasks that can be used to cancel them
 */
class ScheduledTaskHandle
{
  public:
    explicit ScheduledTaskHandle(uint64_t id) : id_(id), cancelled_(std::make_shared<std::atomic<bool>>(false))
    {
    }

    void cancel()
    {
        cancelled_->store(true, std::memory_order_release);
    }

    [[nodiscard]] auto is_cancelled() const -> bool
    {
        return cancelled_->load(std::memory_order_acquire);
    }

    [[nodiscard]] auto id() const -> uint64_t
    {
        return id_;
    }

  private:
    uint64_t id_;
    std::shared_ptr<std::atomic<bool>> cancelled_;

    template <typename>
    friend class ScheduledThreadPoolT;
    [[nodiscard]] auto get_cancel_flag() const -> std::shared_ptr<std::atomic<bool>>
    {
        return cancelled_;
    }
};

/**
 * @brief Thread pool with support for scheduled and periodic tasks
 *
 * Features:
 * - Schedule tasks to run at specific time points
 * - Schedule tasks to run after a delay
 * - Schedule periodic tasks with fixed intervals
 * - Cancel scheduled tasks before they execute
 * - Integrates with any thread pool type (ThreadPool by default)
 *
 * @tparam PoolType Type of thread pool to use for task execution (default: ThreadPool)
 */
template <typename PoolType = ThreadPool>
class ScheduledThreadPoolT
{
  public:
    using Task = std::function<void()>;
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::steady_clock::duration;

    struct ScheduledTaskInfo
    {
        uint64_t id;
        TimePoint next_run;
        Duration interval; // Zero for one-time tasks
        Task task;
        std::shared_ptr<std::atomic<bool>> cancelled;
        bool periodic;
    };

    /**
     * @brief Create a scheduled thread pool
     * @param worker_threads Number of worker threads for executing tasks (default: hardware concurrency)
     */
    explicit ScheduledThreadPoolT(size_t worker_threads = std::thread::hardware_concurrency())
        : pool_(worker_threads), stop_(false), next_task_id_(1)
    {
        scheduler_thread_ = std::thread(&ScheduledThreadPoolT::scheduler_loop, this);
    }

    ScheduledThreadPoolT(ScheduledThreadPoolT const&) = delete;
    auto operator=(ScheduledThreadPoolT const&) -> ScheduledThreadPoolT& = delete;

    ~ScheduledThreadPoolT()
    {
        shutdown();
    }

    /**
     * @brief Schedule a task to run after a delay
     * @param delay Duration to wait before executing the task
     * @param task Function to execute
     * @return Handle to cancel the task
     */
    auto schedule_after(Duration delay, Task task) -> ScheduledTaskHandle
    {
        auto run_time = std::chrono::steady_clock::now() + delay;
        return schedule_at(run_time, std::move(task));
    }

    /**
     * @brief Schedule a task to run at a specific time point
     * @param time_point When to execute the task
     * @param task Function to execute
     * @return Handle to cancel the task
     */
    auto schedule_at(TimePoint time_point, Task task) -> ScheduledTaskHandle
    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t const task_id = next_task_id_++;
        ScheduledTaskHandle handle(task_id);

        ScheduledTaskInfo info;
        info.id = task_id;
        info.next_run = time_point;
        info.interval = Duration::zero();
        info.task = std::move(task);
        info.cancelled = handle.get_cancel_flag();
        info.periodic = false;

        scheduled_tasks_.insert({time_point, std::move(info)});
        condition_.notify_one();

        return handle;
    }

    /**
     * @brief Schedule a task to run periodically at fixed intervals
     * @param interval Duration between executions
     * @param task Function to execute repeatedly
     * @return Handle to cancel the periodic task
     *
     * The task runs immediately and then repeats every interval.
     * Use schedule_periodic_after() if you want to delay the first execution.
     */
    auto schedule_periodic(Duration interval, Task task) -> ScheduledTaskHandle
    {
        return schedule_periodic_after(Duration::zero(), interval, std::move(task));
    }

    /**
     * @brief Schedule a task to run periodically after an initial delay
     * @param initial_delay Duration to wait before first execution
     * @param interval Duration between subsequent executions
     * @param task Function to execute repeatedly
     * @return Handle to cancel the periodic task
     */
    auto schedule_periodic_after(Duration initial_delay, Duration interval, Task task) -> ScheduledTaskHandle
    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t const task_id = next_task_id_++;
        ScheduledTaskHandle handle(task_id);

        ScheduledTaskInfo info;
        info.id = task_id;
        info.next_run = std::chrono::steady_clock::now() + initial_delay;
        info.interval = interval;
        info.task = std::move(task);
        info.cancelled = handle.get_cancel_flag();
        info.periodic = true;

        scheduled_tasks_.insert({info.next_run, std::move(info)});
        condition_.notify_one();

        return handle;
    }

    /**
     * @brief Cancel a scheduled task by handle
     * @param handle Handle returned from schedule_* functions
     *
     * Note: Can also call handle.cancel() directly
     */
    static void cancel(ScheduledTaskHandle& handle)
    {
        handle.cancel();
    }

    /**
     * @brief Get number of scheduled tasks (including periodic)
     */
    [[nodiscard]] auto scheduled_count() const -> size_t
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return scheduled_tasks_.size();
    }

    /**
     * @brief Get the underlying thread pool for direct task submission
     */
    [[nodiscard]] auto thread_pool() -> PoolType&
    {
        return pool_;
    }

    /**
     * @brief Shutdown the scheduler and wait for completion
     */
    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_)
                return;
            stop_ = true;
        }

        condition_.notify_one();

        if (scheduler_thread_.joinable())
        {
            scheduler_thread_.join();
        }

        pool_.shutdown();
    }

    /**
     * @brief Configure worker threads
     *
     * Note: Return type depends on the underlying pool type.
     * ThreadPool returns bool, HighPerformancePool returns expected<void, std::error_code>.
     * For consistent behavior, access the pool directly via thread_pool().
     */
    auto configure_threads(std::string const& name_prefix, SchedulingPolicy policy = SchedulingPolicy::OTHER,
                           ThreadPriority priority = ThreadPriority::normal())
    {
        return pool_.configure_threads(name_prefix, policy, priority);
    }

  private:
    PoolType pool_;
    std::thread scheduler_thread_;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;

    std::multimap<TimePoint, ScheduledTaskInfo> scheduled_tasks_;
    std::atomic<uint64_t> next_task_id_;

    void scheduler_loop()
    {
        while (true)
        {
            std::unique_lock<std::mutex> lock(mutex_);

            // Wait until we have tasks or need to stop
            if (scheduled_tasks_.empty())
            {
                condition_.wait(lock, [this] { return stop_ || !scheduled_tasks_.empty(); });

                if (stop_)
                    return;
            }

            // Get the next task to execute
            auto const now = std::chrono::steady_clock::now();
            auto it = scheduled_tasks_.begin();

            if (it == scheduled_tasks_.end())
            {
                continue;
            }

            // Wait until it's time to execute
            if (it->first > now)
            {
                condition_.wait_until(lock, it->first, [this] { return stop_.load(); });

                if (stop_)
                    return;

                continue;
            }

            // Extract the task info
            ScheduledTaskInfo info = std::move(it->second);
            scheduled_tasks_.erase(it);

            // Check if cancelled
            if (info.cancelled->load(std::memory_order_acquire))
            {
                continue;
            }

            // Schedule for execution in the thread pool
            try
            {
                // Capture task and periodic info
                auto task_copy = info.task;
                auto cancelled_flag = info.cancelled;

                pool_.submit([task_copy, cancelled_flag]() {
                    if (!cancelled_flag->load(std::memory_order_acquire))
                    {
                        task_copy();
                    }
                });

                // Reschedule if periodic
                if (info.periodic && !info.cancelled->load(std::memory_order_acquire))
                {
                    info.next_run += info.interval;
                    scheduled_tasks_.insert({info.next_run, std::move(info)});
                }
            }
            catch (...)
            {
                // Thread pool might be shutting down
            }
        }
    }
};

// Convenience aliases
using ScheduledThreadPool = ScheduledThreadPoolT<ThreadPool>;
using ScheduledHighPerformancePool = ScheduledThreadPoolT<HighPerformancePool>;
using ScheduledFastThreadPool = ScheduledThreadPoolT<FastThreadPool>;

} // namespace threadschedule
