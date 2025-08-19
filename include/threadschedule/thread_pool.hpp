#pragma once

#include "concepts.hpp"
#include "scheduler_policy.hpp"
#include "thread_wrapper.hpp"
#include <algorithm>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <vector>

namespace threadschedule
{

/**
 * @brief Modern thread pool with advanced scheduling capabilities
 */
class ThreadPool
{
  public:
    using Task = std::function<void()>;

    struct Statistics
    {
        size_t total_threads;
        size_t active_threads;
        size_t pending_tasks;
        size_t completed_tasks;
    };

    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency())
        : num_threads_(num_threads),
          stop_(false)
    {

        if (num_threads == 0)
        {
            num_threads_ = 1;
        }

        workers_.reserve(num_threads_);

        // Create worker threads
        for (size_t i = 0; i < num_threads_; ++i)
        {
            workers_.emplace_back(&ThreadPool::worker_function, this, i);
        }
    }

    ThreadPool(const ThreadPool &)            = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    ~ThreadPool()
    {
        shutdown();
    }

    /**
     * @brief Submit a task to the thread pool
     */
    template <
        typename F,
        typename... Args>
    auto submit(
        F &&f,
        Args &&...args
    )
        -> std::future<std::invoke_result_t<
            F,
            Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            if (stop_)
            {
                throw std::runtime_error("ThreadPool is shutting down");
            }

            tasks_.emplace([task]() { (*task)(); });
        }

        condition_.notify_one();
        return result;
    }

    /**
     * @brief Submit multiple tasks
     */
    template <typename Iterator>
    std::vector<std::future<void>> submit_range(
        Iterator begin,
        Iterator end
    )
    {
        std::vector<std::future<void>> futures;
        futures.reserve(std::distance(begin, end));

        for (auto it = begin; it != end; ++it)
        {
            futures.push_back(submit(*it));
        }

        return futures;
    }

    /**
     * @brief Apply a function to a range of values in parallel
     */
    template <
        typename Iterator,
        typename F>
    void parallel_for_each(
        Iterator begin,
        Iterator end,
        F      &&func
    )
    {
        std::vector<std::future<void>> futures;
        futures.reserve(std::distance(begin, end));

        for (auto it = begin; it != end; ++it)
        {
            futures.push_back(submit([func, item = *it]() { func(item); }));
        }

        // Wait for all tasks to complete
        for (auto &future : futures)
        {
            future.wait();
        }
    }

    /**
     * @brief Get the number of worker threads
     */
    size_t size() const noexcept
    {
        return num_threads_;
    }

    /**
     * @brief Get the current number of pending tasks
     */
    size_t pending_tasks() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

    /**
     * @brief Configure thread properties
     */
    bool configure_threads(
        const std::string &name_prefix,
        SchedulingPolicy   policy   = SchedulingPolicy::OTHER,
        ThreadPriority     priority = ThreadPriority::normal()
    )
    {

        bool success = true;

        for (size_t i = 0; i < workers_.size(); ++i)
        {
            const std::string thread_name = name_prefix + "_" + std::to_string(i);

            if (!workers_[i].set_name(thread_name))
            {
                success = false;
            }

            if (!workers_[i].set_scheduling_policy(policy, priority))
            {
                success = false;
            }
        }

        return success;
    }

    /**
     * @brief Set CPU affinity for all worker threads
     */
    bool set_affinity(const ThreadAffinity &affinity)
    {
        bool success = true;

        for (auto &worker : workers_)
        {
            if (!worker.set_affinity(affinity))
            {
                success = false;
            }
        }

        return success;
    }

    /**
     * @brief Distribute threads across CPUs
     */
    bool distribute_across_cpus()
    {
        const auto cpu_count = std::thread::hardware_concurrency();
        if (cpu_count == 0)
            return false;

        bool success = true;

        for (size_t i = 0; i < workers_.size(); ++i)
        {
            ThreadAffinity affinity({static_cast<int>(i % cpu_count)});
            if (!workers_[i].set_affinity(affinity))
            {
                success = false;
            }
        }

        return success;
    }

    /**
     * @brief Wait for all pending tasks to complete
     */
    void wait_for_tasks()
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        task_finished_condition_.wait(lock, [this] { return tasks_.empty() && active_tasks_ == 0; });
    }

    /**
     * @brief Gracefully shutdown the thread pool
     */
    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_)
                return;
            stop_ = true;
        }

        condition_.notify_all();

        for (auto &worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }

        workers_.clear();
    }

    /**
     * @brief Get statistics about the thread pool
     */
    Statistics get_statistics() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        Statistics                  stats;
        stats.total_threads   = num_threads_;
        stats.active_threads  = active_tasks_;
        stats.pending_tasks   = tasks_.size();
        stats.completed_tasks = completed_tasks_;
        return stats;
    }

  private:
    size_t                     num_threads_;
    std::vector<ThreadWrapper> workers_;
    std::queue<Task>           tasks_;

    mutable std::mutex      queue_mutex_;
    std::condition_variable condition_;
    std::condition_variable task_finished_condition_;
    std::atomic<bool>       stop_;
    std::atomic<size_t>     active_tasks_{0};
    std::atomic<size_t>     completed_tasks_{0};

    void worker_function(size_t /* worker_id */)
    {
        while (true)
        {
            Task task;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);

                condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

                if (stop_ && tasks_.empty())
                {
                    return;
                }

                task = std::move(tasks_.front());
                tasks_.pop();
                ++active_tasks_;
            }

            try
            {
                task();
            }
            catch (...)
            {
                // Log exception or handle as needed
            }

            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                --active_tasks_;
                ++completed_tasks_;
            }

            task_finished_condition_.notify_all();
        }
    }
};

/**
 * @brief Singleton thread pool for global use
 */
class GlobalThreadPool
{
  public:
    static ThreadPool &instance()
    {
        static ThreadPool pool(std::thread::hardware_concurrency());
        return pool;
    }

    template <
        typename F,
        typename... Args>
    static auto submit(
        F &&f,
        Args &&...args
    )
    {
        return instance().submit(std::forward<F>(f), std::forward<Args>(args)...);
    }

    template <typename Iterator>
    static auto submit_range(
        Iterator begin,
        Iterator end
    )
    {
        return instance().submit_range(begin, end);
    }

    template <
        typename Iterator,
        typename F>
    static void parallel_for_each(
        Iterator begin,
        Iterator end,
        F      &&func
    )
    {
        instance().parallel_for_each(begin, end, std::forward<F>(func));
    }

  private:
    GlobalThreadPool() = default;
};

/**
 * @brief Convenience function for parallel execution with containers
 */
template <
    typename Container,
    typename F>
void parallel_for_each(
    const Container &container,
    F              &&func
)
{
    GlobalThreadPool::parallel_for_each(container.begin(), container.end(), std::forward<F>(func));
}

} // namespace threadschedule
