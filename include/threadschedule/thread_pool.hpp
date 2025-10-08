#pragma once

#include "concepts.hpp"
#include "expected.hpp"
#include "scheduler_policy.hpp"
#include "thread_wrapper.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <random>
#include <vector>

namespace threadschedule
{

/**
 * @brief High-performance work-stealing deque for individual worker threads
 */
template <typename T>
class WorkStealingDeque
{
  public:
    static constexpr size_t CACHE_LINE_SIZE = 64;
    static constexpr size_t DEFAULT_CAPACITY = 1024;

  private:
    struct alignas(CACHE_LINE_SIZE) AlignedItem
    {
        T item;
        AlignedItem() = default;
        AlignedItem(T &&t) : item(std::move(t))
        {
        }
        AlignedItem(const T &t) : item(t)
        {
        }
    };

    std::unique_ptr<AlignedItem[]> buffer_;
    size_t capacity_;

    alignas(CACHE_LINE_SIZE) std::atomic<size_t> top_{0};    // Owner pushes/pops here
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> bottom_{0}; // Thieves steal here
    alignas(CACHE_LINE_SIZE) mutable std::mutex mutex_;      // For synchronization

  public:
    explicit WorkStealingDeque(size_t capacity = DEFAULT_CAPACITY)
        : buffer_(std::make_unique<AlignedItem[]>(capacity)), capacity_(capacity)
    {
    }

    // Thread-safe operations
    bool push(T &&item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t t = top_.load(std::memory_order_relaxed);
        const size_t b = bottom_.load(std::memory_order_relaxed);

        if (t - b >= capacity_)
        {
            return false; // Queue full
        }

        buffer_[t % capacity_] = AlignedItem(std::move(item));
        top_.store(t + 1, std::memory_order_release);
        return true;
    }

    bool push(const T &item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t t = top_.load(std::memory_order_relaxed);
        const size_t b = bottom_.load(std::memory_order_relaxed);

        if (t - b >= capacity_)
        {
            return false; // Queue full
        }

        buffer_[t % capacity_] = AlignedItem(item);
        top_.store(t + 1, std::memory_order_release);
        return true;
    }

    bool pop(T &item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t t = top_.load(std::memory_order_relaxed);
        const size_t b = bottom_.load(std::memory_order_relaxed);

        if (t <= b)
        {
            return false; // Empty
        }

        const size_t new_top = t - 1;
        item = std::move(buffer_[new_top % capacity_].item);
        top_.store(new_top, std::memory_order_relaxed);
        return true;
    }

    // Thief operations (other threads stealing work)
    bool steal(T &item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t b = bottom_.load(std::memory_order_relaxed);
        const size_t t = top_.load(std::memory_order_relaxed);

        if (b >= t)
        {
            return false; // Empty
        }

        item = std::move(buffer_[b % capacity_].item);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return true;
    }

    size_t size() const
    {
        const size_t t = top_.load(std::memory_order_relaxed);
        const size_t b = bottom_.load(std::memory_order_relaxed);
        return t > b ? t - b : 0;
    }

    bool empty() const
    {
        return size() == 0;
    }
};

/**
 * @brief High-performance thread pool optimized for high-frequency task submission
 *
 * Optimizations for 10k+ tasks/second:
 * - Work-stealing architecture with proper synchronization
 * - Per-thread queues with efficient load balancing
 * - Batch processing support
 * - Optimized wake-up mechanisms
 * - Cache-friendly data structures
 * - Performance monitoring and statistics
 */
class HighPerformancePool
{
  public:
    using Task = std::function<void()>;

    struct Statistics
    {
        size_t total_threads;
        size_t active_threads;
        size_t pending_tasks;
        size_t completed_tasks;
        size_t stolen_tasks;
        double tasks_per_second;
        std::chrono::microseconds avg_task_time;
    };

    explicit HighPerformancePool(size_t num_threads = std::thread::hardware_concurrency())
        : num_threads_(num_threads == 0 ? 1 : num_threads), stop_(false), next_victim_(0),
          start_time_(std::chrono::steady_clock::now())
    {
        // Initialize per-thread work queues
        worker_queues_.resize(num_threads_);
        for (size_t i = 0; i < num_threads_; ++i)
        {
            worker_queues_[i] = std::make_unique<WorkStealingDeque<Task>>();
        }

        workers_.reserve(num_threads_);

        // Create worker threads with thread-local storage
        for (size_t i = 0; i < num_threads_; ++i)
        {
            workers_.emplace_back(&HighPerformancePool::worker_function, this, i);
        }
    }

    HighPerformancePool(const HighPerformancePool &) = delete;
    HighPerformancePool &operator=(const HighPerformancePool &) = delete;

    ~HighPerformancePool()
    {
        shutdown();
    }

    /**
     * @brief High-performance task submission (optimized hot path)
     */
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> result = task->get_future();

        if (stop_.load(std::memory_order_acquire))
        {
            throw std::runtime_error("ThreadPool is shutting down");
        }

        // Try to submit to least loaded queue (round-robin with fallback)
        const size_t preferred_queue = next_victim_.fetch_add(1, std::memory_order_relaxed) % num_threads_;

        // First try the preferred queue
        if (worker_queues_[preferred_queue]->push([task]() { (*task)(); }))
        {
            wakeup_condition_.notify_one();
            return result;
        }

        // If preferred queue is full, try a few random ones
        for (size_t attempts = 0; attempts < (std::min)(num_threads_, size_t(3)); ++attempts)
        {
            const size_t idx = (preferred_queue + attempts + 1) % num_threads_;
            if (worker_queues_[idx]->push([task]() { (*task)(); }))
            {
                wakeup_condition_.notify_one();
                return result;
            }
        }

        // All local queues full, use overflow queue
        {
            std::lock_guard<std::mutex> lock(overflow_mutex_);
            if (stop_.load(std::memory_order_relaxed))
            {
                throw std::runtime_error("ThreadPool is shutting down");
            }
            overflow_tasks_.emplace([task]() { (*task)(); });
        }

        wakeup_condition_.notify_all();
        return result;
    }

    /**
     * @brief Batch task submission for maximum throughput
     */
    template <typename Iterator>
    std::vector<std::future<void>> submit_batch(Iterator begin, Iterator end)
    {
        std::vector<std::future<void>> futures;
        const size_t batch_size = std::distance(begin, end);
        futures.reserve(batch_size);

        if (stop_.load(std::memory_order_acquire))
        {
            throw std::runtime_error("ThreadPool is shutting down");
        }

        // Distribute batch across worker queues
        size_t queue_idx = next_victim_.fetch_add(batch_size, std::memory_order_relaxed) % num_threads_;

        for (auto it = begin; it != end; ++it)
        {
            auto task = std::make_shared<std::packaged_task<void()>>(*it);
            futures.push_back(task->get_future());

            // Try to place in worker queue, round-robin style
            bool queued = false;
            for (size_t attempts = 0; attempts < num_threads_; ++attempts)
            {
                if (worker_queues_[queue_idx]->push([task]() { (*task)(); }))
                {
                    queued = true;
                    break;
                }
                queue_idx = (queue_idx + 1) % num_threads_;
            }

            if (!queued)
            {
                // Overflow to global queue
                std::lock_guard<std::mutex> lock(overflow_mutex_);
                overflow_tasks_.emplace([task]() { (*task)(); });
            }
        }

        // Wake up workers for the batch
        wakeup_condition_.notify_all();
        return futures;
    }

    /**
     * @brief Optimized parallel for_each with work distribution
     */
    template <typename Iterator, typename F>
    void parallel_for_each(Iterator begin, Iterator end, F &&func)
    {
        const size_t total_items = std::distance(begin, end);
        if (total_items == 0)
            return;

        // Calculate optimal chunk size for cache efficiency
        const size_t chunk_size = (std::max)(size_t(1), total_items / (num_threads_ * 4));
        std::vector<std::future<void>> futures;

        for (auto it = begin; it < end;)
        {
            auto chunk_end = (std::min)(it + chunk_size, end);

            futures.push_back(submit([func, it, chunk_end]() {
                for (auto chunk_it = it; chunk_it != chunk_end; ++chunk_it)
                {
                    func(*chunk_it);
                }
            }));

            it = chunk_end;
        }

        // Wait for all chunks to complete
        for (auto &future : futures)
        {
            future.wait();
        }
    }

    size_t size() const noexcept
    {
        return num_threads_;
    }

    size_t pending_tasks() const
    {
        size_t total = 0;
        for (const auto &queue : worker_queues_)
        {
            total += queue->size();
        }

        std::lock_guard<std::mutex> lock(overflow_mutex_);
        total += overflow_tasks_.size();
        return total;
    }

    /**
     * @brief Configure all worker threads
     */
    expected<void, std::error_code> configure_threads(const std::string &name_prefix,
                                                      SchedulingPolicy policy = SchedulingPolicy::OTHER,
                                                      ThreadPriority priority = ThreadPriority::normal())
    {
        bool success = true;

        for (size_t i = 0; i < workers_.size(); ++i)
        {
            const std::string thread_name = name_prefix + "_" + std::to_string(i);

            if (!workers_[i].set_name(thread_name).has_value())
            {
                success = false;
            }

            if (!workers_[i].set_scheduling_policy(policy, priority).has_value())
            {
                success = false;
            }
        }
        if (success)
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
    }

    expected<void, std::error_code> set_affinity(const ThreadAffinity &affinity)
    {
        bool success = true;

        for (auto &worker : workers_)
        {
            if (!worker.set_affinity(affinity).has_value())
            {
                success = false;
            }
        }
        if (success)
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
    }

    expected<void, std::error_code> distribute_across_cpus()
    {
        const auto cpu_count = std::thread::hardware_concurrency();
        if (cpu_count == 0)
            return unexpected(std::make_error_code(std::errc::invalid_argument));

        bool success = true;

        for (size_t i = 0; i < workers_.size(); ++i)
        {
            ThreadAffinity affinity({static_cast<int>(i % cpu_count)});
            if (!workers_[i].set_affinity(affinity).has_value())
            {
                success = false;
            }
        }
        if (success)
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
    }

    void wait_for_tasks()
    {
        std::unique_lock<std::mutex> lock(completion_mutex_);
        completion_condition_.wait(
            lock, [this] { return pending_tasks() == 0 && active_tasks_.load(std::memory_order_acquire) == 0; });
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(overflow_mutex_);
            if (stop_.exchange(true, std::memory_order_acq_rel))
            {
                return; // Already shutting down
            }
        }

        wakeup_condition_.notify_all();

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
     * @brief Get detailed performance statistics
     */
    Statistics get_statistics() const
    {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

        Statistics stats;
        stats.total_threads = num_threads_;
        stats.active_threads = active_tasks_.load(std::memory_order_acquire);
        stats.pending_tasks = pending_tasks();
        stats.completed_tasks = completed_tasks_.load(std::memory_order_acquire);
        stats.stolen_tasks = stolen_tasks_.load(std::memory_order_acquire);

        if (elapsed.count() > 0)
        {
            stats.tasks_per_second = static_cast<double>(stats.completed_tasks) / elapsed.count();
        }
        else
        {
            stats.tasks_per_second = 0.0;
        }

        const auto total_task_time = total_task_time_.load(std::memory_order_acquire);
        if (stats.completed_tasks > 0)
        {
            stats.avg_task_time = std::chrono::microseconds(total_task_time / stats.completed_tasks);
        }
        else
        {
            stats.avg_task_time = std::chrono::microseconds(0);
        }

        return stats;
    }

  private:
    size_t num_threads_;
    std::vector<ThreadWrapper> workers_;
    std::vector<std::unique_ptr<WorkStealingDeque<Task>>> worker_queues_;

    // Overflow queue for when worker queues are full
    std::queue<Task> overflow_tasks_;
    mutable std::mutex overflow_mutex_;

    // Synchronization
    std::atomic<bool> stop_;
    std::condition_variable wakeup_condition_;
    std::mutex wakeup_mutex_;

    std::condition_variable completion_condition_;
    std::mutex completion_mutex_;

    // Load balancing and statistics
    std::atomic<size_t> next_victim_;
    std::atomic<size_t> active_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
    std::atomic<size_t> stolen_tasks_{0};
    std::atomic<uint64_t> total_task_time_{0}; // microseconds

    std::chrono::steady_clock::time_point start_time_;

    void worker_function(size_t worker_id)
    {
        // Thread-local random number generator for work stealing
        thread_local std::random_device rd;
        thread_local std::mt19937 gen = []() {
            std::random_device device;
            return std::mt19937(device());
        }();

        Task task;
        std::uniform_int_distribution<size_t> dist(0, num_threads_ - 1);

        while (true)
        {
            bool found_task = false;

            // 1. Try to get task from own queue (fast path)
            if (worker_queues_[worker_id]->pop(task))
            {
                found_task = true;
            }
            // 2. Try to steal from other workers (limit attempts to reduce contention)
            else
            {
                const size_t max_steal_attempts = (std::min)(num_threads_, size_t(4));
                for (size_t attempts = 0; attempts < max_steal_attempts; ++attempts)
                {
                    const size_t victim_id = dist(gen);
                    if (victim_id != worker_id && worker_queues_[victim_id]->steal(task))
                    {
                        found_task = true;
                        stolen_tasks_.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                }
            }

            // 3. Try overflow queue
            if (!found_task)
            {
                std::lock_guard<std::mutex> lock(overflow_mutex_);
                if (!overflow_tasks_.empty())
                {
                    task = std::move(overflow_tasks_.front());
                    overflow_tasks_.pop();
                    found_task = true;
                }
            }

            if (found_task)
            {
                // Execute task with timing
                active_tasks_.fetch_add(1, std::memory_order_relaxed);

                const auto start_time = std::chrono::steady_clock::now();
                try
                {
                    task();
                }
                catch (...)
                {
                    // Log exception or handle as needed
                }
                const auto end_time = std::chrono::steady_clock::now();

                const auto task_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                total_task_time_.fetch_add(task_duration.count(), std::memory_order_relaxed);

                active_tasks_.fetch_sub(1, std::memory_order_relaxed);
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);

                completion_condition_.notify_all();
            }
            else
            {
                // No work found, check if we should stop
                if (stop_.load(std::memory_order_acquire))
                {
                    break;
                }

                // Wait for work with adaptive timeout
                std::unique_lock<std::mutex> lock(wakeup_mutex_);
                wakeup_condition_.wait_for(lock, std::chrono::microseconds(100));
            }
        }
    }
};

/**
 * @brief Simple high-performance thread pool using single queue with optimized locking
 *
 * Alternative implementation for cases where work-stealing overhead is not justified.
 * Uses a single queue with optimized batch processing and minimal locking.
 */
class FastThreadPool
{
  public:
    using Task = std::function<void()>;

    struct Statistics
    {
        size_t total_threads;
        size_t active_threads;
        size_t pending_tasks;
        size_t completed_tasks;
        double tasks_per_second;
        std::chrono::microseconds avg_task_time;
    };

    explicit FastThreadPool(size_t num_threads = std::thread::hardware_concurrency())
        : num_threads_(num_threads == 0 ? 1 : num_threads), stop_(false), start_time_(std::chrono::steady_clock::now())
    {
        workers_.reserve(num_threads_);

        // Create worker threads
        for (size_t i = 0; i < num_threads_; ++i)
        {
            workers_.emplace_back(&FastThreadPool::worker_function, this, i);
        }
    }

    FastThreadPool(const FastThreadPool &) = delete;
    FastThreadPool &operator=(const FastThreadPool &) = delete;

    ~FastThreadPool()
    {
        shutdown();
    }

    /**
     * @brief Optimized task submission with minimal locking
     */
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_)
            {
                throw std::runtime_error("FastThreadPool is shutting down");
            }
            tasks_.emplace([task]() { (*task)(); });
        }

        condition_.notify_one();
        return result;
    }

    /**
     * @brief Efficient batch processing
     */
    template <typename Iterator>
    std::vector<std::future<void>> submit_batch(Iterator begin, Iterator end)
    {
        std::vector<std::future<void>> futures;
        const size_t batch_size = std::distance(begin, end);
        futures.reserve(batch_size);

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_)
            {
                throw std::runtime_error("FastThreadPool is shutting down");
            }

            for (auto it = begin; it != end; ++it)
            {
                auto task = std::make_shared<std::packaged_task<void()>>(*it);
                futures.push_back(task->get_future());
                tasks_.emplace([task]() { (*task)(); });
            }
        }

        // Wake up all workers for batch processing
        condition_.notify_all();
        return futures;
    }

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

    bool configure_threads(const std::string &name_prefix, SchedulingPolicy policy = SchedulingPolicy::OTHER,
                           ThreadPriority priority = ThreadPriority::normal())
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

    size_t size() const noexcept
    {
        return num_threads_;
    }

    size_t pending_tasks() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

    Statistics get_statistics() const
    {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

        std::lock_guard<std::mutex> lock(queue_mutex_);
        Statistics stats;
        stats.total_threads = num_threads_;
        stats.active_threads = active_tasks_.load(std::memory_order_acquire);
        stats.pending_tasks = tasks_.size();
        stats.completed_tasks = completed_tasks_.load(std::memory_order_acquire);

        if (elapsed.count() > 0)
        {
            stats.tasks_per_second = static_cast<double>(stats.completed_tasks) / elapsed.count();
        }
        else
        {
            stats.tasks_per_second = 0.0;
        }

        const auto total_task_time = total_task_time_.load(std::memory_order_acquire);
        if (stats.completed_tasks > 0)
        {
            stats.avg_task_time = std::chrono::microseconds(total_task_time / stats.completed_tasks);
        }
        else
        {
            stats.avg_task_time = std::chrono::microseconds(0);
        }

        return stats;
    }

  private:
    size_t num_threads_;
    std::vector<ThreadWrapper> workers_;
    std::queue<Task> tasks_;

    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
    std::atomic<size_t> active_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
    std::atomic<uint64_t> total_task_time_{0}; // microseconds

    std::chrono::steady_clock::time_point start_time_;

    void worker_function(size_t /* worker_id */)
    {
        while (true)
        {
            Task task;
            bool found_task = false;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);

                // Use timeout to avoid indefinite blocking
                if (condition_.wait_for(lock, std::chrono::milliseconds(10),
                                        [this] { return stop_ || !tasks_.empty(); }))
                {
                    if (stop_ && tasks_.empty())
                    {
                        return;
                    }

                    if (!tasks_.empty())
                    {
                        task = std::move(tasks_.front());
                        tasks_.pop();
                        found_task = true;
                    }
                }
                else if (stop_)
                {
                    return;
                }
            }

            if (found_task)
            {
                active_tasks_.fetch_add(1, std::memory_order_relaxed);

                const auto start_time = std::chrono::steady_clock::now();
                try
                {
                    task();
                }
                catch (...)
                {
                    // Log exception or handle as needed
                }
                const auto end_time = std::chrono::steady_clock::now();

                const auto task_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                total_task_time_.fetch_add(task_duration.count(), std::memory_order_relaxed);

                active_tasks_.fetch_sub(1, std::memory_order_relaxed);
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
};

/**
 * @brief Simple thread pool for general-purpose use
 *
 * This is a straightforward thread pool implementation suitable for:
 * - General application use (< 1000 tasks/second)
 * - Simple task submission patterns
 * - Lower memory overhead
 * - Easier to understand and debug
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
        : num_threads_(num_threads == 0 ? 1 : num_threads), stop_(false)
    {
        workers_.reserve(num_threads_);

        // Create worker threads
        for (size_t i = 0; i < num_threads_; ++i)
        {
            workers_.emplace_back(&ThreadPool::worker_function, this);
        }
    }

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    ~ThreadPool()
    {
        shutdown();
    }

    /**
     * @brief Submit a task to the thread pool
     */
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

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
    std::vector<std::future<void>> submit_range(Iterator begin, Iterator end)
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
    template <typename Iterator, typename F>
    void parallel_for_each(Iterator begin, Iterator end, F &&func)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(std::distance(begin, end));

        for (auto it = begin; it != end; ++it)
        {
            futures.push_back(submit([func, it]() { func(*it); }));
        }

        // Wait for all tasks to complete
        for (auto &future : futures)
        {
            future.wait();
        }
    }

    size_t size() const noexcept
    {
        return num_threads_;
    }

    size_t pending_tasks() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

    /**
     * @brief Configure thread properties
     */
    bool configure_threads(const std::string &name_prefix, SchedulingPolicy policy = SchedulingPolicy::OTHER,
                           ThreadPriority priority = ThreadPriority::normal())
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

    void wait_for_tasks()
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        task_finished_condition_.wait(lock, [this] { return tasks_.empty() && active_tasks_ == 0; });
    }

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

    Statistics get_statistics() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        Statistics stats;
        stats.total_threads = num_threads_;
        stats.active_threads = active_tasks_;
        stats.pending_tasks = tasks_.size();
        stats.completed_tasks = completed_tasks_;
        return stats;
    }

  private:
    size_t num_threads_;
    std::vector<ThreadWrapper> workers_;
    std::queue<Task> tasks_;

    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::condition_variable task_finished_condition_;
    std::atomic<bool> stop_;
    std::atomic<size_t> active_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};

    void worker_function()
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
 * @brief Singleton thread pool for global use (simple version)
 */
class GlobalThreadPool
{
  public:
    static ThreadPool &instance()
    {
        static ThreadPool pool(std::thread::hardware_concurrency());
        return pool;
    }

    template <typename F, typename... Args>
    static auto submit(F &&f, Args &&...args)
    {
        return instance().submit(std::forward<F>(f), std::forward<Args>(args)...);
    }

    template <typename Iterator>
    static auto submit_range(Iterator begin, Iterator end)
    {
        return instance().submit_range(begin, end);
    }

    template <typename Iterator, typename F>
    static void parallel_for_each(Iterator begin, Iterator end, F &&func)
    {
        instance().parallel_for_each(begin, end, std::forward<F>(func));
    }

  private:
    GlobalThreadPool() = default;
};

/**
 * @brief Singleton high-performance thread pool for global use
 */
class GlobalHighPerformancePool
{
  public:
    static HighPerformancePool &instance()
    {
        static HighPerformancePool pool(std::thread::hardware_concurrency());
        return pool;
    }

    template <typename F, typename... Args>
    static auto submit(F &&f, Args &&...args)
    {
        return instance().submit(std::forward<F>(f), std::forward<Args>(args)...);
    }

    template <typename Iterator>
    static auto submit_batch(Iterator begin, Iterator end)
    {
        return instance().submit_batch(begin, end);
    }

    template <typename Iterator, typename F>
    static void parallel_for_each(Iterator begin, Iterator end, F &&func)
    {
        instance().parallel_for_each(begin, end, std::forward<F>(func));
    }

  private:
    GlobalHighPerformancePool() = default;
};

/**
 * @brief Convenience function for parallel execution with containers
 */
template <typename Container, typename F>
void parallel_for_each(Container &container, F &&func)
{
    GlobalThreadPool::parallel_for_each(container.begin(), container.end(), std::forward<F>(func));
}

} // namespace threadschedule
