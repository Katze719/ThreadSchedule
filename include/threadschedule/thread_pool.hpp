#pragma once

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
 * @brief Work-stealing deque for per-thread task queues in a thread pool.
 *
 * Implements a double-ended queue where the owning worker thread pushes and
 * pops tasks from the top, while other ("thief") threads steal tasks from the
 * bottom. This asymmetry reduces contention under typical workloads because
 * the owner operates on one end and thieves on the other.
 *
 * @par Thread safety
 * All public operations are serialized by an internal mutex, so the deque is
 * safe to use concurrently from any number of threads. The atomic counters
 * (top_ / bottom_) exist for a fast, lock-free size() / empty() snapshot but
 * do @e not make push/pop/steal lock-free; the mutex is always acquired.
 *
 * @par Capacity
 * The deque has a fixed capacity set at construction (default
 * @c DEFAULT_CAPACITY = 1024). push() returns @c false when the deque is
 * full; it never reallocates. Choose a capacity large enough for your expected
 * burst size or use an overflow queue externally (as @ref HighPerformancePool does).
 *
 * @par Memory layout
 * Each stored item is wrapped in an @c AlignedItem that is aligned to
 * @c CACHE_LINE_SIZE (64 bytes) to prevent false sharing between adjacent
 * elements when multiple threads access neighboring slots.
 *
 * @par Copyability / movability
 * Not copyable and not movable (contains a std::mutex).
 *
 * @tparam T The task type. Must be move-constructible.
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
        AlignedItem(T&& t) : item(std::move(t))
        {
        }
        AlignedItem(T const& t) : item(t)
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

    [[nodiscard]] auto push(T&& item) -> bool
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t const t = top_.load(std::memory_order_relaxed);
        size_t const b = bottom_.load(std::memory_order_relaxed);

        if (t - b >= capacity_)
        {
            return false;
        }

        buffer_[t % capacity_] = AlignedItem(std::move(item));
        top_.store(t + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] auto push(T const& item) -> bool
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t const t = top_.load(std::memory_order_relaxed);
        size_t const b = bottom_.load(std::memory_order_relaxed);

        if (t - b >= capacity_)
        {
            return false;
        }

        buffer_[t % capacity_] = AlignedItem(item);
        top_.store(t + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] auto pop(T& item) -> bool
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t const t = top_.load(std::memory_order_relaxed);
        size_t const b = bottom_.load(std::memory_order_relaxed);

        if (t <= b)
        {
            return false;
        }

        size_t const new_top = t - 1;
        item = std::move(buffer_[new_top % capacity_].item);
        top_.store(new_top, std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] auto steal(T& item) -> bool
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t const b = bottom_.load(std::memory_order_relaxed);
        size_t const t = top_.load(std::memory_order_relaxed);

        if (b >= t)
        {
            return false;
        }

        item = std::move(buffer_[b % capacity_].item);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] auto size() const -> size_t
    {
        size_t const t = top_.load(std::memory_order_relaxed);
        size_t const b = bottom_.load(std::memory_order_relaxed);
        return t > b ? t - b : 0;
    }

    [[nodiscard]] auto empty() const -> bool
    {
        return size() == 0;
    }
};

/**
 * @brief High-performance thread pool optimized for high-frequency task submission.
 *
 * Uses a work-stealing architecture: each worker thread owns a private
 * @ref WorkStealingDeque, and idle workers attempt to steal tasks from other
 * workers' queues. A shared overflow queue absorbs bursts when all per-thread
 * queues are full.
 *
 * Optimizations for 1k+ tasks with 10k+ tasks/second throughput:
 * - Work-stealing architecture with proper synchronization
 * - Per-thread queues with efficient load balancing
 * - Batch processing support for maximum throughput
 * - Optimized wake-up mechanisms
 * - Cache-friendly data structures with proper alignment
 * - Performance monitoring and statistics
 *
 * @par How task execution works
 * When you call submit(), the callable is wrapped in a std::packaged_task and
 * placed into one of the per-worker queues (round-robin selection). A
 * condition_variable then wakes one sleeping worker. The worker picks up the
 * task from its own queue. If its own queue is empty, the worker tries to
 * steal tasks from up to 4 other workers' queues (random selection). If no
 * per-worker queue has work, the worker checks the shared overflow queue. If
 * nothing is found at all, the worker sleeps for up to 100 microseconds
 * before retrying.
 *
 * @par Execution guarantees
 * - Every successfully submitted task (submit() returned without throwing)
 *   is guaranteed to eventually execute, as long as the pool is not destroyed
 *   while shutdown() is draining.
 * - submit() throws std::runtime_error if the pool is already shutting down.
 *   In that case the task is NOT enqueued and will NOT execute.
 * - Tasks are executed in approximately FIFO order per queue, but the
 *   work-stealing mechanism means that the global execution order across all
 *   threads is non-deterministic. There is no ordering guarantee between two
 *   tasks submitted from different threads, or even from the same thread if
 *   they land in different worker queues.
 * - The returned std::future becomes ready once the task has completed. You
 *   can call future.get() to block until the result is available, or
 *   future.wait() to just wait without retrieving the result.
 * - If a task throws an exception, the exception is stored in the future.
 *   Calling future.get() will rethrow it. The worker thread itself continues
 *   to run and process further tasks.
 * - shutdown() sets the stop flag and wakes all workers. Workers finish
 *   their current task and then drain all remaining queued tasks before
 *   exiting. The destructor calls shutdown() implicitly.
 *
 * @par Thread safety
 * submit() and submit_batch() may be called from any thread concurrently.
 * shutdown() is internally guarded and is safe to call more than once.
 *
 * @par Exception handling
 * Exceptions thrown by tasks are caught inside the worker loop. They do not
 * propagate to the caller directly, but are stored in the std::future
 * returned by submit(). Call future.get() to observe or rethrow the
 * exception. The worker thread is not affected and continues processing.
 *
 * @par Statistics accuracy
 * Counters such as completed_tasks_, stolen_tasks_, and total_task_time_
 * are updated with std::memory_order_relaxed, so the values returned by
 * get_statistics() are approximate and may lag behind the true counts by
 * a small margin.
 *
 * @par Blocking
 * wait_for_tasks() blocks the calling thread until every queued and currently
 * active task has finished.
 *
 * @par Lifetime
 * The destructor calls shutdown() and joins all worker threads. It is safe
 * to let the pool go out of scope while tasks are still running; they will be
 * drained first. Note that this means the destructor can block for a long
 * time if tasks are slow.
 *
 * @par Copyability / movability
 * Not copyable, not movable.
 *
 * @note Has overhead for small task counts (< 100 tasks) due to
 *       work-stealing complexity. Best for high-throughput scenarios like
 *       image processing, batch operations, etc.
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
        worker_queues_.resize(num_threads_);
        for (size_t i = 0; i < num_threads_; ++i)
        {
            worker_queues_[i] = std::make_unique<WorkStealingDeque<Task>>();
        }

        workers_.reserve(num_threads_);

        for (size_t i = 0; i < num_threads_; ++i)
        {
            workers_.emplace_back(&HighPerformancePool::worker_function, this, i);
        }
    }

    HighPerformancePool(HighPerformancePool const&) = delete;
    auto operator=(HighPerformancePool const&) -> HighPerformancePool& = delete;

    ~HighPerformancePool()
    {
        shutdown();
    }

    /**
     * @brief High-performance task submission (optimized hot path)
     */
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> result = task->get_future();

        if (stop_.load(std::memory_order_acquire))
        {
            throw std::runtime_error("HighPerformancePool is shutting down");
        }

        size_t const preferred_queue = next_victim_.fetch_add(1, std::memory_order_relaxed) % num_threads_;

        if (worker_queues_[preferred_queue]->push([task]() { (*task)(); }))
        {
            wakeup_condition_.notify_one();
            return result;
        }

        for (size_t attempts = 0; attempts < (std::min)(num_threads_, size_t(3)); ++attempts)
        {
            size_t const idx = (preferred_queue + attempts + 1) % num_threads_;
            if (worker_queues_[idx]->push([task]() { (*task)(); }))
            {
                wakeup_condition_.notify_one();
                return result;
            }
        }

        {
            std::lock_guard<std::mutex> lock(overflow_mutex_);
            if (stop_.load(std::memory_order_relaxed))
            {
                throw std::runtime_error("HighPerformancePool is shutting down");
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
    auto submit_batch(Iterator begin, Iterator end) -> std::vector<std::future<void>>
    {
        std::vector<std::future<void>> futures;
        size_t const batch_size = std::distance(begin, end);
        futures.reserve(batch_size);

        if (stop_.load(std::memory_order_acquire))
        {
            throw std::runtime_error("HighPerformancePool is shutting down");
        }

        size_t queue_idx = next_victim_.fetch_add(batch_size, std::memory_order_relaxed) % num_threads_;

        for (auto it = begin; it != end; ++it)
        {
            auto task = std::make_shared<std::packaged_task<void()>>(*it);
            futures.push_back(task->get_future());

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
                std::lock_guard<std::mutex> lock(overflow_mutex_);
                overflow_tasks_.emplace([task]() { (*task)(); });
            }
        }

        wakeup_condition_.notify_all();
        return futures;
    }

    /**
     * @brief Optimized parallel for_each with work distribution
     */
    template <typename Iterator, typename F>
    void parallel_for_each(Iterator begin, Iterator end, F&& func)
    {
        size_t const total_items = std::distance(begin, end);
        if (total_items == 0)
            return;

        size_t const chunk_size = (std::max)(size_t(1), total_items / (num_threads_ * 4));
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

        for (auto& future : futures)
        {
            future.wait();
        }
    }

    [[nodiscard]] auto size() const noexcept -> size_t
    {
        return num_threads_;
    }

    [[nodiscard]] auto pending_tasks() const -> size_t
    {
        size_t total = 0;
        for (auto const& queue : worker_queues_)
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
    auto configure_threads(std::string const& name_prefix, SchedulingPolicy policy = SchedulingPolicy::OTHER,
                           ThreadPriority priority = ThreadPriority::normal()) -> expected<void, std::error_code>
    {
        bool success = true;

        for (size_t i = 0; i < workers_.size(); ++i)
        {
            std::string const thread_name = name_prefix + "_" + std::to_string(i);

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

    auto set_affinity(ThreadAffinity const& affinity) -> expected<void, std::error_code>
    {
        bool success = true;

        for (auto& worker : workers_)
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

    auto distribute_across_cpus() -> expected<void, std::error_code>
    {
        auto const cpu_count = std::thread::hardware_concurrency();
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
                return;
            }
        }

        wakeup_condition_.notify_all();

        for (auto& worker : workers_)
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
    auto get_statistics() const -> Statistics
    {
        auto const now = std::chrono::steady_clock::now();
        auto const elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

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

        auto const total_task_time = total_task_time_.load(std::memory_order_acquire);
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

    std::queue<Task> overflow_tasks_;
    mutable std::mutex overflow_mutex_;

    std::atomic<bool> stop_;
    std::condition_variable wakeup_condition_;
    std::mutex wakeup_mutex_;

    std::condition_variable completion_condition_;
    std::mutex completion_mutex_;

    std::atomic<size_t> next_victim_;
    std::atomic<size_t> active_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
    std::atomic<size_t> stolen_tasks_{0};
    std::atomic<uint64_t> total_task_time_{0};

    std::chrono::steady_clock::time_point start_time_;

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void worker_function(size_t worker_id)
    {
        thread_local std::mt19937 gen = []() {
            std::random_device device;
            return std::mt19937(device());
        }();

        Task task;
        std::uniform_int_distribution<size_t> dist(0, num_threads_ - 1);

        while (true)
        {
            bool found_task = false;

            if (worker_queues_[worker_id]->pop(task))
            {
                found_task = true;
            }
            else
            {
                size_t const max_steal_attempts = (std::min)(num_threads_, size_t(4));
                for (size_t attempts = 0; attempts < max_steal_attempts; ++attempts)
                {
                    size_t const victim_id = dist(gen);
                    if (victim_id != worker_id && worker_queues_[victim_id]->steal(task))
                    {
                        found_task = true;
                        stolen_tasks_.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                }
            }

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
                active_tasks_.fetch_add(1, std::memory_order_relaxed);

                auto const start_time = std::chrono::steady_clock::now();
                try
                {
                    task();
                }
                catch (...)
                {
                }
                auto const end_time = std::chrono::steady_clock::now();

                auto const task_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                total_task_time_.fetch_add(task_duration.count(), std::memory_order_relaxed);

                active_tasks_.fetch_sub(1, std::memory_order_relaxed);
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);

                completion_condition_.notify_all();
            }
            else
            {
                if (stop_.load(std::memory_order_acquire))
                {
                    break;
                }

                std::unique_lock<std::mutex> lock(wakeup_mutex_);
                wakeup_condition_.wait_for(lock, std::chrono::microseconds(100));
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Wait policies for ThreadPoolBase
// ---------------------------------------------------------------------------

/**
 * @brief Wait policy that blocks indefinitely until work is available.
 *
 * Workers consume zero CPU while idle but wake instantly when a task is
 * enqueued. Used by the @c ThreadPool type alias.
 */
struct IndefiniteWait
{
    template <typename Lock, typename Pred>
    static auto wait(std::condition_variable& cv, Lock& lock, Pred pred) -> bool
    {
        cv.wait(lock, pred);
        return true;
    }
};

/**
 * @brief Wait policy that polls with a 10 ms timeout.
 *
 * Workers periodically re-check the queue even without notification, trading
 * a small amount of CPU for lower wake-up latency under bursty workloads.
 * Used by the @c FastThreadPool type alias.
 */
struct PollingWait
{
    template <typename Lock, typename Pred>
    static auto wait(std::condition_variable& cv, Lock& lock, Pred pred) -> bool
    {
        return cv.wait_for(lock, std::chrono::milliseconds(10), pred);
    }
};

// ---------------------------------------------------------------------------
// ThreadPoolBase
// ---------------------------------------------------------------------------

/**
 * @brief Single-queue thread pool parameterized by its idle-wait strategy.
 *
 * All tasks share one std::queue protected by a single mutex. The
 * @p WaitPolicy template parameter controls how workers wait for new
 * work:
 * - @ref IndefiniteWait - blocks on condition_variable::wait() (zero CPU
 *   while idle, instant wake). Instantiated as @c ThreadPool.
 * - @ref PollingWait - polls with condition_variable::wait_for(10 ms).
 *   Slightly higher idle CPU but lower worst-case latency under bursty
 *   loads. Instantiated as @c FastThreadPool.
 *
 * @par How task execution works
 * When you call submit(), the callable is wrapped in a std::packaged_task,
 * pushed into the shared task queue under a mutex lock, and one sleeping
 * worker is woken via condition_variable::notify_one(). The woken worker
 * pops the front element and executes it.
 *
 * @par Execution guarantees
 * - Every successfully submitted task (submit() returned without throwing)
 *   is guaranteed to eventually execute.
 * - submit() throws std::runtime_error if the pool is already shutting
 *   down. In that case the task is NOT enqueued.
 * - Tasks are stored in a FIFO queue. Multiple workers pop concurrently,
 *   so submission order is roughly preserved but completion order is
 *   non-deterministic.
 * - The returned std::future becomes ready once the task finishes. If the
 *   task threw an exception, future.get() rethrows it.
 * - On shutdown(), workers finish their current task, then drain all
 *   remaining queued tasks before exiting.
 * - wait_for_tasks() blocks until the queue is empty AND no worker is
 *   currently executing a task.
 *
 * @par Thread safety
 * submit() and submit_batch() may be called from any thread concurrently.
 * shutdown() is internally guarded and safe to call more than once.
 *
 * @par Exception handling
 * Exceptions thrown by tasks are caught inside the worker loop. They are
 * stored in the std::future returned by submit(). The worker thread
 * continues processing.
 *
 * @par Lifetime
 * The destructor calls shutdown() and joins all worker threads. Can block
 * if tasks are still running.
 *
 * @par Copyability / movability
 * Not copyable, not movable.
 *
 * @tparam WaitPolicy Strategy type with a static
 *         @c wait(cv, lock, predicate) -> bool method.
 */
template <typename WaitPolicy>
class ThreadPoolBase
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

    explicit ThreadPoolBase(size_t num_threads = std::thread::hardware_concurrency())
        : num_threads_(num_threads == 0 ? 1 : num_threads), stop_(false),
          start_time_(std::chrono::steady_clock::now())
    {
        workers_.reserve(num_threads_);

        for (size_t i = 0; i < num_threads_; ++i)
        {
            workers_.emplace_back(&ThreadPoolBase::worker_function, this, i);
        }
    }

    ThreadPoolBase(ThreadPoolBase const&) = delete;
    auto operator=(ThreadPoolBase const&) -> ThreadPoolBase& = delete;

    ~ThreadPoolBase()
    {
        shutdown();
    }

    /**
     * @brief Submit a task to the thread pool
     */
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_)
            {
                throw std::runtime_error("Pool is shutting down");
            }
            tasks_.emplace([task]() { (*task)(); });
        }

        condition_.notify_one();
        return result;
    }

    /**
     * @brief Submit multiple tasks under a single lock acquisition
     */
    template <typename Iterator>
    auto submit_batch(Iterator begin, Iterator end) -> std::vector<std::future<void>>
    {
        std::vector<std::future<void>> futures;
        futures.reserve(std::distance(begin, end));

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_)
            {
                throw std::runtime_error("Pool is shutting down");
            }

            for (auto it = begin; it != end; ++it)
            {
                auto task = std::make_shared<std::packaged_task<void()>>(*it);
                futures.push_back(task->get_future());
                tasks_.emplace([task]() { (*task)(); });
            }
        }

        condition_.notify_all();
        return futures;
    }

    /**
     * @brief Apply a function to a range of values in parallel
     */
    template <typename Iterator, typename F>
    void parallel_for_each(Iterator begin, Iterator end, F&& func)
    {
        std::vector<std::future<void>> futures;
        futures.reserve(std::distance(begin, end));

        for (auto it = begin; it != end; ++it)
        {
            futures.push_back(submit([func, it]() { func(*it); }));
        }

        for (auto& future : futures)
        {
            future.wait();
        }
    }

    [[nodiscard]] auto size() const noexcept -> size_t
    {
        return num_threads_;
    }

    [[nodiscard]] auto pending_tasks() const -> size_t
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

    /**
     * @brief Configure all worker threads (name, scheduling policy, priority)
     */
    auto configure_threads(std::string const& name_prefix, SchedulingPolicy policy = SchedulingPolicy::OTHER,
                           ThreadPriority priority = ThreadPriority::normal()) -> expected<void, std::error_code>
    {
        bool success = true;

        for (size_t i = 0; i < workers_.size(); ++i)
        {
            std::string const thread_name = name_prefix + "_" + std::to_string(i);

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

    /**
     * @brief Set CPU affinity for all worker threads
     */
    auto set_affinity(ThreadAffinity const& affinity) -> expected<void, std::error_code>
    {
        bool success = true;

        for (auto& worker : workers_)
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

    /**
     * @brief Distribute workers across available CPUs (round-robin)
     */
    auto distribute_across_cpus() -> expected<void, std::error_code>
    {
        auto const cpu_count = std::thread::hardware_concurrency();
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
        std::unique_lock<std::mutex> lock(queue_mutex_);
        task_finished_condition_.wait(
            lock, [this] { return tasks_.empty() && active_tasks_.load(std::memory_order_acquire) == 0; });
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

        for (auto& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }

        workers_.clear();
    }

    /**
     * @brief Get performance statistics
     */
    [[nodiscard]] auto get_statistics() const -> Statistics
    {
        auto const now = std::chrono::steady_clock::now();
        auto const elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

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

        auto const total_task_time = total_task_time_.load(std::memory_order_acquire);
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
    std::condition_variable task_finished_condition_;
    std::atomic<bool> stop_;
    std::atomic<size_t> active_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
    std::atomic<uint64_t> total_task_time_{0};

    std::chrono::steady_clock::time_point start_time_;

    void worker_function(size_t /*worker_id*/)
    {
        while (true)
        {
            Task task;
            bool found_task = false;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);

                if (WaitPolicy::wait(condition_, lock, [this] { return stop_ || !tasks_.empty(); }))
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
                        active_tasks_.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else if (stop_)
                {
                    return;
                }
            }

            if (found_task)
            {
                auto const start_time = std::chrono::steady_clock::now();
                try
                {
                    task();
                }
                catch (...)
                {
                }
                auto const end_time = std::chrono::steady_clock::now();

                auto const task_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                total_task_time_.fetch_add(task_duration.count(), std::memory_order_relaxed);

                active_tasks_.fetch_sub(1, std::memory_order_relaxed);
                completed_tasks_.fetch_add(1, std::memory_order_relaxed);

                task_finished_condition_.notify_all();
            }
        }
    }
};

/**
 * @brief General-purpose thread pool with indefinite blocking wait.
 *
 * Workers block on condition_variable::wait() when idle - zero CPU
 * consumption, instant wake-up on task submission. Suitable for most
 * workloads.
 *
 * @see ThreadPoolBase, IndefiniteWait
 */
using ThreadPool = ThreadPoolBase<IndefiniteWait>;

/**
 * @brief Thread pool with 10 ms polling wait for lower wake-up latency.
 *
 * Workers poll with condition_variable::wait_for(10 ms), trading a small
 * amount of idle CPU for more consistent latency under bursty workloads.
 *
 * @see ThreadPoolBase, PollingWait
 */
using FastThreadPool = ThreadPoolBase<PollingWait>;

// ---------------------------------------------------------------------------
// GlobalPool
// ---------------------------------------------------------------------------

/**
 * @brief Singleton accessor for a process-wide pool instance.
 *
 * Provides static convenience methods that forward to a single pool
 * whose lifetime is managed as a function-local static (Meyer's singleton).
 *
 * @par Thread safety
 * The underlying pool is created on the first call to instance() and is
 * guaranteed to be thread-safe in C++11 and later (magic statics). All
 * forwarded methods are as thread-safe as the corresponding pool methods.
 *
 * @par Pool size
 * The pool is created with @c std::thread::hardware_concurrency() threads.
 * This size is fixed for the lifetime of the process.
 *
 * @par Static destruction order
 * Because the pool is a function-local static, it is destroyed during static
 * destruction in reverse order of construction. Submitting work to the global
 * pool from destructors of other static objects is undefined behaviour if the
 * pool has already been destroyed.
 *
 * @par Copyability / movability
 * Not instantiable (private constructor). All access is through static
 * methods.
 *
 * @tparam PoolType The concrete pool type to wrap.
 */
template <typename PoolType>
class GlobalPool
{
  public:
    static auto instance() -> PoolType&
    {
        static PoolType pool(std::thread::hardware_concurrency());
        return pool;
    }

    template <typename F, typename... Args>
    static auto submit(F&& f, Args&&... args)
    {
        return instance().submit(std::forward<F>(f), std::forward<Args>(args)...);
    }

    template <typename Iterator>
    static auto submit_batch(Iterator begin, Iterator end)
    {
        return instance().submit_batch(begin, end);
    }

    template <typename Iterator, typename F>
    static void parallel_for_each(Iterator begin, Iterator end, F&& func)
    {
        instance().parallel_for_each(begin, end, std::forward<F>(func));
    }

  private:
    GlobalPool() = default;
};

/** @brief Singleton @ref ThreadPool accessor. */
using GlobalThreadPool = GlobalPool<ThreadPool>;

/** @brief Singleton @ref HighPerformancePool accessor. */
using GlobalHighPerformancePool = GlobalPool<HighPerformancePool>;

/**
 * @brief Convenience wrapper that applies a callable to every element of a
 *        container in parallel using the @ref GlobalThreadPool singleton.
 *
 * Equivalent to:
 * @code
 * GlobalThreadPool::parallel_for_each(container.begin(), container.end(), func);
 * @endcode
 *
 * The call blocks until every element has been processed.
 *
 * @tparam Container Any type exposing begin() / end() iterators.
 * @tparam F         Callable compatible with @c void(Container::value_type&).
 *
 * @param container The container whose elements will be processed.
 * @param func      The callable applied to each element.
 */
template <typename Container, typename F>
void parallel_for_each(Container& container, F&& func)
{
    GlobalThreadPool::parallel_for_each(container.begin(), container.end(), std::forward<F>(func));
}

} // namespace threadschedule
