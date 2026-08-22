#pragma once

/**
 * @file detail/pool/thread_pool_backend_base.hpp
 * @brief Shared-queue pool implementation and public detail aliases.
 *
 * Self-contained internal implementation header.
 */

#include "../callable/bind.hpp"
#include "../callable/move_only_function.hpp"
#include "callbacks.hpp"
#include "deadline.hpp"
#include "indefinite_wait.hpp"
#include "polling_wait.hpp"
#include "shutdown_policy_backend.hpp"
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
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace threadschedule::detail
{

// ---------------------------------------------------------------------------
// thread_pool_backend_base
// ---------------------------------------------------------------------------

/**
 * @brief Single-queue thread pool parameterized by its idle-wait strategy.
 *
 * All tasks share one std::queue protected by a single mutex. The
 * @p WaitPolicy template parameter controls how workers wait for new
 * work:
 * - @ref indefinite_wait - blocks on condition_variable::wait() (zero CPU
 *   while idle, instant wake). Instantiated as @c thread_pool_backend.
 * - @ref polling_wait - polls with condition_variable::wait_for(10 ms).
 *   Slightly higher idle CPU but lower worst-case latency under bursty
 *   loads. Instantiated as @c polling_pool_backend.
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
class thread_pool_backend_base
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
    double tasks_per_second;
    std::chrono::microseconds avg_task_time;
  };

  explicit thread_pool_backend_base(size_t num_threads = default_worker_count(), bool register_workers = false)
      : num_threads_(checked_worker_count(num_threads)), register_workers_(register_workers), stop_(false),
        start_time_(std::chrono::steady_clock::now())
  {
    workers_.reserve(num_threads_);

    try
      {
        for (size_t i = 0; i < num_threads_; ++i)
          workers_.emplace_back(&thread_pool_backend_base::worker_function, this, i);
        startup_.wait(num_threads_);
      }
    catch (...)
      {
        stop_.store(true, std::memory_order_release);
        condition_.notify_all();
        for (auto& worker : workers_)
          if (worker.joinable())
            worker.join();
        throw;
      }
  }

  thread_pool_backend_base(thread_pool_backend_base const&) = delete;
  auto operator=(thread_pool_backend_base const&) -> thread_pool_backend_base& = delete;

  ~thread_pool_backend_base()
  {
    shutdown(shutdown_policy_backend::drain);
  }

  /// @name Task submission
  /// @{

  /**
   * @brief Submit a task without throwing on shutdown.
   * @return @c expected<std::future<R>, std::error_code>.
   * @see submit() for the throwing variant.
   */
  template <typename F, typename... Args>
  auto
  try_submit(F&& f, Args&&... args) -> expected<std::future<bind_result_t<F, Args...>>, std::error_code>
  {
    using return_type = bind_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        detail::bind_args(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> result = task->get_future();

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (stop_)
        return unexpected(std::make_error_code(std::errc::operation_canceled));
      tasks_.emplace([task]() { (*task)(); });
    }

    condition_.notify_one();
    return result;
  }

  /**
   * @brief Submit a task, throwing on shutdown.
   * @throws std::runtime_error If the pool is shutting down.
   */
  template <typename F, typename... Args>
  auto
  submit(F&& f, Args&&... args) -> std::future<bind_result_t<F, Args...>>
  {
    auto result = try_submit(std::forward<F>(f), std::forward<Args>(args)...);
    if (!result.has_value())
      throw std::runtime_error("Pool is shutting down");
    return std::move(result.value());
  }

  /**
   * @brief Fire-and-forget task submission (throwing variant).
   *
   * Bypasses @c std::packaged_task / @c std::future for lower overhead.
   *
   * @throws std::runtime_error If the pool is shutting down.
   * @see try_post()
   */
  template <typename F, typename... Args>
  void
  post(F&& f, Args&&... args)
  {
    auto r = try_post(std::forward<F>(f), std::forward<Args>(args)...);
    if (!r.has_value())
      throw std::runtime_error("Pool is shutting down");
  }

  /**
   * @brief Fire-and-forget task submission (non-throwing variant).
   * @return @c expected<void, std::error_code> --
   *         @c std::errc::operation_canceled on shutdown.
   */
  template <typename F, typename... Args>
  auto
  try_post(F&& f, Args&&... args) -> expected<void, std::error_code>
  {
    queued_task task(detail::bind_args(std::forward<F>(f), std::forward<Args>(args)...));
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (stop_)
        return unexpected(std::make_error_code(std::errc::operation_canceled));
      tasks_.push(std::move(task));
    }
    condition_.notify_one();
    return {};
  }

  /**
   * @brief Submit a range of @c void() callables in one go (non-throwing).
   *
   * All tasks are enqueued under a single lock acquisition.
   * @tparam Iterator Input iterator whose value is callable as @c void().
   */
  template <typename Iterator>
  auto
  try_submit_batch(Iterator begin, Iterator end) -> expected<std::vector<std::future<void>>, std::error_code>
  {
    std::vector<std::future<void>> futures;
    size_t const batch_size_hint = detail::multipass_range_size(begin, end);
    futures.reserve(batch_size_hint);
    std::vector<std::shared_ptr<std::packaged_task<void()>>> prepared;
    prepared.reserve(batch_size_hint);

    for (auto it = begin; it != end; ++it)
      {
        auto task = std::make_shared<std::packaged_task<void()>>(*it);
        futures.push_back(task->get_future());
        prepared.push_back(std::move(task));
      }

    bool enqueued = false;
    try
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_)
          return unexpected(std::make_error_code(std::errc::operation_canceled));

        for (auto const& task : prepared)
          {
            tasks_.emplace([task]() { (*task)(); });
            enqueued = true;
          }
      }
    catch (...)
      {
        if (enqueued)
          condition_.notify_all();
        throw;
      }

    condition_.notify_all();
    return futures;
  }

  /// @brief Submit a batch of tasks (throwing). @see try_submit_batch()
  template <typename Iterator>
  auto
  submit_batch(Iterator begin, Iterator end) -> std::vector<std::future<void>>
  {
    auto result = try_submit_batch(begin, end);
    if (!result.has_value())
      throw std::runtime_error("Pool is shutting down");
    return std::move(result.value());
  }

  /// @brief Apply @p func to @c [begin, end) in parallel (chunked).
  /// @tparam Iterator Forward iterator; task chunks retain iterator pairs.
  template <typename Iterator, typename F>
  void
  parallel_for_each(Iterator begin, Iterator end, F&& func)
  {
    if (is_current_worker())
      detail::throw_worker_deadlock();
    detail::parallel_for_each_chunked(*this, begin, end, std::forward<F>(func), num_threads_);
  }

  /// @}

  /// @name Observers
  /// @{

  /// @brief Number of worker threads.
  [[nodiscard]] auto
  size() const noexcept -> size_t
  {
    return num_threads_;
  }

  /// @brief Number of tasks waiting in the queue.
  [[nodiscard]] auto
  pending_tasks() const -> size_t
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
  }

  /// @}

  /// @name Thread configuration
  /// @{

  /**
   * @brief Name, schedule and prioritize all worker threads.
   * @see work_stealing_pool_backend::configure_threads
   */
  auto
  configure_threads(std::string const& name_prefix, native_scheduling_policy policy = native_scheduling_policy::other,
                    native_thread_priority priority = native_thread_priority::normal())
      -> expected<void, std::error_code>
  {
    return detail::configure_worker_threads(workers_, name_prefix, policy, priority,
                                            register_workers_ ? &runtime_registry() : nullptr);
  }

  auto
  configure_threads(native_thread_config const& config) -> expected<void, std::error_code>
  {
    return detail::configure_worker_threads(workers_, config, register_workers_ ? &runtime_registry() : nullptr);
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

  /// @name Synchronisation & lifecycle
  /// @{

  /// @brief Block until all pending and active tasks have completed.
  void
  wait_for_tasks()
  {
    if (is_current_worker())
      detail::throw_worker_deadlock();
    std::unique_lock<std::mutex> lock(queue_mutex_);
    task_finished_condition_.wait(lock, [this]
                                    { return tasks_.empty() && active_tasks_.load(std::memory_order_acquire) == 0; });
  }

  [[nodiscard]] auto
  is_current_worker() const noexcept -> bool
  {
    return current_pool == this;
  }

  /**
   * @brief Shut the pool down.
   * @param policy @c drain (default) finishes all queued tasks;
   *               @c drop_pending discards queued tasks.
   */
  void
  shutdown(shutdown_policy_backend policy = shutdown_policy_backend::drain)
  {
    if (is_current_worker())
      detail::throw_worker_deadlock();
    std::lock_guard<std::recursive_timed_mutex> shutdown_lock(shutdown_mutex_);
    std::queue<queued_task> discarded;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (stop_)
        return;
      stop_ = true;
      if (policy == shutdown_policy_backend::drop_pending)
        tasks_.swap(discarded);
    }
    shutdown_completed_all_ = discarded.empty();

    condition_.notify_all();
    task_finished_condition_.notify_all();
    {
      std::queue<queued_task> empty;
      discarded.swap(empty);
    }

    for (auto& worker : workers_)
      {
        if (worker.joinable())
          worker.join();
      }

    workers_.clear();
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
    auto const deadline = shutdown_deadline_after(timeout);
    std::unique_lock<std::recursive_timed_mutex> shutdown_lock(shutdown_mutex_, std::defer_lock);
    if (deadline == std::chrono::steady_clock::time_point::max())
      shutdown_lock.lock();
    else if (!shutdown_lock.try_lock_until(deadline))
      return false;

    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (stop_)
      return shutdown_completed_all_ && shutdown_completed_at_ <= deadline;
    stop_ = true;
    condition_.notify_all();
    bool const drained = task_finished_condition_.wait_until(
        lock, deadline, [this] { return tasks_.empty() && active_tasks_.load(std::memory_order_acquire) == 0; });
    std::queue<queued_task> discarded;
    if (!drained)
      tasks_.swap(discarded);
    shutdown_completed_all_ = discarded.empty();
    lock.unlock();

    condition_.notify_all();
    task_finished_condition_.notify_all();
    {
      std::queue<queued_task> empty;
      discarded.swap(empty);
    }
    for (auto& worker : workers_)
      if (worker.joinable())
        worker.join();
    workers_.clear();
    shutdown_completed_at_ = std::chrono::steady_clock::now();
    return drained;
  }

  /// @}

  /// @name Observers
  /// @{

  /// @brief Collect approximate performance counters.
  [[nodiscard]] auto
  get_statistics() const -> statistics
  {
    auto const now = std::chrono::steady_clock::now();
    auto const elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

    std::lock_guard<std::mutex> lock(queue_mutex_);
    statistics stats;
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
  detail::worker_startup_latch startup_;
  std::vector<detail::thread_backend> workers_;
  std::queue<queued_task> tasks_;

  mutable std::mutex queue_mutex_;
  std::condition_variable condition_;
  std::condition_variable task_finished_condition_;
  std::recursive_timed_mutex shutdown_mutex_;
  std::atomic<bool> stop_;
  bool shutdown_completed_all_{ true };
  std::chrono::steady_clock::time_point shutdown_completed_at_{};
  std::atomic<size_t> active_tasks_{ 0 };
  std::atomic<size_t> completed_tasks_{ 0 };
  std::atomic<uint64_t> total_task_time_{ 0 };

  std::mutex trace_mutex_;
  task_start_callback_storage on_task_start_;
  task_end_callback_storage on_task_end_;

  std::chrono::steady_clock::time_point start_time_;
  inline static thread_local thread_pool_backend_base* current_pool = nullptr;

  void
  worker_function(size_t worker_id)
  {
    detail::worker_context_guard<thread_pool_backend_base> worker_context(current_pool, this);
    std::optional<registration_guard_backend> reg_guard;
    try
      {
        if (register_workers_)
          reg_guard.emplace("pool_worker_" + std::to_string(worker_id), "threadschedule.pool");
        startup_.arrive();
      }
    catch (...)
      {
        startup_.arrive(std::current_exception());
        return;
      }

    while (true)
      {
        queued_task task;
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

            // See work_stealing_pool_backend::worker_function for rationale.
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

            {
              std::lock_guard<std::mutex> lock(queue_mutex_);
              active_tasks_.fetch_sub(1, std::memory_order_relaxed);
            }
            completed_tasks_.fetch_add(1, std::memory_order_relaxed);

            task_finished_condition_.notify_all();
          }
      }
  }
};

/**
 * @typedef thread_pool_backend
 * @brief General-purpose thread pool with indefinite blocking wait.
 *
 * Workers block on condition_variable::wait() when idle - zero CPU
 * consumption, instant wake-up on task submission. Suitable for most
 * workloads.
 *
 * @see thread_pool_backend_base, indefinite_wait
 */
using thread_pool_backend = thread_pool_backend_base<indefinite_wait>;

/**
 * @typedef polling_pool_backend
 * @brief Thread pool with 10 ms polling wait for lower wake-up latency.
 *
 * Workers poll with condition_variable::wait_for(10 ms), trading a small
 * amount of idle CPU for more consistent latency under bursty workloads.
 *
 * @see thread_pool_backend_base, polling_wait
 */
using polling_pool_backend = thread_pool_backend_base<polling_wait<>>;

} // namespace threadschedule::detail
