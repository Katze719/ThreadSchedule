#pragma once

/**
 * @file thread_pool.hpp
 * @brief Thread pools: work_stealing_pool_backend, thread_pool_backend_base,
 * lightweight_pool_backend_base, and global_pool_backend.
 */

#include "callable.hpp"
#include "detail/thread_backend.hpp"
#include "expected.hpp"
#include "scheduler_policy.hpp"
#include "thread_registry.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <tuple>
#include <type_traits>
#include <vector>

namespace threadschedule
{

namespace detail
{

template <typename WorkerRange>
inline auto
configure_worker_threads(WorkerRange& workers, std::string const& name_prefix,
                         native_scheduling_policy policy,
                         native_thread_priority priority)
    -> expected<void, std::error_code>
{
  std::error_code first_error;
  for (size_t i = 0; i < workers.size(); ++i)
    {
      std::string const thread_name = name_prefix + "_" + std::to_string(i);
      auto named = workers[i].set_name(thread_name);
      if (!named && !first_error)
        first_error = named.error();
      auto scheduled = workers[i].set_scheduling_policy(policy, priority);
      if (!scheduled && !first_error)
        first_error = scheduled.error();
    }
  if (first_error)
    return unexpected(first_error);
  return {};
}

template <typename WorkerRange>
inline auto
configure_worker_threads(WorkerRange& workers,
                         native_thread_config const& config)
    -> expected<void, std::error_code>
{
  std::error_code first_error;
  auto const scheduling = resolve_scheduling_config(config.scheduling);
  for (size_t i = 0; i < workers.size(); ++i)
    {
      if (!config.name.empty())
        {
          std::string const thread_name
              = config.name + "_" + std::to_string(i);
          auto named = workers[i].set_name(thread_name);
          if (!named && !first_error)
            first_error = named.error();
        }
      auto scheduled = workers[i].set_scheduling_policy(scheduling.policy,
                                                        scheduling.priority);
      if (!scheduled && !first_error)
        first_error = scheduled.error();
      if (config.affinity.has_value())
        {
          auto affinity = workers[i].set_affinity(*config.affinity);
          if (!affinity && !first_error)
            first_error = affinity.error();
        }
    }
  if (first_error)
    return unexpected(first_error);
  return {};
}

template <typename WorkerRange>
inline auto
set_worker_affinity(WorkerRange& workers,
                    native_thread_affinity const& affinity)
    -> expected<void, std::error_code>
{
  std::error_code first_error;
  for (auto& worker : workers)
    {
      auto configured = worker.set_affinity(affinity);
      if (!configured && !first_error)
        first_error = configured.error();
    }
  if (first_error)
    return unexpected(first_error);
  return {};
}

template <typename WorkerRange>
inline auto
distribute_workers_across_cpus(WorkerRange& workers)
    -> expected<void, std::error_code>
{
  auto const cpu_count = std::thread::hardware_concurrency();
  if (cpu_count == 0)
    return unexpected(std::make_error_code(std::errc::invalid_argument));

  std::error_code first_error;
  for (size_t i = 0; i < workers.size(); ++i)
    {
      native_thread_affinity affinity({ static_cast<int>(i % cpu_count) });
      auto configured = workers[i].set_affinity(affinity);
      if (!configured && !first_error)
        first_error = configured.error();
    }
  if (first_error)
    return unexpected(first_error);
  return {};
}

template <typename Pool, typename Iterator, typename F>
inline void
parallel_for_each_chunked(Pool& pool, Iterator begin, Iterator end, F&& func,
                          size_t num_workers)
{
  auto const total = static_cast<size_t>(std::distance(begin, end));
  if (total == 0)
    return;

  size_t const chunk_size = (std::max)(size_t(1), total / (num_workers * 4));
  std::vector<std::future<void>> futures;
  auto it = begin;

  while (it != end)
    {
      auto remaining = static_cast<size_t>(std::distance(it, end));
      auto this_chunk = (std::min)(chunk_size, remaining);
      auto chunk_end = it;
      std::advance(chunk_end, this_chunk);

      futures.push_back(pool.submit(
          [it, chunk_end, &func]()
            {
              for (auto cur = it; cur != chunk_end; ++cur)
                func(*cur);
            }));

      it = chunk_end;
    }

  for (auto& f : futures)
    f.get();
}

// ---------------------------------------------------------------------------
// bind_args -- ABI-consistent C++17 tuple binding
// ---------------------------------------------------------------------------

/**
 * @brief Bind a callable with its arguments into a nullary lambda.
 *
 * Always uses the same C++17 representation so inline definitions remain
 * identical when different consumers select different language modes.
 */
template <typename F, typename... Args>
auto
bind_args(F&& f, Args&&... args)
{
  return [fn = std::forward<F>(f),
          tup = std::make_tuple(std::forward<Args>(args)...)]() mutable
    { return std::apply(std::move(fn), std::move(tup)); };
}

// ---------------------------------------------------------------------------
// sbo_callable -- type-erased callable with inline small-buffer storage
// ---------------------------------------------------------------------------

/**
 * @brief Type-erased, move-only callable with configurable inline storage.
 *
 * Designed as a lightweight replacement for @c std::function when heap
 * allocations are undesirable. Callables whose size and alignment fit
 * within the inline buffer are stored in-place (Small Buffer Optimization);
 * larger callables fall back to a heap allocation transparently.
 *
 * @par Storage layout
 * @code
 *   |<---------- TaskSize bytes ---------->|
 *   [ vtable* (8 B) | inline buffer        ]
 * @endcode
 * The usable inline buffer is @c TaskSize - sizeof(void*) bytes
 * (56 bytes on 64-bit platforms with the default @c TaskSize of 64).
 *
 * @par Inline eligibility
 * A callable @c F is stored inline when all of the following hold:
 * - @c sizeof(F) <= buffer_size
 * - @c alignof(F) <= alignof(std::max_align_t)
 * - @c std::is_nothrow_move_constructible_v<F>
 *
 * @par Move semantics
 * Move-only. Invoking @c operator() consumes the callable (invoke + destroy),
 * leaving the object in an empty state. This single-shot design avoids the
 * overhead of reference counting or shared ownership.
 *
 * @par Thread safety
 * Not thread-safe. Intended to be used as a queue element inside a
 * mutex-protected task queue.
 *
 * @tparam TaskSize Total object size in bytes (default 64, one x86 cache
 * line).
 */
template <size_t TaskSize = 64>
class sbo_callable
{
  static_assert(TaskSize > sizeof(void*),
                "TaskSize must be larger than a pointer");

  struct vtable
  {
    void (*invoke)(void* storage);
    void (*destroy)(void* storage);
    void (*move_to)(void* dst, void* src) noexcept;
  };

  static constexpr size_t buffer_size = TaskSize - sizeof(vtable const*);

  template <typename F>
  static constexpr bool fits_inline_v
      = sizeof(F) <= buffer_size && alignof(F) <= alignof(std::max_align_t)
        && std::is_nothrow_move_constructible_v<F>;

  template <typename F>
  static vtable const*
  vtable_for() noexcept
  {
    if constexpr (fits_inline_v<F>)
      {
        static constexpr vtable vt{ [](void* s) { (*static_cast<F*>(s))(); },
                                    [](void* s) { static_cast<F*>(s)->~F(); },
                                    [](void* dst, void* src) noexcept
                                      {
                                        ::new (dst) F(
                                            std::move(*static_cast<F*>(src)));
                                        static_cast<F*>(src)->~F();
                                      } };
        return &vt;
      }
    else
      {
        static constexpr vtable vt{
          [](void* s) { (*(*static_cast<F**>(s)))(); },
          [](void* s) { delete *static_cast<F**>(s); },
          [](void* dst, void* src) noexcept
            {
              *static_cast<F**>(dst) = *static_cast<F**>(src);
              *static_cast<F**>(src) = nullptr;
            }
        };
        return &vt;
      }
  }

public:
  sbo_callable() = default;

  template <typename F,
            typename
            = std::enable_if_t<!std::is_same_v<std::decay_t<F>, sbo_callable>>>
  sbo_callable(F&& f) // NOLINT(google-explicit-constructor)
  {
    using decay_type = std::decay_t<F>;
    vtable_ = vtable_for<decay_type>();
    if constexpr (fits_inline_v<decay_type>)
      ::new (buffer_) decay_type(std::forward<F>(f));
    else
      *reinterpret_cast<decay_type**>(buffer_)
          = new decay_type(std::forward<F>(f));
  }

  sbo_callable(sbo_callable&& other) noexcept : vtable_(other.vtable_)
  {
    if (vtable_)
      {
        vtable_->move_to(buffer_, other.buffer_);
        other.vtable_ = nullptr;
      }
  }

  auto
  operator=(sbo_callable&& other) noexcept -> sbo_callable&
  {
    if (this != &other)
      {
        if (vtable_)
          vtable_->destroy(buffer_);
        vtable_ = other.vtable_;
        if (vtable_)
          {
            vtable_->move_to(buffer_, other.buffer_);
            other.vtable_ = nullptr;
          }
      }
    return *this;
  }

  sbo_callable(sbo_callable const&) = delete;
  auto operator=(sbo_callable const&) -> sbo_callable& = delete;

  ~sbo_callable()
  {
    if (vtable_)
      vtable_->destroy(buffer_);
  }

  explicit
  operator bool() const noexcept
  {
    return vtable_ != nullptr;
  }

  void
  operator()()
  {
    auto* vt = vtable_;
    vtable_ = nullptr;
    vt->invoke(buffer_);
    vt->destroy(buffer_);
  }

private:
  vtable const* vtable_ = nullptr;
  alignas(std::max_align_t) unsigned char buffer_[buffer_size];
};

} // namespace detail

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
 * @c default_capacity = 1024). push() returns @c false when the deque is
 * full; it never reallocates. Choose a capacity large enough for your expected
 * burst size or use an overflow queue externally (as @ref
 * work_stealing_pool_backend does).
 *
 * @par Memory layout
 * Each stored item is wrapped in an @c aligned_item that is aligned to
 * @c cache_line_size (64 bytes) to prevent false sharing between adjacent
 * elements when multiple threads access neighboring slots.
 *
 * @par Copyability / movability
 * Not copyable and not movable (contains a std::mutex).
 *
 * @tparam T The task type. Must be move-constructible.
 */

/// Callback invoked when a pool worker begins executing a task.
using task_start_callback = detail::copyable_callable<void(
    std::chrono::steady_clock::time_point, std::thread::id)>;

/// Callback invoked when a pool worker finishes executing a task.
using task_end_callback = detail::copyable_callable<void(
    std::chrono::steady_clock::time_point, std::thread::id,
    std::chrono::microseconds elapsed)>;

using task_start_callback_storage = task_start_callback;
using task_end_callback_storage = task_end_callback;

template <typename T>
class work_stealing_deque
{
public:
  static constexpr size_t cache_line_size = 64;
  static constexpr size_t default_capacity = 1024;

private:
  struct alignas(cache_line_size) aligned_item
  {
    T item;
    aligned_item() = default;
    aligned_item(T&& t) : item(std::move(t)) {}
    template <typename U = T,
              std::enable_if_t<std::is_copy_constructible_v<U>, int> = 0>
    aligned_item(T const& t) : item(t)
    {
    }
  };

  std::unique_ptr<aligned_item[]> buffer_;
  size_t capacity_;

  // The owner pushes and pops at the top.
  alignas(cache_line_size) std::atomic<size_t> top_{ 0 };

  // Thieves steal from the bottom.
  alignas(cache_line_size) std::atomic<size_t> bottom_{ 0 };

  // Synchronizes accesses that cannot use the lock-free path.
  alignas(cache_line_size) mutable std::mutex mutex_;

public:
  explicit work_stealing_deque(size_t capacity = default_capacity)
      : buffer_(std::make_unique<aligned_item[]>(capacity)),
        capacity_(capacity)
  {
  }

  [[nodiscard]] auto
  push(T&& item) -> bool
  {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t const t = top_.load(std::memory_order_relaxed);
    size_t const b = bottom_.load(std::memory_order_relaxed);

    if (t - b >= capacity_)
      {
        return false;
      }

    buffer_[t % capacity_] = aligned_item(std::move(item));
    top_.store(t + 1, std::memory_order_release);
    return true;
  }

  template <typename U = T,
            std::enable_if_t<std::is_copy_constructible_v<U>, int> = 0>
  [[nodiscard]] auto
  push(T const& item) -> bool
  {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t const t = top_.load(std::memory_order_relaxed);
    size_t const b = bottom_.load(std::memory_order_relaxed);

    if (t - b >= capacity_)
      {
        return false;
      }

    buffer_[t % capacity_] = aligned_item(item);
    top_.store(t + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] auto
  pop(T& item) -> bool
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

  [[nodiscard]] auto
  steal(T& item) -> bool
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

  [[nodiscard]] auto
  size() const -> size_t
  {
    size_t const t = top_.load(std::memory_order_relaxed);
    size_t const b = bottom_.load(std::memory_order_relaxed);
    return t > b ? t - b : 0;
  }

  [[nodiscard]] auto
  empty() const -> bool
  {
    return size() == 0;
  }

  void
  clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    bottom_.store(0, std::memory_order_relaxed);
    top_.store(0, std::memory_order_relaxed);
  }

  [[nodiscard]] auto
  clear_and_count() -> size_t
  {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t const t = top_.load(std::memory_order_relaxed);
    size_t const b = bottom_.load(std::memory_order_relaxed);
    size_t const count = t > b ? t - b : 0;
    bottom_.store(0, std::memory_order_relaxed);
    top_.store(0, std::memory_order_relaxed);
    return count;
  }
};

/**
 * @brief Controls how a pool handles pending tasks during shutdown.
 *
 * Passed to @c shutdown() on any pool type to select graceful vs. immediate
 * shutdown behaviour.
 *
 * | Policy          | Running tasks | Queued tasks        |
 * |-----------------|---------------|---------------------|
 * | @c drain        | Finish        | Execute, then stop  |
 * | @c drop_pending | Finish        | Discard immediately |
 *
 * @see work_stealing_pool_backend::shutdown,
 * thread_pool_backend_base::shutdown, lightweight_pool_backend_base::shutdown
 */
enum class shutdown_policy_backend : uint8_t
{
  drain,       ///< Finish all queued tasks before stopping (default).
  drop_pending ///< Finish running tasks, discard queued ones.
};

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
  using queued_task = detail::move_callable<void()>;

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

  explicit work_stealing_pool_backend(
      size_t num_threads = std::thread::hardware_concurrency(),
      size_t deque_capacity
      = work_stealing_deque<queued_task>::default_capacity,
      bool register_workers = false)
      : num_threads_(num_threads == 0 ? 1 : num_threads),
        register_workers_(register_workers), stop_(false), next_victim_(0),
        start_time_(std::chrono::steady_clock::now())
  {
    worker_queues_.resize(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i)
      {
        worker_queues_[i] = std::make_unique<work_stealing_deque<queued_task>>(
            deque_capacity);
      }

    workers_.reserve(num_threads_);

    for (size_t i = 0; i < num_threads_; ++i)
      {
        workers_.emplace_back(&work_stealing_pool_backend::worker_function,
                              this, i);
      }
  }

  work_stealing_pool_backend(work_stealing_pool_backend const&) = delete;
  auto operator=(work_stealing_pool_backend const&)
      -> work_stealing_pool_backend& = delete;

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
    size_t dropped_tasks = 0;
    {
      std::lock_guard<std::mutex> lock(overflow_mutex_);
      if (stop_.exchange(true, std::memory_order_acq_rel))
        return;

      if (policy == shutdown_policy_backend::drop_pending)
        {
          dropped_tasks += overflow_tasks_.size();
          std::queue<queued_task> empty;
          overflow_tasks_.swap(empty);
          for (auto& q : worker_queues_)
            dropped_tasks += q->clear_and_count();
        }
    }

    if (dropped_tasks != 0)
      {
        std::lock_guard<std::mutex> lock(completion_mutex_);
        outstanding_tasks_.fetch_sub(dropped_tasks, std::memory_order_acq_rel);
      }

    if (dropped_tasks != 0)
      completion_condition_.notify_all();
    wakeup_condition_.notify_all();

    for (auto& worker : workers_)
      {
        if (worker.joinable())
          worker.join();
      }

    workers_.clear();
  }

  /**
   * @brief Attempt a timed drain: finish as many tasks as possible within
   *        @p timeout, then force-stop remaining workers.
   * @return @c true if all tasks completed within the deadline,
   *         @c false if the timeout expired first.
   */
  auto
  shutdown_for(std::chrono::milliseconds timeout) -> bool
  {
    auto const deadline = std::chrono::steady_clock::now() + timeout;

    {
      std::lock_guard<std::mutex> lock(overflow_mutex_);
      if (stop_.load(std::memory_order_acquire))
        return true;
    }

    std::unique_lock<std::mutex> lock(completion_mutex_);
    bool const drained = completion_condition_.wait_until(
        lock, deadline, [this]
          { return outstanding_tasks_.load(std::memory_order_acquire) == 0; });

    shutdown(shutdown_policy_backend::drain);
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
  try_submit(F&& f, Args&&... args)
      -> expected<std::future<std::invoke_result_t<F, Args...>>,
                  std::error_code>
  {
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        detail::bind_args(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> result = task->get_future();

    if (stop_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));

    size_t const preferred_queue
        = next_victim_.fetch_add(1, std::memory_order_relaxed) % num_threads_;

    outstanding_tasks_.fetch_add(1, std::memory_order_release);
    if (worker_queues_[preferred_queue]->push([task]() { (*task)(); }))
      {
        wakeup_condition_.notify_one();
        return result;
      }
    outstanding_tasks_.fetch_sub(1, std::memory_order_acq_rel);

    for (size_t attempts = 0; attempts < (std::min)(num_threads_, size_t(3));
         ++attempts)
      {
        size_t const idx = (preferred_queue + attempts + 1) % num_threads_;
        outstanding_tasks_.fetch_add(1, std::memory_order_release);
        if (worker_queues_[idx]->push([task]() { (*task)(); }))
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
      outstanding_tasks_.fetch_add(1, std::memory_order_release);
      overflow_tasks_.emplace([task]() { (*task)(); });
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
  submit(F&& f, Args&&... args)
      -> std::future<std::invoke_result_t<F, Args...>>
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
    queued_task bound(detail::make_move_callable<void()>(
        detail::bind_args(std::forward<F>(f), std::forward<Args>(args)...)));

    if (stop_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));

    size_t const preferred_queue
        = next_victim_.fetch_add(1, std::memory_order_relaxed) % num_threads_;

    outstanding_tasks_.fetch_add(1, std::memory_order_release);
    if (worker_queues_[preferred_queue]->push(std::move(bound)))
      {
        wakeup_condition_.notify_one();
        return {};
      }
    outstanding_tasks_.fetch_sub(1, std::memory_order_acq_rel);

    for (size_t attempts = 0; attempts < (std::min)(num_threads_, size_t(3));
         ++attempts)
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
      outstanding_tasks_.fetch_add(1, std::memory_order_release);
      overflow_tasks_.emplace(std::move(bound));
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
  try_submit_batch(Iterator begin, Iterator end)
      -> expected<std::vector<std::future<void>>, std::error_code>
  {
    std::vector<std::future<void>> futures;
    size_t const batch_size = std::distance(begin, end);
    futures.reserve(batch_size);

    if (stop_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));

    size_t queue_idx
        = next_victim_.fetch_add(batch_size, std::memory_order_relaxed)
          % num_threads_;

    for (auto it = begin; it != end; ++it)
      {
        auto task = std::make_shared<std::packaged_task<void()>>(*it);
        futures.push_back(task->get_future());

        bool queued = false;
        for (size_t attempts = 0; attempts < num_threads_; ++attempts)
          {
            outstanding_tasks_.fetch_add(1, std::memory_order_release);
            if (worker_queues_[queue_idx]->push([task]() { (*task)(); }))
              {
                queued = true;
                break;
              }
            outstanding_tasks_.fetch_sub(1, std::memory_order_acq_rel);
            queue_idx = (queue_idx + 1) % num_threads_;
          }

        if (!queued)
          {
            std::lock_guard<std::mutex> lock(overflow_mutex_);
            outstanding_tasks_.fetch_add(1, std::memory_order_release);
            overflow_tasks_.emplace([task]() { (*task)(); });
          }
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
    detail::parallel_for_each_chunked(*this, begin, end, std::forward<F>(func),
                                      num_threads_);
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
    auto const elapsed
        = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

    statistics stats;
    stats.total_threads = num_threads_;
    stats.active_threads = active_tasks_.load(std::memory_order_acquire);
    stats.pending_tasks = pending_tasks();
    stats.completed_tasks = completed_tasks_.load(std::memory_order_acquire);
    stats.stolen_tasks = stolen_tasks_.load(std::memory_order_acquire);

    if (elapsed.count() > 0)
      {
        stats.tasks_per_second
            = static_cast<double>(stats.completed_tasks) / elapsed.count();
      }
    else
      {
        stats.tasks_per_second = 0.0;
      }

    auto const total_task_time
        = total_task_time_.load(std::memory_order_acquire);
    if (stats.completed_tasks > 0)
      {
        stats.avg_task_time = std::chrono::microseconds(
            total_task_time / stats.completed_tasks);
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
  configure_threads(std::string const& name_prefix,
                    native_scheduling_policy policy
                    = native_scheduling_policy::other,
                    native_thread_priority priority
                    = native_thread_priority::normal())
      -> expected<void, std::error_code>
  {
    return detail::configure_worker_threads(workers_, name_prefix, policy,
                                            priority);
  }

  auto
  configure_threads(native_thread_config const& config)
      -> expected<void, std::error_code>
  {
    return detail::configure_worker_threads(workers_, config);
  }

  /// @brief Pin all workers to the same CPU set.
  auto
  set_affinity(native_thread_affinity const& affinity)
      -> expected<void, std::error_code>
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
    std::unique_lock<std::mutex> lock(completion_mutex_);
    completion_condition_.wait(
        lock, [this]
          { return outstanding_tasks_.load(std::memory_order_acquire) == 0; });
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
            std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<Callback>,
                                             task_start_callback>,
                             int> = 0>
  void
  set_on_task_start(Callback&& cb)
  {
    static_assert(
        std::is_invocable_r_v<void, Callback&,
                              std::chrono::steady_clock::time_point,
                              std::thread::id>,
        "Task start callback must accept (time_point, std::thread::id)");
    std::lock_guard<std::mutex> lock(trace_mutex_);
    on_task_start_ = detail::make_copyable_callable<void(
        std::chrono::steady_clock::time_point, std::thread::id)>(
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
            std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<Callback>,
                                             task_end_callback>,
                             int> = 0>
  void
  set_on_task_end(Callback&& cb)
  {
    static_assert(
        std::is_invocable_r_v<void, Callback&,
                              std::chrono::steady_clock::time_point,
                              std::thread::id, std::chrono::microseconds>,
        "Task end callback must accept (time_point, std::thread::id, "
        "std::chrono::microseconds)");
    std::lock_guard<std::mutex> lock(trace_mutex_);
    on_task_end_ = detail::make_copyable_callable<void(
        std::chrono::steady_clock::time_point, std::thread::id,
        std::chrono::microseconds)>(std::forward<Callback>(cb));
  }

  /// @}

private:
  size_t num_threads_;
  bool register_workers_;
  std::vector<detail::thread_backend> workers_;
  std::vector<std::unique_ptr<work_stealing_deque<queued_task>>>
      worker_queues_;

  std::queue<queued_task> overflow_tasks_;
  mutable std::mutex overflow_mutex_;

  std::atomic<bool> stop_;
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

  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  void
  worker_function(size_t worker_id)
  {
    std::optional<auto_register_current_thread> reg_guard;
    if (register_workers_)
      reg_guard.emplace("hp_worker_" + std::to_string(worker_id),
                        "threadschedule.pool");

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
            size_t const max_steal_attempts
                = (std::min)(num_threads_, size_t(4));
            for (size_t attempts = 0; attempts < max_steal_attempts;
                 ++attempts)
              {
                size_t const victim_id = dist(gen);
                if (victim_id != worker_id
                    && worker_queues_[victim_id]->steal(task))
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

            task_start_callback_storage on_task_start;
            {
              std::lock_guard<std::mutex> tl(trace_mutex_);
              on_task_start = on_task_start_;
            }
            if (on_task_start)
              on_task_start(start_time, tid);

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
            auto const end_time = std::chrono::steady_clock::now();

            auto const task_duration
                = std::chrono::duration_cast<std::chrono::microseconds>(
                    end_time - start_time);
            total_task_time_.fetch_add(task_duration.count(),
                                       std::memory_order_relaxed);

            task_end_callback_storage on_task_end;
            {
              std::lock_guard<std::mutex> tl(trace_mutex_);
              on_task_end = on_task_end_;
            }
            if (on_task_end)
              on_task_end(end_time, tid, task_duration);

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
            if (stop_.load(std::memory_order_acquire)
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

// ---------------------------------------------------------------------------
// Wait policies for thread_pool_backend_base
// ---------------------------------------------------------------------------

/**
 * @brief Wait policy that blocks indefinitely until work is available.
 *
 * Workers consume zero CPU while idle but wake instantly when a task is
 * enqueued. Used by the @c thread_pool_backend type alias.
 */
struct indefinite_wait
{
  template <typename Lock, typename Pred>
  static auto
  wait(std::condition_variable& cv, Lock& lock, Pred pred) -> bool
  {
    cv.wait(lock, pred);
    return true;
  }
};

/**
 * @brief Wait policy that polls with a configurable timeout.
 *
 * Workers periodically re-check the queue even without notification, trading
 * a small amount of CPU for lower wake-up latency under bursty workloads.
 * Used by the @c polling_pool_backend type alias (default 10 ms).
 *
 * @tparam IntervalMs Polling interval in milliseconds.
 */
template <unsigned IntervalMs = 10>
struct polling_wait
{
  template <typename Lock, typename Pred>
  static auto
  wait(std::condition_variable& cv, Lock& lock, Pred pred) -> bool
  {
    return cv.wait_for(lock, std::chrono::milliseconds(IntervalMs), pred);
  }
};

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
  using queued_task = detail::move_callable<void()>;

  struct statistics
  {
    size_t total_threads;
    size_t active_threads;
    size_t pending_tasks;
    size_t completed_tasks;
    double tasks_per_second;
    std::chrono::microseconds avg_task_time;
  };

  explicit thread_pool_backend_base(size_t num_threads
                                    = std::thread::hardware_concurrency(),
                                    bool register_workers = false)
      : num_threads_(num_threads == 0 ? 1 : num_threads),
        register_workers_(register_workers), stop_(false),
        start_time_(std::chrono::steady_clock::now())
  {
    workers_.reserve(num_threads_);

    for (size_t i = 0; i < num_threads_; ++i)
      {
        workers_.emplace_back(&thread_pool_backend_base::worker_function, this,
                              i);
      }
  }

  thread_pool_backend_base(thread_pool_backend_base const&) = delete;
  auto operator=(thread_pool_backend_base const&)
      -> thread_pool_backend_base& = delete;

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
  try_submit(F&& f, Args&&... args)
      -> expected<std::future<std::invoke_result_t<F, Args...>>,
                  std::error_code>
  {
    using return_type = std::invoke_result_t<F, Args...>;

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
  submit(F&& f, Args&&... args)
      -> std::future<std::invoke_result_t<F, Args...>>
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
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (stop_)
        return unexpected(std::make_error_code(std::errc::operation_canceled));
      tasks_.emplace(
          detail::bind_args(std::forward<F>(f), std::forward<Args>(args)...));
    }
    condition_.notify_one();
    return {};
  }

  /**
   * @brief Submit a range of @c void() callables in one go (non-throwing).
   *
   * All tasks are enqueued under a single lock acquisition.
   */
  template <typename Iterator>
  auto
  try_submit_batch(Iterator begin, Iterator end)
      -> expected<std::vector<std::future<void>>, std::error_code>
  {
    std::vector<std::future<void>> futures;
    futures.reserve(std::distance(begin, end));

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (stop_)
        return unexpected(std::make_error_code(std::errc::operation_canceled));

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
  template <typename Iterator, typename F>
  void
  parallel_for_each(Iterator begin, Iterator end, F&& func)
  {
    detail::parallel_for_each_chunked(*this, begin, end, std::forward<F>(func),
                                      num_threads_);
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
  configure_threads(std::string const& name_prefix,
                    native_scheduling_policy policy
                    = native_scheduling_policy::other,
                    native_thread_priority priority
                    = native_thread_priority::normal())
      -> expected<void, std::error_code>
  {
    return detail::configure_worker_threads(workers_, name_prefix, policy,
                                            priority);
  }

  auto
  configure_threads(native_thread_config const& config)
      -> expected<void, std::error_code>
  {
    return detail::configure_worker_threads(workers_, config);
  }

  /// @brief Pin all workers to the same CPU set.
  auto
  set_affinity(native_thread_affinity const& affinity)
      -> expected<void, std::error_code>
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
    std::unique_lock<std::mutex> lock(queue_mutex_);
    task_finished_condition_.wait(
        lock,
        [this]
          {
            return tasks_.empty()
                   && active_tasks_.load(std::memory_order_acquire) == 0;
          });
  }

  /**
   * @brief Shut the pool down.
   * @param policy @c drain (default) finishes all queued tasks;
   *               @c drop_pending discards queued tasks.
   */
  void
  shutdown(shutdown_policy_backend policy = shutdown_policy_backend::drain)
  {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (stop_)
        return;
      stop_ = true;
      if (policy == shutdown_policy_backend::drop_pending)
        {
          std::queue<queued_task> empty;
          tasks_.swap(empty);
        }
    }

    condition_.notify_all();

    for (auto& worker : workers_)
      {
        if (worker.joinable())
          worker.join();
      }

    workers_.clear();
  }

  /**
   * @brief Attempt a timed drain: finish as many tasks as possible within
   *        @p timeout, then force-stop remaining workers.
   * @return @c true if all tasks completed within the deadline,
   *         @c false if the timeout expired first.
   */
  auto
  shutdown_for(std::chrono::milliseconds timeout) -> bool
  {
    auto const deadline = std::chrono::steady_clock::now() + timeout;

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (stop_)
        return true;
    }

    std::unique_lock<std::mutex> lock(queue_mutex_);
    bool const drained = task_finished_condition_.wait_until(
        lock, deadline,
        [this]
          {
            return tasks_.empty()
                   && active_tasks_.load(std::memory_order_acquire) == 0;
          });
    lock.unlock();

    shutdown(shutdown_policy_backend::drain);
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
    auto const elapsed
        = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

    std::lock_guard<std::mutex> lock(queue_mutex_);
    statistics stats;
    stats.total_threads = num_threads_;
    stats.active_threads = active_tasks_.load(std::memory_order_acquire);
    stats.pending_tasks = tasks_.size();
    stats.completed_tasks = completed_tasks_.load(std::memory_order_acquire);

    if (elapsed.count() > 0)
      {
        stats.tasks_per_second
            = static_cast<double>(stats.completed_tasks) / elapsed.count();
      }
    else
      {
        stats.tasks_per_second = 0.0;
      }

    auto const total_task_time
        = total_task_time_.load(std::memory_order_acquire);
    if (stats.completed_tasks > 0)
      {
        stats.avg_task_time = std::chrono::microseconds(
            total_task_time / stats.completed_tasks);
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
            std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<Callback>,
                                             task_start_callback>,
                             int> = 0>
  void
  set_on_task_start(Callback&& cb)
  {
    static_assert(
        std::is_invocable_r_v<void, Callback&,
                              std::chrono::steady_clock::time_point,
                              std::thread::id>,
        "Task start callback must accept (time_point, std::thread::id)");
    std::lock_guard<std::mutex> lock(trace_mutex_);
    on_task_start_ = detail::make_copyable_callable<void(
        std::chrono::steady_clock::time_point, std::thread::id)>(
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
            std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<Callback>,
                                             task_end_callback>,
                             int> = 0>
  void
  set_on_task_end(Callback&& cb)
  {
    static_assert(
        std::is_invocable_r_v<void, Callback&,
                              std::chrono::steady_clock::time_point,
                              std::thread::id, std::chrono::microseconds>,
        "Task end callback must accept (time_point, std::thread::id, "
        "std::chrono::microseconds)");
    std::lock_guard<std::mutex> lock(trace_mutex_);
    on_task_end_ = detail::make_copyable_callable<void(
        std::chrono::steady_clock::time_point, std::thread::id,
        std::chrono::microseconds)>(std::forward<Callback>(cb));
  }

  /// @}

private:
  size_t num_threads_;
  bool register_workers_;
  std::vector<detail::thread_backend> workers_;
  std::queue<queued_task> tasks_;

  mutable std::mutex queue_mutex_;
  std::condition_variable condition_;
  std::condition_variable task_finished_condition_;
  std::atomic<bool> stop_;
  std::atomic<size_t> active_tasks_{ 0 };
  std::atomic<size_t> completed_tasks_{ 0 };
  std::atomic<uint64_t> total_task_time_{ 0 };

  std::mutex trace_mutex_;
  task_start_callback_storage on_task_start_;
  task_end_callback_storage on_task_end_;

  std::chrono::steady_clock::time_point start_time_;

  void
  worker_function(size_t worker_id)
  {
    std::optional<auto_register_current_thread> reg_guard;
    if (register_workers_)
      reg_guard.emplace("pool_worker_" + std::to_string(worker_id),
                        "threadschedule.pool");

    while (true)
      {
        queued_task task;
        bool found_task = false;

        {
          std::unique_lock<std::mutex> lock(queue_mutex_);

          if (WaitPolicy::wait(condition_, lock,
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

            task_start_callback_storage on_task_start;
            {
              std::lock_guard<std::mutex> tl(trace_mutex_);
              on_task_start = on_task_start_;
            }
            if (on_task_start)
              on_task_start(start_time, tid);

            // See work_stealing_pool_backend::worker_function for rationale.
            try
              {
                task();
              }
            catch (...)
              {
              }
            auto const end_time = std::chrono::steady_clock::now();

            auto const task_duration
                = std::chrono::duration_cast<std::chrono::microseconds>(
                    end_time - start_time);
            total_task_time_.fetch_add(task_duration.count(),
                                       std::memory_order_relaxed);

            task_end_callback_storage on_task_end;
            {
              std::lock_guard<std::mutex> tl(trace_mutex_);
              on_task_end = on_task_end_;
            }
            if (on_task_end)
              on_task_end(end_time, tid, task_duration);

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
  explicit lightweight_pool_backend_base(size_t num_threads
                                         = std::thread::hardware_concurrency())
      : num_threads_(num_threads == 0 ? 1 : num_threads)
  {
    workers_.reserve(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i)
      workers_.emplace_back(&lightweight_pool_backend_base::worker_loop, this);
  }

  lightweight_pool_backend_base(lightweight_pool_backend_base const&) = delete;
  auto operator=(lightweight_pool_backend_base const&)
      -> lightweight_pool_backend_base& = delete;

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
    detail::sbo_callable<TaskSize> task(
        detail::bind_args(std::forward<F>(f), std::forward<Args>(args)...));
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
  try_post_batch(Iterator begin, Iterator end)
      -> expected<void, std::error_code>
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_)
        return unexpected(std::make_error_code(std::errc::operation_canceled));
      for (auto it = begin; it != end; ++it)
        tasks_.push(detail::sbo_callable<TaskSize>(*it));
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
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_)
        return;
      stop_ = true;
      if (policy == shutdown_policy_backend::drop_pending)
        {
          std::queue<detail::sbo_callable<TaskSize>> empty;
          tasks_.swap(empty);
        }
    }
    condition_.notify_all();
    for (auto& w : workers_)
      {
        if (w.joinable())
          w.join();
      }
    workers_.clear();
  }

  /**
   * @brief Attempt a timed drain.
   *
   * Waits up to @p timeout for all tasks to complete, then performs a
   * full @c shutdown(drain).
   *
   * @return @c true if all tasks completed within the deadline,
   *         @c false if the timeout expired (pool is still shut down).
   */
  auto
  shutdown_for(std::chrono::milliseconds timeout) -> bool
  {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_)
        return true;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    bool const drained = drain_condition_.wait_until(
        lock, deadline,
        [this]
          {
            return tasks_.empty()
                   && active_tasks_.load(std::memory_order_acquire) == 0;
          });
    lock.unlock();
    shutdown(shutdown_policy_backend::drain);
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

  /// @}

  /// @name Thread configuration
  /// @{

  /**
   * @brief Name, schedule and prioritize all worker threads.
   *
   * Workers are named @c name_prefix + "_0", @c "_1", etc.
   */
  auto
  configure_threads(std::string const& name_prefix,
                    native_scheduling_policy policy
                    = native_scheduling_policy::other,
                    native_thread_priority priority
                    = native_thread_priority::normal())
      -> expected<void, std::error_code>
  {
    return detail::configure_worker_threads(workers_, name_prefix, policy,
                                            priority);
  }

  auto
  configure_threads(native_thread_config const& config)
      -> expected<void, std::error_code>
  {
    return detail::configure_worker_threads(workers_, config);
  }

  /// @brief Pin all workers to the same CPU set.
  auto
  set_affinity(native_thread_affinity const& affinity)
      -> expected<void, std::error_code>
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
  std::atomic<bool> stop_{ false };
  std::atomic<size_t> active_tasks_{ 0 };

  void
  worker_loop()
  {
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

// ---------------------------------------------------------------------------
// global_pool_backend
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
class global_pool_backend
{
public:
  /**
   * @brief Pre-configure the number of threads before first use.
   *
   * Must be called before instance() is first invoked. Subsequent calls
   * are ignored (std::call_once semantics).
   */
  static void
  init(size_t num_threads)
  {
    std::call_once(init_flag(),
                   [num_threads] { thread_count() = num_threads; });
  }

  /// @brief Access the singleton pool instance (created on first call).
  static auto
  instance() -> PoolType&
  {
    static PoolType pool(thread_count());
    return pool;
  }

  /// @name Forwarding wrappers
  /// All methods below simply forward to @c instance().method(...).
  /// @{

  template <typename F, typename... Args>
  static auto
  submit(F&& f, Args&&... args)
  {
    return instance().submit(std::forward<F>(f), std::forward<Args>(args)...);
  }

  template <typename F, typename... Args>
  static auto
  try_submit(F&& f, Args&&... args)
  {
    return instance().try_submit(std::forward<F>(f),
                                 std::forward<Args>(args)...);
  }

  template <typename F, typename... Args>
  static void
  post(F&& f, Args&&... args)
  {
    instance().post(std::forward<F>(f), std::forward<Args>(args)...);
  }

  template <typename F, typename... Args>
  static auto
  try_post(F&& f, Args&&... args)
  {
    return instance().try_post(std::forward<F>(f),
                               std::forward<Args>(args)...);
  }

  template <typename Iterator>
  static auto
  submit_batch(Iterator begin, Iterator end)
  {
    return instance().submit_batch(begin, end);
  }

  template <typename Iterator>
  static auto
  try_submit_batch(Iterator begin, Iterator end)
  {
    return instance().try_submit_batch(begin, end);
  }

  template <typename Iterator, typename F>
  static void
  parallel_for_each(Iterator begin, Iterator end, F&& func)
  {
    instance().parallel_for_each(begin, end, std::forward<F>(func));
  }

  /// @}

private:
  global_pool_backend() = default;

  static auto
  init_flag() -> std::once_flag&
  {
    static std::once_flag flag;
    return flag;
  }

  static auto
  thread_count() -> size_t&
  {
    static size_t count = std::thread::hardware_concurrency();
    return count;
  }
};

/**
 * @typedef global_thread_pool_backend
 * @brief Singleton accessor for the process-wide @c thread_pool_backend
 * instance.
 */
using global_thread_pool_backend = global_pool_backend<thread_pool_backend>;

/**
 * @typedef global_work_stealing_pool_backend
 * @brief Singleton accessor for the process-wide @ref
 * work_stealing_pool_backend instance.
 */
using global_work_stealing_pool_backend
    = global_pool_backend<work_stealing_pool_backend>;

/**
 * @brief Convenience wrapper that applies a callable to every element of a
 *        container in parallel using the @c global_thread_pool_backend
 * singleton.
 *
 * Equivalent to:
 * @code
 * global_thread_pool_backend::parallel_for_each(container.begin(),
 * container.end(), func);
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
void
parallel_for_each(Container& container, F&& func)
{
  global_thread_pool_backend::parallel_for_each(
      container.begin(), container.end(), std::forward<F>(func));
}

} // namespace threadschedule
