#pragma once

/**
 * @file detail/pool/lightweight_pool_backend_base.hpp
 * @brief Low-overhead fire-and-forget pool implementation.
 *
 * Internal implementation fragment included by backend.hpp inside
 * threadschedule::detail.
 */

// ---------------------------------------------------------------------------
// lightweight_pool_backend_base
// ---------------------------------------------------------------------------

/**
 * @brief Ultra-lightweight fire-and-forget thread pool.
 *
 * Designed for maximum throughput on tasks whose return value is not needed.
 * Typical measured throughput is **3x** higher than @c submit() on e.g.
 * @ref work_stealing_pool_backend on the
 * same hardware, because @c lightweight_pool_backend_base avoids the overhead
 * of
 * @c std::packaged_task, @c std::future, and @c std::shared_ptr entirely.
 *
 * @par Internal architecture
 * @code
 *   Producer(s)          Single Queue            Worker Threads
 *  +---------+      +------------------+      +----------------+
 *  | post()  | ---> | sbo_callable<64>  | ---> | detail::thread_backend  |
 *  | post()  | ---> | sbo_callable<64>  | ---> | detail::thread_backend  |
 *  +---------+      +------------------+      +----------------+
 *                     mutex + cond_var
 * @endcode
 *
 * - **Queue**: Single @c std::queue of @ref detail::sbo_callable objects
 *   protected by one mutex + condition_variable.
 * - **Workers**: @ref detail::thread_backend instances so that thread naming,
 * CPU affinity, and scheduling policy can be configured after construction.
 * - **SBO**: Callables up to @c TaskSize - 8 bytes are stored inline
 *   (no heap allocation). Larger callables fall back to the heap.
 *
 * @par What is @e not included (by design)
 * - No @c std::future / @c std::packaged_task (use @c submit() on other
 *   pools if you need return values).
 * - No statistics counters (@ref work_stealing_pool_backend::get_statistics).
 * - No tracing hooks (@ref work_stealing_pool_backend::set_on_task_start).
 * - No work stealing (single shared queue).
 * - No @c thread_registry_backend auto-registration.
 *
 * @par Execution guarantees
 * - Every successfully posted task is guaranteed to execute (unless
 *   @c shutdown(shutdown_policy_backend::drop_pending) is called).
 * - Tasks are dequeued in FIFO order. Because multiple workers pop
 *   concurrently, the @e completion order is non-deterministic.
 * - Exceptions thrown by tasks are silently caught; the worker continues.
 *
 * @par Thread safety
 * @c post(), @c try_post(), @c post_batch(), and @c try_post_batch() may
 * be called from any number of threads concurrently. @c shutdown() is
 * internally guarded and safe to call more than once.
 *
 * @par Lifetime
 * The destructor calls @c shutdown(shutdown_policy_backend::drain) and joins
 * all workers. It blocks until every queued task has been executed.
 *
 * @par Choosing @c TaskSize
 * The default of 64 bytes (one x86 cache line) works well for lambdas
 * capturing up to ~7 pointers. If your tasks capture more state, increase
 * @c TaskSize to avoid the heap fallback:
 * @code
 *   lightweight_pool_backend_base<128> pool(4);   // 120 bytes of inline
 * storage
 * @endcode
 *
 * @par Copyability / movability
 * Not copyable, not movable.
 *
 * @tparam TaskSize Total size in bytes of each @ref detail::sbo_callable
 *         slot (default 64). Usable inline buffer = @c TaskSize - 8 bytes
 *         on 64-bit platforms.
 *
 * @see lightweight_pool_backend (alias for @c
 * lightweight_pool_backend_base<64>), scheduled_lightweight_pool_backend
 * (scheduled variant).
 */
template <size_t TaskSize = 64>
class lightweight_pool_backend_base
{
public:
  /**
   * @brief Construct a lightweight pool with @p num_threads workers.
   * @param num_threads Number of worker threads (clamped to at least 1).
   *                    Defaults to @c std::thread::hardware_concurrency().
   */
  explicit lightweight_pool_backend_base(size_t num_threads = std::thread::hardware_concurrency())
      : num_threads_(num_threads == 0 ? 1 : num_threads)
  {
    workers_.reserve(num_threads_);
    try
      {
        for (size_t i = 0; i < num_threads_; ++i)
          workers_.emplace_back(&lightweight_pool_backend_base::worker_loop, this);
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

  lightweight_pool_backend_base(lightweight_pool_backend_base const&) = delete;
  auto operator=(lightweight_pool_backend_base const&) -> lightweight_pool_backend_base& = delete;

  ~lightweight_pool_backend_base()
  {
    shutdown(shutdown_policy_backend::drain);
  }

  /// @name Task submission
  /// @{

  /**
   * @brief Post a fire-and-forget task (throwing variant).
   *
   * The callable and its arguments are bound into a
   * @ref detail::sbo_callable and pushed into the shared queue.
   *
   * @tparam F    Callable type.
   * @tparam Args Argument types forwarded to @p F.
   * @throws std::runtime_error If the pool is shutting down.
   * @see try_post() for the non-throwing variant.
   */
  template <typename F, typename... Args>
  void
  post(F&& f, Args&&... args)
  {
    auto r = try_post(std::forward<F>(f), std::forward<Args>(args)...);
    if (!r.has_value())
      throw std::runtime_error("lightweight_pool_backend is shutting down");
  }

  /**
   * @brief Post a fire-and-forget task (non-throwing variant).
   *
   * @return @c expected<void, std::error_code> --
   *         @c std::errc::operation_canceled on shutdown.
   */
  template <typename F, typename... Args>
  auto
  try_post(F&& f, Args&&... args) -> expected<void, std::error_code>
  {
    detail::sbo_callable<TaskSize> task(detail::bind_args(std::forward<F>(f), std::forward<Args>(args)...));
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_)
        return unexpected(std::make_error_code(std::errc::operation_canceled));
      tasks_.push(std::move(task));
    }
    condition_.notify_one();
    return {};
  }

  /**
   * @brief Post a range of callables under a single lock acquisition.
   *
   * More efficient than calling @ref post() in a loop because the mutex
   * is acquired only once and all workers are woken via @c notify_all().
   *
   * @tparam Iterator Forward iterator whose value_type is callable as @c
   * void().
   * @throws std::runtime_error If the pool is shutting down.
   */
  template <typename Iterator>
  void
  post_batch(Iterator begin, Iterator end)
  {
    auto r = try_post_batch(begin, end);
    if (!r.has_value())
      throw std::runtime_error("lightweight_pool_backend is shutting down");
  }

  /**
   * @brief Batch post (non-throwing).
   * @return @c expected<void, std::error_code>.
   */
  template <typename Iterator>
  auto
  try_post_batch(Iterator begin, Iterator end) -> expected<void, std::error_code>
  {
    std::vector<detail::sbo_callable<TaskSize>> prepared;
    prepared.reserve(std::distance(begin, end));
    for (auto it = begin; it != end; ++it)
      prepared.emplace_back(*it);

    bool enqueued = false;
    try
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_)
          return unexpected(std::make_error_code(std::errc::operation_canceled));
        for (auto& task : prepared)
          {
            tasks_.push(std::move(task));
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
    return {};
  }

  /// @}

  /// @name Lifecycle
  /// @{

  /**
   * @brief Shut the pool down.
   *
   * @param policy @c drain (default) - workers finish all queued tasks
   *               before exiting. @c drop_pending - the queue is cleared
   *               and only the currently executing tasks are allowed to
   *               finish.
   *
   * Safe to call more than once (subsequent calls are no-ops).
   */
  void
  shutdown(shutdown_policy_backend policy = shutdown_policy_backend::drain)
  {
    if (is_current_worker())
      detail::throw_worker_deadlock();
    std::lock_guard<std::recursive_mutex> shutdown_lock(shutdown_mutex_);
    std::queue<detail::sbo_callable<TaskSize>> discarded;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_)
        return;
      stop_ = true;
      if (policy == shutdown_policy_backend::drop_pending)
        tasks_.swap(discarded);
    }
    shutdown_completed_all_ = discarded.empty();
    condition_.notify_all();
    drain_condition_.notify_all();
    {
      std::queue<detail::sbo_callable<TaskSize>> empty;
      discarded.swap(empty);
    }
    for (auto& w : workers_)
      {
        if (w.joinable())
          w.join();
      }
    workers_.clear();
    shutdown_completed_at_ = std::chrono::steady_clock::now();
  }

  /**
   * @brief Attempt a timed drain.
   *
   * Waits up to @p timeout for all tasks to complete, then discards queued
   * work and waits for any already-running tasks.
   *
   * New submissions are rejected before the timed wait begins.
   * @return @c true if all tasks completed within the deadline,
   *         @c false if the timeout expired (pool is still shut down).
   */
  auto
  shutdown_for(std::chrono::milliseconds timeout) -> bool
  {
    if (is_current_worker())
      detail::throw_worker_deadlock();
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    std::lock_guard<std::recursive_mutex> shutdown_lock(shutdown_mutex_);
    std::unique_lock<std::mutex> lock(mutex_);
    if (stop_)
      return shutdown_completed_all_ && shutdown_completed_at_ <= deadline;
    stop_ = true;
    condition_.notify_all();
    bool const drained = drain_condition_.wait_until(
        lock, deadline, [this] { return tasks_.empty() && active_tasks_.load(std::memory_order_acquire) == 0; });
    std::queue<detail::sbo_callable<TaskSize>> discarded;
    if (!drained)
      tasks_.swap(discarded);
    shutdown_completed_all_ = discarded.empty();
    lock.unlock();
    condition_.notify_all();
    drain_condition_.notify_all();
    {
      std::queue<detail::sbo_callable<TaskSize>> empty;
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

  /// @brief Number of worker threads.
  [[nodiscard]] auto
  size() const noexcept -> size_t
  {
    return num_threads_;
  }

  [[nodiscard]] auto
  is_current_worker() const noexcept -> bool
  {
    return current_pool == this;
  }

  /// @}

  /// @name Thread configuration
  /// @{

  /**
   * @brief Name, schedule and prioritize all worker threads.
   *
   * Workers are named @c name_prefix + "_0", @c "_1", etc.
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

private:
  size_t num_threads_;
  std::vector<detail::thread_backend> workers_;
  std::queue<detail::sbo_callable<TaskSize>> tasks_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::condition_variable drain_condition_;
  std::recursive_mutex shutdown_mutex_;
  std::atomic<bool> stop_{ false };
  bool shutdown_completed_all_{ true };
  std::chrono::steady_clock::time_point shutdown_completed_at_{};
  std::atomic<size_t> active_tasks_{ 0 };
  inline static thread_local lightweight_pool_backend_base* current_pool = nullptr;

  void
  worker_loop()
  {
    detail::worker_context_guard<lightweight_pool_backend_base> worker_context(current_pool, this);
    while (true)
      {
        detail::sbo_callable<TaskSize> task;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
          if (stop_ && tasks_.empty())
            return;
          if (!tasks_.empty())
            {
              task = std::move(tasks_.front());
              tasks_.pop();
              active_tasks_.fetch_add(1, std::memory_order_relaxed);
            }
          else
            continue;
        }
        try
          {
            task();
          }
        catch (...)
          {
          }
        active_tasks_.fetch_sub(1, std::memory_order_relaxed);
        drain_condition_.notify_all();
      }
  }
};

/**
 * @typedef lightweight_pool_backend
 * @brief Default lightweight pool with 64-byte task slots (56 bytes usable).
 *
 * Sufficient for lambdas capturing up to ~7 pointers on 64-bit platforms.
 *
 * @see lightweight_pool_backend_base
 */
using lightweight_pool_backend = lightweight_pool_backend_base<>;
