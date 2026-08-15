#pragma once

/** @file detail/scheduled/scheduled_pool_backend_base.hpp
 *  @brief Deadline queue, dispatch, periodic rescheduling, and scheduler lifecycle.
 */

#include "../pool/backend.hpp"
#include "../pool/worker_count.hpp"
#include "scheduled_task_backend.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace threadschedule::detail
{

/**
 * @brief Thread pool augmented with delayed and periodic task scheduling.
 *
 * Non-copyable, non-movable. Combines a dedicated scheduler thread with
 * an underlying PoolType (default: @c thread_pool_backend) that does the
 * actual work.
 *
 * @par How task execution works
 * The pool owns a single scheduler thread that runs an internal loop
 * (scheduler_loop). Scheduled tasks are stored in a std::multimap sorted
 * by their next_run time point. The scheduler thread sleeps (via
 * condition_variable::wait / wait_until) until the earliest task is due.
 * When a task becomes due, the scheduler thread:
 *   1. Removes it from the multimap.
 *   2. Checks if the task has been cancelled (via the atomic flag). If
 *      cancelled, the task is discarded.
 *   3. Posts the task to the underlying PoolType via pool_.post().
 *      From this point on, the task follows the execution rules of the
 *      underlying pool (see @c thread_pool_backend, @c polling_pool_backend,
 *      @ref work_stealing_pool_backend, or @c lightweight_pool_backend
 * documentation).
 *   4. For periodic tasks, the scheduler re-inserts the next fixed-rate
 *      deadline. A task never overlaps with itself; deadlines missed while
 *      an occurrence is running are skipped rather than queued as backlog.
 *
 * @par Execution guarantees
 * - Every successfully scheduled task (schedule_after/schedule_at/
 *   schedule_periodic returned a handle) is guaranteed to eventually
 *   execute, unless it is cancelled or shutdown() is called before it
 *   becomes due.
 * - Tasks are stored in a std::multimap keyed by time point. When
 *   multiple tasks share the same due time, they are dispatched in
 *   insertion order (guaranteed by std::multimap since C++11).
 * - Tasks that are already due and submitted to the underlying pool
 *   before shutdown() will still execute (the pool drains its queue).
 * - Tasks that are not yet due at the time of shutdown() will NOT
 *   execute. The scheduler thread exits immediately on shutdown, so
 *   future-scheduled tasks are lost.
 * - Cancellation is cooperative: calling handle.cancel() sets an atomic
 *   flag. The scheduler checks this flag before posting the task to
 *   the pool. Additionally, the pool-side wrapper checks the flag again
 *   right before calling the task. However, a task that is already
 *   running will NOT be interrupted by cancel().
 * - Periodic tasks use fixed-rate deadlines. If an occurrence takes longer
 *   than its interval, missed occurrences are skipped. The same periodic
 *   callable is never invoked concurrently, and independent pool work does
 *   not wait behind a backlog of overdue occurrences.
 * - There is no returned std::future for scheduled tasks. If you need
 *   to observe the result, use the underlying pool directly via
 *   thread_pool().post() or thread_pool().submit().
 *
 * @par Thread safety
 * All schedule_* methods are thread-safe (protected by an internal
 * mutex). cancel() on a scheduled_task_backend is also thread-safe (atomic).
 * shutdown() is internally guarded and safe to call more than once.
 *
 * @par Lifetime
 * The destructor calls shutdown(), which joins the scheduler thread and
 * then shuts down the underlying pool. Can block if the pool still has
 * running tasks.
 *
 * @par Copyability / movability
 * Not copyable, not movable.
 *
 * @tparam PoolType Thread pool used for task execution
 *         (default: thread_pool_backend).
 *
 * @see scheduled_pool_backend, scheduled_work_stealing_pool_backend,
 *      scheduled_polling_pool_backend, scheduled_lightweight_pool_backend
 * (convenience aliases)
 */
template <typename PoolType = thread_pool_backend>
class scheduled_pool_backend_base
{
public:
  using task_type = std::function<void()>;
  using one_shot_task_type = detail::move_only_function<void()>;
  using periodic_task_type = detail::move_only_function<void()>;
  using time_point = std::chrono::steady_clock::time_point;
  using duration = std::chrono::steady_clock::duration;

  struct periodic_task_state
  {
    explicit periodic_task_state(periodic_task_type task_value) : task(std::move(task_value)) {}

    periodic_task_type task;
    std::atomic<bool> running{ false };
  };

  class scheduled_task_info
  {
  public:
    enum class kind : std::uint8_t
    {
      one_shot,
      periodic
    };

    [[nodiscard]] static auto
    one_shot(std::uint64_t id, time_point run_time, one_shot_task_type task,
             std::shared_ptr<detail::scheduled_cancellation_state> cancellation) -> scheduled_task_info
    {
      return scheduled_task_info(id, run_time, std::move(task), std::move(cancellation));
    }

    [[nodiscard]] static auto
    periodic(std::uint64_t id, time_point run_time, duration interval, periodic_task_type task,
             std::shared_ptr<detail::scheduled_cancellation_state> cancellation) -> scheduled_task_info
    {
      if (interval <= duration::zero())
        throw std::invalid_argument("scheduled_pool periodic interval must be positive");
      return scheduled_task_info(id, run_time, interval, std::move(task), std::move(cancellation));
    }

    [[nodiscard]] auto
    type() const noexcept -> kind
    {
      return kind_;
    }
    [[nodiscard]] auto
    cancellation() const noexcept -> std::shared_ptr<detail::scheduled_cancellation_state> const&
    {
      return cancellation_;
    }
    [[nodiscard]] auto
    periodic_state() const noexcept -> std::shared_ptr<periodic_task_state> const&
    {
      return periodic_task_;
    }
    [[nodiscard]] auto
    take_one_shot() noexcept -> one_shot_task_type
    {
      return std::move(one_shot_task_);
    }
    [[nodiscard]] auto
    next_run() const noexcept -> time_point
    {
      return next_run_;
    }

    void
    advance(time_point now)
    {
      auto const interval = *interval_;
      next_run_ += interval;
      if (next_run_ <= now)
        {
          auto const missed = (now - next_run_) / interval + 1;
          next_run_ += interval * missed;
        }
    }

  private:
    scheduled_task_info(std::uint64_t id, time_point run_time, one_shot_task_type task,
                        std::shared_ptr<detail::scheduled_cancellation_state> cancellation)
        : id_(id), next_run_(run_time), one_shot_task_(std::move(task)), cancellation_(std::move(cancellation)),
          kind_(kind::one_shot)
    {
    }

    scheduled_task_info(std::uint64_t id, time_point run_time, duration interval, periodic_task_type task,
                        std::shared_ptr<detail::scheduled_cancellation_state> cancellation)
        : id_(id), next_run_(run_time), interval_(interval),
          periodic_task_(std::make_shared<periodic_task_state>(std::move(task))),
          cancellation_(std::move(cancellation)), kind_(kind::periodic)
    {
    }

    std::uint64_t id_;
    time_point next_run_;
    std::optional<duration> interval_;
    one_shot_task_type one_shot_task_;
    std::shared_ptr<periodic_task_state> periodic_task_;
    std::shared_ptr<detail::scheduled_cancellation_state> cancellation_;
    kind kind_;
  };

  /**
   * @brief Create a scheduled thread pool
   * @param worker_threads Number of worker threads for executing tasks
   * (default: hardware concurrency)
   */
  explicit scheduled_pool_backend_base(size_t worker_threads = default_worker_count())
      : pool_(worker_threads), stop_(false), next_task_id_(1)
  {
    start_scheduler();
  }

  template <typename T = PoolType, std::enable_if_t<std::is_constructible_v<T, size_t, bool>, int> = 0>
  scheduled_pool_backend_base(size_t worker_threads, bool register_workers)
      : pool_(worker_threads, register_workers), stop_(false), next_task_id_(1)
  {
    start_scheduler();
  }

  scheduled_pool_backend_base(scheduled_pool_backend_base const&) = delete;
  auto operator=(scheduled_pool_backend_base const&) -> scheduled_pool_backend_base& = delete;

  ~scheduled_pool_backend_base()
  {
    shutdown();
  }

  /**
   * @brief Schedule a task to run after a delay
   * @param delay duration to wait before executing the task
   * @param task Function to execute
   * @return Handle to cancel the task
   */
  auto
  schedule_after(duration delay, task_type task) -> scheduled_task_backend
  {
    auto run_time = std::chrono::steady_clock::now() + delay;
    return insert_one_shot_task(run_time, detail::make_move_only_function<void()>(std::move(task)));
  }

  template <typename F, std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<F>, task_type>, int> = 0>
  auto
  schedule_after(duration delay, F&& task) -> scheduled_task_backend
  {
    auto run_time = std::chrono::steady_clock::now() + delay;
    return insert_one_shot_task(run_time, detail::make_move_only_function<void()>(std::forward<F>(task)));
  }

  /**
   * @brief Schedule a task to run at a specific time point
   * @param time_point When to execute the task
   * @param task Function to execute
   * @return Handle to cancel the task
   */
  auto
  schedule_at(time_point time_point, task_type task) -> scheduled_task_backend
  {
    return insert_one_shot_task(time_point, detail::make_move_only_function<void()>(std::move(task)));
  }

  template <typename F, std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<F>, task_type>, int> = 0>
  auto
  schedule_at(time_point time_point, F&& task) -> scheduled_task_backend
  {
    return insert_one_shot_task(time_point, detail::make_move_only_function<void()>(std::forward<F>(task)));
  }

  /**
   * @brief Schedule a task to run periodically at fixed intervals
   * @param interval duration between executions
   * @param task Function to execute repeatedly
   * @return Handle to cancel the periodic task
   *
   * The task runs immediately and then repeats every interval.
   * Use schedule_periodic_after() if you want to delay the first execution.
   */
  auto
  schedule_periodic(duration interval, task_type task) -> scheduled_task_backend
  {
    return schedule_periodic_after(duration::zero(), interval, std::move(task));
  }

  template <typename F, std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<F>, task_type>, int> = 0>
  auto
  schedule_periodic(duration interval, F&& task) -> scheduled_task_backend
  {
    return schedule_periodic_after(duration::zero(), interval, std::forward<F>(task));
  }

  /**
   * @brief Schedule a task to run periodically after an initial delay
   * @param initial_delay duration to wait before first execution
   * @param interval duration between subsequent executions
   * @param task Function to execute repeatedly
   * @return Handle to cancel the periodic task
   */
  auto
  schedule_periodic_after(duration initial_delay, duration interval, task_type task) -> scheduled_task_backend
  {
    if (interval <= duration::zero())
      throw std::invalid_argument("scheduled_pool periodic interval must be positive");
    auto const run_time = std::chrono::steady_clock::now() + initial_delay;
    return insert_periodic_task(run_time, interval, detail::make_move_only_function<void()>(std::move(task)));
  }

  template <typename F, std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<F>, task_type>, int> = 0>
  auto
  schedule_periodic_after(duration initial_delay, duration interval, F&& task) -> scheduled_task_backend
  {
    if (interval <= duration::zero())
      throw std::invalid_argument("scheduled_pool periodic interval must be positive");
    auto const run_time = std::chrono::steady_clock::now() + initial_delay;
    return insert_periodic_task(run_time, interval, detail::make_move_only_function<void()>(std::forward<F>(task)));
  }

  /**
   * @brief Cancel a scheduled task by handle
   * @param handle Handle returned from schedule_* functions
   *
   * Note: Can also call handle.cancel() directly
   */
  static void
  cancel(scheduled_task_backend& handle)
  {
    handle.cancel();
  }

  /**
   * @brief Get number of scheduled tasks (including periodic)
   */
  [[nodiscard]] auto
  scheduled_count() const -> size_t
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return scheduled_tasks_.size();
  }

  /**
   * @brief Get the underlying thread pool for direct task submission
   */
  [[nodiscard]] auto
  thread_pool() -> PoolType&
  {
    return pool_;
  }

  /**
   * @brief Shutdown the scheduler and wait for completion
   */
  void
  shutdown(shutdown_policy_backend policy = shutdown_policy_backend::drain)
  {
    if (pool_.is_current_worker() || (scheduler_thread_.joinable() && thread_info::get_thread_id() == scheduler_tid_))
      detail::throw_worker_deadlock();
    std::lock_guard<std::recursive_mutex> shutdown_lock(shutdown_mutex_);
    std::multimap<time_point, scheduled_task_info> discarded;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_)
        return;
      stop_ = true;
      for (auto const& task : scheduled_tasks_)
        task.second.cancellation()->pool_stopped.store(true, std::memory_order_release);
      scheduled_tasks_.swap(discarded);
    }

    discarded.clear();

    condition_.notify_one();

    if (scheduler_thread_.joinable())
      {
        scheduler_thread_.join();
        scheduler_tid_ = native_thread_id{};
      }

    pool_.shutdown(policy);
  }

  /**
   * @brief Configure worker threads
   *
   * Returns expected<void, std::error_code> from the underlying pool.
   */
  auto
  configure_threads(std::string const& name_prefix, native_scheduling_policy policy = native_scheduling_policy::other,
                    native_thread_priority priority = native_thread_priority::normal())
  {
    return pool_.configure_threads(name_prefix, policy, priority);
  }

  auto
  configure_threads(native_thread_config const& config) -> expected<void, std::error_code>
  {
    return pool_.configure_threads(config);
  }

  [[nodiscard]] auto
  scheduler_thread_info() const -> std::optional<thread_info>
  {
    if (!scheduler_thread_.joinable() || scheduler_tid_ == native_thread_id{})
      return std::nullopt;
    return thread_info(scheduler_tid_);
  }

  auto
  configure_scheduler_thread(std::string const& name, native_scheduling_policy policy = native_scheduling_policy::other,
                             native_thread_priority priority = native_thread_priority::normal())
      -> expected<void, std::error_code>
  {
    auto info = scheduler_thread_info();
    if (!info.has_value())
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return detail::configure_thread(info.value(), name, policy, priority);
  }

  auto
  configure_scheduler_thread(native_thread_config const& config) -> expected<void, std::error_code>
  {
    auto info = scheduler_thread_info();
    if (!info.has_value())
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return info->configure(config);
  }

private:
  PoolType pool_;
  detail::thread_backend scheduler_thread_;
  native_thread_id scheduler_tid_{};

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::atomic<bool> stop_;
  std::recursive_mutex shutdown_mutex_;

  std::multimap<time_point, scheduled_task_info> scheduled_tasks_;
  std::atomic<uint64_t> next_task_id_;

  void
  start_scheduler()
  {
    std::promise<native_thread_id> scheduler_started;
    auto scheduler_ready = scheduler_started.get_future();

    scheduler_thread_ = detail::thread_backend(
        [this, started = std::move(scheduler_started)]() mutable
          {
            started.set_value(thread_info::get_thread_id());
            scheduler_loop();
          });

    scheduler_tid_ = scheduler_ready.get();
    (void)thread_info(scheduler_tid_).set_name("ts_sched_pool");
  }

  auto
  insert_one_shot_task(time_point run_time, one_shot_task_type task) -> scheduled_task_backend
  {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t const task_id = next_task_id_++;
    scheduled_task_backend handle(task_id);

    if (stop_)
      {
        handle.cancel();
        return handle;
      }

    scheduled_tasks_.insert(
        { run_time, scheduled_task_info::one_shot(task_id, run_time, std::move(task), handle.get_cancellation()) });
    condition_.notify_one();

    return handle;
  }

  auto
  insert_periodic_task(time_point run_time, duration interval, periodic_task_type task) -> scheduled_task_backend
  {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t const task_id = next_task_id_++;
    scheduled_task_backend handle(task_id);

    if (stop_)
      {
        handle.cancel();
        return handle;
      }

    scheduled_tasks_.insert({ run_time, scheduled_task_info::periodic(task_id, run_time, interval, std::move(task),
                                                                      handle.get_cancellation()) });
    condition_.notify_one();

    return handle;
  }

  void
  scheduler_loop()
  {
    while (true)
      {
        std::unique_lock<std::mutex> lock(mutex_);

        if (stop_)
          return;

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
            // wait_until releases the mutex while retaining a reference to
            // its deadline. Shutdown may clear scheduled_tasks_ during that
            // wait, so never pass a map key by reference here.
            auto const next_run = it->first;
            condition_.wait_until(lock, next_run);

            if (stop_)
              return;

            continue;
          }

        // Extract the task info
        scheduled_task_info info = std::move(it->second);
        scheduled_tasks_.erase(it);
        lock.unlock();

        // Check if cancelled
        if (info.cancellation()->user_cancelled.load(std::memory_order_acquire)
            || info.cancellation()->pool_stopped.load(std::memory_order_acquire))
          {
            continue;
          }

        // Schedule for execution in the thread pool
        try
          {
            auto cancellation = info.cancellation();
            if (info.type() == scheduled_task_info::kind::periodic)
              {
                auto periodic_task = info.periodic_state();
                bool const already_running = periodic_task->running.exchange(true, std::memory_order_acq_rel);
                if (!already_running)
                  {
                    try
                      {
                        pool_.post(
                            [periodic_task, cancellation]()
                              {
                                try
                                  {
                                    if (!cancellation->user_cancelled.load(std::memory_order_acquire))
                                      periodic_task->task();
                                  }
                                catch (...)
                                  {
                                    periodic_task->running.store(false, std::memory_order_release);
                                    throw;
                                  }
                                periodic_task->running.store(false, std::memory_order_release);
                              });
                      }
                    catch (...)
                      {
                        periodic_task->running.store(false, std::memory_order_release);
                        throw;
                      }
                  }

                info.advance(std::chrono::steady_clock::now());
                std::lock_guard<std::mutex> schedule_lock(mutex_);
                if (!stop_ && !cancellation->user_cancelled.load(std::memory_order_acquire))
                  scheduled_tasks_.insert({ info.next_run(), std::move(info) });
              }
            else
              {
                auto one_shot_task = info.take_one_shot();
                pool_.post(
                    [task = std::move(one_shot_task), cancellation]() mutable
                      {
                        if (!cancellation->user_cancelled.load(std::memory_order_acquire))
                          {
                            task();
                          }
                      });
              }
          }
        catch (...)
          {
            info.cancellation()->pool_stopped.store(true, std::memory_order_release);
            // Thread pool might be shutting down
          }
      }
  }
};

/** @brief @ref scheduled_pool_backend_base using the default @c
 * thread_pool_backend backend.
 */
using scheduled_pool_backend = scheduled_pool_backend_base<thread_pool_backend>;
/** @brief @ref scheduled_pool_backend_base using @ref
 * work_stealing_pool_backend as backend.
 */
using scheduled_work_stealing_pool_backend = scheduled_pool_backend_base<work_stealing_pool_backend>;
/** @brief @ref scheduled_pool_backend_base using @c polling_pool_backend as
 * backend. */
using scheduled_polling_pool_backend = scheduled_pool_backend_base<polling_pool_backend>;
/** @brief @ref scheduled_pool_backend_base using @c lightweight_pool_backend
 * as backend (minimal overhead). */
using scheduled_lightweight_pool_backend = scheduled_pool_backend_base<lightweight_pool_backend>;

} // namespace threadschedule::detail
