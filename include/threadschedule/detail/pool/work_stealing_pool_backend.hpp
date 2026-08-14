#pragma once

/**
 * @file detail/pool/work_stealing_pool_backend.hpp
 * @brief Work-stealing deque and pool implementation.
 *
 * Self-contained internal implementation header.
 */

#include "../callable/bind.hpp"
#include "../callable/move_only_function.hpp"
#include "callbacks.hpp"
#include "shutdown_policy_backend.hpp"
#include "work_stealing_deque.hpp"
#include "worker_context_guard.hpp"
#include "worker_count.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <shared_mutex>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace threadschedule::detail
{

/**
 * @brief High-performance thread pool optimized for high-frequency task
 * submission.
 *
 * Uses a work-stealing architecture: each worker thread owns a private
 * @ref work_stealing_deque, and idle workers attempt to steal tasks from other
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
 * @par statistics accuracy
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
class work_stealing_pool_backend
{
public:
  using task_type = std::function<void()>;
  using queued_task = detail::move_only_function<void()>;

  struct statistics
  {
    size_t total_threads;
    size_t active_threads;
    size_t pending_tasks;
    size_t completed_tasks;
    size_t stolen_tasks;
    double tasks_per_second;
    std::chrono::microseconds avg_task_time;
  };

  explicit work_stealing_pool_backend(size_t num_threads = default_worker_count(),
                                      size_t deque_capacity = work_stealing_deque<queued_task>::default_capacity,
                                      bool register_workers = false)
      : num_threads_(checked_worker_count(num_threads)), register_workers_(register_workers), stop_(false),
        next_victim_(0), start_time_(std::chrono::steady_clock::now())
  {
    worker_queues_.resize(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i)
      {
        worker_queues_[i] = std::make_unique<work_stealing_deque<queued_task>>(deque_capacity);
      }

    workers_.reserve(num_threads_);

    try
      {
        for (size_t i = 0; i < num_threads_; ++i)
          workers_.emplace_back(&work_stealing_pool_backend::worker_function, this, i);
      }
    catch (...)
      {
        stop_.store(true, std::memory_order_release);
        submissions_quiesced_.store(true, std::memory_order_release);
        wakeup_condition_.notify_all();
        for (auto& worker : workers_)
          if (worker.joinable())
            worker.join();
        throw;
      }
  }

  work_stealing_pool_backend(work_stealing_pool_backend const&) = delete;
  auto operator=(work_stealing_pool_backend const&) -> work_stealing_pool_backend& = delete;

  ~work_stealing_pool_backend()
  {
    shutdown(shutdown_policy_backend::drain);
  }

  /**
   * @brief Shut the pool down.
   *
   * @param policy @c drain (default) finishes all queued tasks;
   *               @c drop_pending discards queued tasks.
   */
  void
  shutdown(shutdown_policy_backend policy = shutdown_policy_backend::drain)
  {
    if (is_current_worker())
      detail::throw_worker_deadlock();
    std::lock_guard<std::recursive_mutex> shutdown_lock(shutdown_mutex_);
    if (stop_.exchange(true, std::memory_order_acq_rel))
      return;
    shutdown_completed_all_ = finish_shutdown(policy);
    shutdown_completed_at_ = std::chrono::steady_clock::now();
  }

  /**
   * @brief Attempt a timed drain: finish as many tasks as possible within
   *        @p timeout, then discard queued work and wait for running tasks.
   *
   * New submissions are rejected before the timed wait begins.
   * Running C++ callables cannot be stopped safely, so this function can
   * return after the timeout while an already-running task finishes.
   * @return @c true if all tasks completed within the deadline,
   *         @c false if the timeout expired first.
   */
  auto
  shutdown_for(std::chrono::milliseconds timeout) -> bool
  {
    if (is_current_worker())
      detail::throw_worker_deadlock();
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    std::lock_guard<std::recursive_mutex> shutdown_lock(shutdown_mutex_);

    if (stop_.exchange(true, std::memory_order_acq_rel))
      return shutdown_completed_all_ && shutdown_completed_at_ <= deadline;

    {
      std::unique_lock<std::shared_mutex> submission_lock(submission_mutex_);
      submissions_quiesced_.store(true, std::memory_order_release);
    }
    wakeup_condition_.notify_all();

    std::unique_lock<std::mutex> lock(completion_mutex_);
    bool const drained = completion_condition_.wait_until(
        lock, deadline, [this] { return outstanding_tasks_.load(std::memory_order_acquire) == 0; });
    lock.unlock();

    shutdown_completed_all_
        = finish_shutdown(drained ? shutdown_policy_backend::drain : shutdown_policy_backend::drop_pending);
    shutdown_completed_at_ = std::chrono::steady_clock::now();
    return drained;
  }

  /**
   * @brief Submit a task without throwing on shutdown.
   *
   * Wraps the callable in a @c std::packaged_task and enqueues it.
   * Returns an @c expected containing the @c std::future on success,
   * or @c std::errc::operation_canceled if the pool is shutting down.
   *
   * @tparam F   Callable type.
   * @tparam Args Argument types forwarded to @p F.
   * @param  f   Callable to execute.
   * @param  args Arguments forwarded to @p f.
   * @return @c expected<std::future<R>, std::error_code> where
   *         @c R = @c std::invoke_result_t<F, Args...>.
   *
   * @see submit() for the throwing variant.
   */
  template <typename F, typename... Args>
  auto
  try_submit(F&& f, Args&&... args) -> expected<std::future<std::invoke_result_t<F, Args...>>, std::error_code>
  {
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        detail::bind_args(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> result = task->get_future();
    queued_task queued([task]() { (*task)(); });

    std::shared_lock<std::shared_mutex> submission_lock(submission_mutex_);

    if (stop_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));

    size_t const preferred_queue = next_victim_.fetch_add(1, std::memory_order_relaxed) % num_threads_;

    outstanding_tasks_.fetch_add(1, std::memory_order_release);
    if (worker_queues_[preferred_queue]->push(std::move(queued)))
      {
        wakeup_condition_.notify_one();
        return result;
      }
    outstanding_tasks_.fetch_sub(1, std::memory_order_acq_rel);

    for (size_t attempts = 0; attempts < (std::min)(num_threads_, size_t(3)); ++attempts)
      {
        size_t const idx = (preferred_queue + attempts + 1) % num_threads_;
        outstanding_tasks_.fetch_add(1, std::memory_order_release);
        // A failed push does not consume the queued task.
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if (worker_queues_[idx]->push(std::move(queued)))
          {
            wakeup_condition_.notify_one();
            return result;
          }
        outstanding_tasks_.fetch_sub(1, std::memory_order_acq_rel);
      }

    {
      std::lock_guard<std::mutex> lock(overflow_mutex_);
      if (stop_.load(std::memory_order_relaxed))
        return unexpected(std::make_error_code(std::errc::operation_canceled));
      // Failed deque pushes preserve the queued task.
      // NOLINTNEXTLINE(bugprone-use-after-move)
      overflow_tasks_.emplace(std::move(queued));
      outstanding_tasks_.fetch_add(1, std::memory_order_release);
    }

    wakeup_condition_.notify_all();
    return result;
  }

  /**
   * @brief Submit a task, throwing on shutdown.
   *
   * Equivalent to @ref try_submit but throws @c std::runtime_error instead
   * of returning an error code when the pool is shutting down.
   *
   * @throws std::runtime_error If the pool is shutting down.
   * @return @c std::future<R> that becomes ready when the task completes.
   */
  template <typename F, typename... Args>
  auto
  submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
  {
    auto result = try_submit(std::forward<F>(f), std::forward<Args>(args)...);
    if (!result.has_value())
      throw std::runtime_error("work_stealing_pool_backend is shutting down");
    return std::move(result.value());
  }

  /**
   * @brief Fire-and-forget task submission (throwing variant).
   *
   * Enqueues a callable without creating a @c std::packaged_task or
   * @c std::future, giving roughly 3x higher throughput than \c submit()
   * for tasks whose return value is not needed.
   *
   * @throws std::runtime_error If the pool is shutting down.
   * @see try_post() for the non-throwing variant.
   */
  template <typename F, typename... Args>
  void
  post(F&& f, Args&&... args)
  {
    auto r = try_post(std::forward<F>(f), std::forward<Args>(args)...);
    if (!r.has_value())
      throw std::runtime_error("work_stealing_pool_backend is shutting down");
  }

  /**
   * @brief Fire-and-forget task submission (non-throwing variant).
   *
   * @return @c expected<void, std::error_code> --
   *         @c std::errc::operation_canceled on shutdown.
   */
  template <typename F, typename... Args>
  auto
  try_post(F&& f, Args&&... args) -> expected<void, std::error_code>
  {
    queued_task bound(
        detail::make_move_only_function<void()>(detail::bind_args(std::forward<F>(f), std::forward<Args>(args)...)));

    std::shared_lock<std::shared_mutex> submission_lock(submission_mutex_);

    if (stop_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));

    size_t const preferred_queue = next_victim_.fetch_add(1, std::memory_order_relaxed) % num_threads_;

    outstanding_tasks_.fetch_add(1, std::memory_order_release);
    if (worker_queues_[preferred_queue]->push(std::move(bound)))
      {
        wakeup_condition_.notify_one();
        return {};
      }
    outstanding_tasks_.fetch_sub(1, std::memory_order_acq_rel);

    for (size_t attempts = 0; attempts < (std::min)(num_threads_, size_t(3)); ++attempts)
      {
        size_t const idx = (preferred_queue + attempts + 1) % num_threads_;
        outstanding_tasks_.fetch_add(1, std::memory_order_release);
        // work_stealing_deque::push only moves from the task after it has
        // confirmed capacity; a failed push deliberately preserves it.
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if (worker_queues_[idx]->push(std::move(bound)))
          {
            wakeup_condition_.notify_one();
            return {};
          }
        outstanding_tasks_.fetch_sub(1, std::memory_order_acq_rel);
      }

    {
      std::lock_guard<std::mutex> lock(overflow_mutex_);
      if (stop_.load(std::memory_order_relaxed))
        return unexpected(std::make_error_code(std::errc::operation_canceled));
      // Failed deque pushes preserve the queued task.
      // NOLINTNEXTLINE(bugprone-use-after-move)
      overflow_tasks_.emplace(std::move(bound));
      outstanding_tasks_.fetch_add(1, std::memory_order_release);
    }

    wakeup_condition_.notify_all();
    return {};
  }

  /**
   * @brief Submit a range of @c void() callables in one go (non-throwing).
   *
   * Acquires the lock once per batch, distributing tasks across worker
   * queues in round-robin fashion. Significantly more efficient than
   * calling @c submit() in a loop for large batches.
   *
   * @tparam Iterator Forward iterator whose value_type is callable as @c
   * void().
   * @return @c expected containing a vector of futures, or
   *         @c std::errc::operation_canceled on shutdown.
   */
  template <typename Iterator>
  auto
  try_submit_batch(Iterator begin, Iterator end) -> expected<std::vector<std::future<void>>, std::error_code>
  {
    std::vector<std::future<void>> futures;
    size_t const batch_size = std::distance(begin, end);
    futures.reserve(batch_size);
    std::vector<queued_task> prepared;
    prepared.reserve(batch_size);

    for (auto it = begin; it != end; ++it)
      {
        auto task = std::make_shared<std::packaged_task<void()>>(*it);
        futures.push_back(task->get_future());
        prepared.emplace_back([task]() { (*task)(); });
      }

    std::shared_lock<std::shared_mutex> submission_lock(submission_mutex_);

    if (stop_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));

    size_t queue_idx = next_victim_.fetch_add(batch_size, std::memory_order_relaxed) % num_threads_;

    try
      {
        for (auto& queued : prepared)
          {
            bool enqueued = false;
            for (size_t attempts = 0; attempts < num_threads_; ++attempts)
              {
                outstanding_tasks_.fetch_add(1, std::memory_order_release);
                // A failed push does not consume the queued task.
                // NOLINTNEXTLINE(bugprone-use-after-move)
                if (worker_queues_[queue_idx]->push(std::move(queued)))
                  {
                    enqueued = true;
                    break;
                  }
                outstanding_tasks_.fetch_sub(1, std::memory_order_acq_rel);
                queue_idx = (queue_idx + 1) % num_threads_;
              }

            if (!enqueued)
              {
                std::lock_guard<std::mutex> lock(overflow_mutex_);
                // Failed deque pushes preserve the queued task.
                // NOLINTNEXTLINE(bugprone-use-after-move)
                overflow_tasks_.emplace(std::move(queued));
                outstanding_tasks_.fetch_add(1, std::memory_order_release);
              }
          }
      }
    catch (...)
      {
        wakeup_condition_.notify_all();
        throw;
      }

    wakeup_condition_.notify_all();
    return futures;
  }

  /**
   * @brief Submit a range of @c void() callables in one go (throwing).
   * @throws std::runtime_error If the pool is shutting down.
   * @see try_submit_batch() for the non-throwing variant.
   */
  template <typename Iterator>
  auto
  submit_batch(Iterator begin, Iterator end) -> std::vector<std::future<void>>
  {
    auto result = try_submit_batch(begin, end);
    if (!result.has_value())
      throw std::runtime_error("work_stealing_pool_backend is shutting down");
    return std::move(result.value());
  }

  /**
   * @brief Apply @p func to every element in @c [begin, end) in parallel.
   *
   * The range is split into chunks and submitted as tasks. Blocks until
   * all elements have been processed.
   */
  template <typename Iterator, typename F>
  void
  parallel_for_each(Iterator begin, Iterator end, F&& func)
  {
    if (is_current_worker())
      detail::throw_worker_deadlock();
    detail::parallel_for_each_chunked(*this, begin, end, std::forward<F>(func), num_threads_);
  }

  /// @name Observers
  /// @{

  /// @brief Number of worker threads in this pool.
  [[nodiscard]] auto
  size() const noexcept -> size_t
  {
    return num_threads_;
  }

  /// @brief Approximate count of tasks waiting in all queues.
  [[nodiscard]] auto
  pending_tasks() const -> size_t
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

  /// @brief Collect approximate performance counters.
  auto
  get_statistics() const -> statistics
  {
    auto const now = std::chrono::steady_clock::now();
    auto const elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

    statistics stats;
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

  /// @}

  /// @name Thread configuration
  /// @{

  /**
   * @brief Name, schedule and prioritize all worker threads.
   *
   * Each worker is named @c name_prefix + "_0", @c "_1", etc.
   *
   * @return @c expected<void, std::error_code> - error if the OS
   *         rejected any configuration call.
   */
  auto
  configure_threads(std::string const& name_prefix, native_scheduling_policy policy = native_scheduling_policy::other,
                    native_thread_priority priority = native_thread_priority::normal())
      -> expected<void, std::error_code>
  {
    return detail::configure_worker_threads(workers_, name_prefix, policy, priority);
  }

  auto
  configure_threads(native_thread_config const& config) -> expected<void, std::error_code>
  {
    return detail::configure_worker_threads(workers_, config);
  }

  /// @brief Pin all workers to the same CPU set.
  auto
  set_affinity(native_thread_affinity const& affinity) -> expected<void, std::error_code>
  {
    return detail::set_worker_affinity(workers_, affinity);
  }

  /// @brief Pin each worker to a distinct CPU core (round-robin).
  auto
  distribute_across_cpus() -> expected<void, std::error_code>
  {
    return detail::distribute_workers_across_cpus(workers_);
  }

  /// @}

  /// @name Synchronisation
  /// @{

  /// @brief Block until all pending and active tasks have completed.
  void
  wait_for_tasks()
  {
    if (is_current_worker())
      detail::throw_worker_deadlock();
    std::unique_lock<std::mutex> lock(completion_mutex_);
    completion_condition_.wait(lock, [this] { return outstanding_tasks_.load(std::memory_order_acquire) == 0; });
  }

  [[nodiscard]] auto
  is_current_worker() const noexcept -> bool
  {
    return current_pool == this;
  }

  /// @}

  /// @name Tracing hooks
  /// @{

  /**
   * @brief Register a callback invoked just before each task executes.
   * @param cb Receives the start time and the worker's @c std::thread::id.
   */
  void
  set_on_task_start(task_start_callback cb)
  {
    std::lock_guard<std::mutex> lock(trace_mutex_);
    on_task_start_ = task_start_callback_storage(std::move(cb));
  }

  template <typename Callback,
            std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<Callback>, task_start_callback>, int> = 0>
  void
  set_on_task_start(Callback&& cb)
  {
    static_assert(std::is_invocable_r_v<void, Callback&, std::chrono::steady_clock::time_point, std::thread::id>,
                  "Task start callback must accept (time_point, std::thread::id)");
    std::lock_guard<std::mutex> lock(trace_mutex_);
    on_task_start_ = detail::make_copyable_function<void(std::chrono::steady_clock::time_point, std::thread::id)>(
        std::forward<Callback>(cb));
  }

  /**
   * @brief Register a callback invoked just after each task completes.
   * @param cb Receives the end time, the worker's @c std::thread::id,
   *           and the wall-clock duration of the task.
   */
  void
  set_on_task_end(task_end_callback cb)
  {
    std::lock_guard<std::mutex> lock(trace_mutex_);
    on_task_end_ = task_end_callback_storage(std::move(cb));
  }

  template <typename Callback,
            std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<Callback>, task_end_callback>, int> = 0>
  void
  set_on_task_end(Callback&& cb)
  {
    static_assert(std::is_invocable_r_v<void, Callback&, std::chrono::steady_clock::time_point, std::thread::id,
                                        std::chrono::microseconds>,
                  "Task end callback must accept (time_point, std::thread::id, "
                  "std::chrono::microseconds)");
    std::lock_guard<std::mutex> lock(trace_mutex_);
    on_task_end_ = detail::make_copyable_function<void(std::chrono::steady_clock::time_point, std::thread::id,
                                                       std::chrono::microseconds)>(std::forward<Callback>(cb));
  }

  /// @}

private:
  size_t num_threads_;
  bool register_workers_;
  std::vector<detail::thread_backend> workers_;
  std::vector<std::unique_ptr<work_stealing_deque<queued_task>>> worker_queues_;

  std::queue<queued_task> overflow_tasks_;
  mutable std::mutex overflow_mutex_;
  mutable std::shared_mutex submission_mutex_;
  std::recursive_mutex shutdown_mutex_;

  std::atomic<bool> stop_;
  std::atomic<bool> submissions_quiesced_{ false };
  bool shutdown_completed_all_{ true };
  std::chrono::steady_clock::time_point shutdown_completed_at_{};
  std::condition_variable wakeup_condition_;
  std::mutex wakeup_mutex_;

  std::condition_variable completion_condition_;
  std::mutex completion_mutex_;

  std::atomic<size_t> next_victim_;
  std::atomic<size_t> active_tasks_{ 0 };
  std::atomic<size_t> outstanding_tasks_{ 0 };
  std::atomic<size_t> completed_tasks_{ 0 };
  std::atomic<size_t> stolen_tasks_{ 0 };
  std::atomic<uint64_t> total_task_time_{ 0 };

  std::mutex trace_mutex_;
  task_start_callback_storage on_task_start_;
  task_end_callback_storage on_task_end_;

  std::chrono::steady_clock::time_point start_time_;
  inline static thread_local work_stealing_pool_backend* current_pool = nullptr;

  auto
  finish_shutdown(shutdown_policy_backend policy) -> bool
  {
    size_t dropped_tasks = 0;
    std::queue<queued_task> discarded_overflow;
    {
      std::unique_lock<std::shared_mutex> submission_lock(submission_mutex_);
      submissions_quiesced_.store(true, std::memory_order_release);
      std::lock_guard<std::mutex> lock(overflow_mutex_);
      if (policy == shutdown_policy_backend::drop_pending)
        {
          dropped_tasks += overflow_tasks_.size();
          overflow_tasks_.swap(discarded_overflow);
        }
    }

    if (dropped_tasks != 0)
      {
        std::lock_guard<std::mutex> lock(completion_mutex_);
        outstanding_tasks_.fetch_sub(dropped_tasks, std::memory_order_acq_rel);
      }

    if (dropped_tasks != 0)
      completion_condition_.notify_all();

    if (dropped_tasks != 0)
      {
        std::queue<queued_task> empty;
        discarded_overflow.swap(empty);
      }

    if (policy == shutdown_policy_backend::drop_pending)
      {
        for (auto& queue : worker_queues_)
          {
            queued_task discarded;
            while (queue->steal(discarded))
              {
                ++dropped_tasks;
                {
                  std::lock_guard<std::mutex> lock(completion_mutex_);
                  outstanding_tasks_.fetch_sub(1, std::memory_order_acq_rel);
                }
                completion_condition_.notify_all();
                discarded = queued_task{};
              }
          }
      }

    wakeup_condition_.notify_all();

    for (auto& worker : workers_)
      if (worker.joinable())
        worker.join();

    workers_.clear();
    return dropped_tasks == 0;
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  void
  worker_function(size_t worker_id)
  {
    detail::worker_context_guard<work_stealing_pool_backend> worker_context(current_pool, this);
    std::optional<registration_guard_backend> reg_guard;
    if (register_workers_)
      reg_guard.emplace("hp_worker_" + std::to_string(worker_id), "threadschedule.pool");

    thread_local std::mt19937 gen = []()
      {
        std::random_device device;
        return std::mt19937(device());
      }();

    queued_task task;
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
            auto const tid = std::this_thread::get_id();

            try
              {
                task_start_callback_storage on_task_start;
                {
                  std::lock_guard<std::mutex> tl(trace_mutex_);
                  on_task_start = on_task_start_;
                }
                if (on_task_start)
                  on_task_start(start_time, tid);
              }
            catch (...)
              {
              }

            // For submit() tasks the callable is a packaged_task which
            // catches exceptions internally and stores them in the
            // std::future shared state - those never reach this catch.
            // For post() tasks (fire-and-forget) the catch prevents an
            // unhandled exception from terminating the worker thread.
            try
              {
                task();
              }
            catch (...)
              {
              }
            task = queued_task{};
            auto const end_time = std::chrono::steady_clock::now();

            auto const task_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            total_task_time_.fetch_add(task_duration.count(), std::memory_order_relaxed);

            try
              {
                task_end_callback_storage on_task_end;
                {
                  std::lock_guard<std::mutex> tl(trace_mutex_);
                  on_task_end = on_task_end_;
                }
                if (on_task_end)
                  on_task_end(end_time, tid, task_duration);
              }
            catch (...)
              {
              }

            active_tasks_.fetch_sub(1, std::memory_order_relaxed);
            {
              std::lock_guard<std::mutex> lock(completion_mutex_);
              outstanding_tasks_.fetch_sub(1, std::memory_order_acq_rel);
            }
            completed_tasks_.fetch_add(1, std::memory_order_relaxed);

            completion_condition_.notify_all();
            wakeup_condition_.notify_all();
          }
        else
          {
            if (stop_.load(std::memory_order_acquire) && submissions_quiesced_.load(std::memory_order_acquire)
                && outstanding_tasks_.load(std::memory_order_acquire) == 0)
              {
                break;
              }

            std::unique_lock<std::mutex> lock(wakeup_mutex_);
            wakeup_condition_.wait_for(lock, std::chrono::microseconds(100));
          }
      }
  }
};

} // namespace threadschedule::detail
