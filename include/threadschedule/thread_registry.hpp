#pragma once

/**
 * @file thread_registry.hpp
 * @brief Process-wide thread registry, control blocks, and composite registry.
 */

#include "callable.hpp"
#include "detail/thread_backend.hpp"
#include "expected.hpp"
#include "export.hpp"
#include "scheduler_policy.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <pthread.h>
#  include <sched.h>
#  include <sys/types.h>
#endif

namespace threadschedule
{

/**
 * @brief Snapshot of metadata for a single registered thread.
 *
 * This is a POD-like value type that captures thread identity, lifecycle
 * state, and an optional handle to the underlying thread_control_block.
 * Instances are returned by thread_registry_backend queries and are safe to
 * store, copy, and inspect from any thread.
 *
 * @par Thread safety
 * Instances are plain value types and carry no internal synchronisation.
 * Concurrent reads are safe; concurrent read/write on the *same* instance is
 * not.  The @c control shared_ptr is ref-counted and the pointee
 * (@ref thread_control_block) is itself thread-safe.
 *
 * @par Copyability / movability
 * Fully copyable and movable (regular value semantics).
 *
 * @par Lifetime
 * A registered_thread_info_backend is a *snapshot* - it may outlive the thread
 * it describes.  The @c alive flag reflects the state at the time the snapshot
 * was taken; it is **not** updated retroactively when the thread unregisters.
 *
 * @par Fields
 * - @c tid   - OS-level thread identifier (@c pid_t on Linux via
 *               @c gettid(), @c DWORD on Windows).
 * - @c std_id - The corresponding @c std::thread::id.
 * - @c name  - Human-readable name given at registration time.
 * - @c component - Optional logical grouping tag (e.g. "io", "compute").
 * - @c alive - @c true while the thread is registered; set to @c false when
 *               the thread calls @c unregister_current_thread().
 * - @c control - Shared pointer to the thread's @ref thread_control_block. May
 * be
 *                 @c nullptr if the thread was registered without a control
 *                 block (i.e. via the name-only overload of
 *                 @c register_current_thread()).
 */
struct registered_thread_info_backend
{
  native_thread_id tid{};
  std::thread::id std_id;
  std::string name;
  std::string component;
  bool alive{ true };
  std::shared_ptr<class thread_control_block> control;
};

using registry_callback
    = detail::copyable_callable<void(registered_thread_info_backend const&)>;

/**
 * @brief Per-thread control handle for OS-level scheduling operations.
 *
 * A thread_control_block captures the native thread handle (pthread_t on
 * Linux, a duplicated @c HANDLE on Windows) at construction time and exposes
 * cross-platform methods to modify the thread's affinity, priority,
 * scheduling policy, and OS-visible name.
 *
 * @par Creation
 * Always use the static factory create_for_current_thread().  It **must** be
 * called from the thread it will represent, because it snapshots
 * @c pthread_self() / @c GetCurrentThread().
 *
 * @par Ownership
 * thread_control_block is intended to be held via @c std::shared_ptr so that
 * the registry, the owning thread, and any observers can all share the same
 * instance.  The static factory already returns a @c shared_ptr.
 *
 * @par Thread safety
 * - The object is **not** copyable and **not** movable (identity type).
 * - All @c set_* methods are safe to call from **any** thread - they operate
 *   on the stored native handle, not on thread-local state.
 * - Concurrent calls to different @c set_* methods on the same instance are
 *   safe (each call is a single OS syscall on the stored handle).
 *
 * @par Platform notes
 * - **Linux**: stores @c pthread_t obtained via @c pthread_self().  No
 *   resource is owned; the handle is valid for the lifetime of the thread.
 * - **Windows**: duplicates the pseudo-handle returned by
 *   @c GetCurrentThread() into a real @c HANDLE with
 *   @c THREAD_SET_INFORMATION | @c THREAD_QUERY_INFORMATION rights.  The
 *   duplicated handle is closed in the destructor.
 *
 * @par Caveats
 * - Do **not** construct directly; always use create_for_current_thread().
 * - On Linux, @c set_name() enforces the 15-character POSIX limit and
 *   returns @c std::errc::invalid_argument if exceeded.
 */
class thread_control_block
{
public:
  thread_control_block() = default;
  thread_control_block(thread_control_block const&) = delete;
  auto operator=(thread_control_block const&)
      -> thread_control_block& = delete;
  thread_control_block(thread_control_block&&) = delete;
  auto operator=(thread_control_block&&) -> thread_control_block& = delete;

  ~thread_control_block()
  {
#ifdef _WIN32
    if (handle_)
      {
        CloseHandle(handle_);
        handle_ = nullptr;
      }
#endif
  }

  [[nodiscard]] auto
  tid() const noexcept -> native_thread_id
  {
    return tid_;
  }
  [[nodiscard]] auto
  std_id() const noexcept -> std::thread::id
  {
    return std_id_;
  }

private:
  [[nodiscard]] auto
  native_handle() const
  {
#ifdef _WIN32
    return handle_;
#else
    return pthread_handle_;
#endif
  }

public:
  [[nodiscard]] auto
  set_affinity(native_thread_affinity const& affinity) const
      -> expected<void, std::error_code>
  {
    return detail::apply_affinity(native_handle(), affinity);
  }

  [[nodiscard]] auto
  set_priority(native_thread_priority priority) const
      -> expected<void, std::error_code>
  {
    return detail::apply_priority(native_handle(), priority);
  }

  [[nodiscard]] auto
  set_nice_value(int nice_value) const -> expected<void, std::error_code>
  {
#ifdef _WIN32
    return detail::apply_nice_value(native_handle(), nice_value);
#else
    return detail::apply_nice_value(tid_, nice_value);
#endif
  }

  [[nodiscard]] auto
  get_nice_value() const -> expected<int, std::error_code>
  {
    return detail::read_effective_nice(native_handle(), tid_);
  }

  [[nodiscard]] auto
  set_scheduling_policy(native_scheduling_policy policy,
                        native_thread_priority priority) const
      -> expected<void, std::error_code>
  {
    return detail::apply_scheduling_policy(native_handle(), policy, priority);
  }

  [[nodiscard]] auto
  configure(native_scheduling_config const& config) const
      -> expected<void, std::error_code>
  {
    return detail::apply_scheduling_config(native_handle(), tid_, config);
  }

  [[nodiscard]] auto
  set_name(std::string const& name) const -> expected<void, std::error_code>
  {
    return detail::apply_name(native_handle(), name);
  }

  static auto
  create_for_current_thread() -> std::shared_ptr<thread_control_block>
  {
    auto block = std::make_shared<thread_control_block>();
    block->tid_ = thread_info::get_thread_id();
    block->std_id_ = std::this_thread::get_id();
#ifdef _WIN32
    HANDLE realHandle = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                    GetCurrentProcess(), &realHandle,
                    THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE,
                    0);
    block->handle_ = realHandle;
#else
    block->pthread_handle_ = pthread_self();
#endif
    return block;
  }

private:
  native_thread_id tid_{};
  std::thread::id std_id_;
#ifdef _WIN32
  HANDLE handle_ = nullptr;
#else
  pthread_t pthread_handle_{};
#endif
};

namespace detail
{

/**
 * @brief CRTP mixin that provides functional-style query facade methods.
 *
 * The derived class must implement a public @c query() method returning a
 * query_view-like object. All facade methods (filter, map, for_each,
 * find_if, any, all, none, take, skip, count, empty, apply) delegate to it.
 *
 * Return types are deduced via @c auto so the mixin can be used as a base
 * class before the concrete query_view type is fully defined (CRTP).
 *
 * @tparam Derived CRTP derived type.
 */
template <typename Derived>
class query_facade_mixin
{
  auto
  self() const -> Derived const&
  {
    return static_cast<Derived const&>(*this);
  }

public:
  template <typename Predicate>
  [[nodiscard]] auto
  filter(Predicate&& pred) const
  {
    return self().query().filter(std::forward<Predicate>(pred));
  }

  [[nodiscard]] auto
  count() const -> size_t
  {
    return self().query().count();
  }

  [[nodiscard]] auto
  empty() const -> bool
  {
    return self().query().empty();
  }

  template <typename Fn>
  void
  for_each(Fn&& fn) const
  {
    self().query().for_each(std::forward<Fn>(fn));
  }

  template <typename Predicate, typename Fn>
  void
  apply(Predicate&& pred, Fn&& fn) const
  {
    self()
        .query()
        .filter(std::forward<Predicate>(pred))
        .for_each(std::forward<Fn>(fn));
  }

  template <typename Fn>
  [[nodiscard]] auto
  map(Fn&& fn) const -> std::vector<
      std::invoke_result_t<Fn, registered_thread_info_backend const&>>
  {
    return self().query().map(std::forward<Fn>(fn));
  }

  template <typename Predicate>
  [[nodiscard]] auto
  find_if(Predicate&& pred) const
      -> std::optional<registered_thread_info_backend>
  {
    return self().query().find_if(std::forward<Predicate>(pred));
  }

  template <typename Predicate>
  [[nodiscard]] auto
  any(Predicate&& pred) const -> bool
  {
    return self().query().any(std::forward<Predicate>(pred));
  }

  template <typename Predicate>
  [[nodiscard]] auto
  all(Predicate&& pred) const -> bool
  {
    return self().query().all(std::forward<Predicate>(pred));
  }

  template <typename Predicate>
  [[nodiscard]] auto
  none(Predicate&& pred) const -> bool
  {
    return self().query().none(std::forward<Predicate>(pred));
  }

  [[nodiscard]] auto
  take(size_t n) const
  {
    return self().query().take(n);
  }

  [[nodiscard]] auto
  skip(size_t n) const
  {
    return self().query().skip(n);
  }
};

} // namespace detail

/**
 * @brief Central registry of threads indexed by OS-level thread ID
 * (native_thread_id).
 *
 * thread_registry_backend maintains a map of currently registered threads
 * together with their metadata and optional @ref thread_control_block handles.
 * It provides a functional-style query API (via @ref query_view) and
 * convenience methods that delegate scheduling operations to each thread's
 * control block.
 *
 * @par Thread safety
 * All public methods are thread-safe.  Internal state is protected by a
 * @c std::shared_mutex: mutating operations (register, unregister, set
 * callbacks) acquire a unique lock, while read-only operations (get, query,
 * set_affinity, etc.) acquire a shared lock.
 *
 * @par Copyability / movability
 * - **Not copyable** (copy constructor and assignment are deleted).
 * - **Not movable** (implicitly deleted because copy operations are deleted
 *   and the class holds a @c std::shared_mutex).
 *
 * @par Registration semantics
 * - register_current_thread() must be called **from** the thread being
 *   registered.  Duplicate registration of the same TID is silently ignored
 *   (the first registration wins).
 * - unregister_current_thread() removes the calling thread's entry and marks
 *   its @c alive flag as @c false in the snapshot passed to the callback.
 *
 * @par Callbacks
 * The optional @c onRegister / @c onUnregister callbacks are invoked **with
 * the lock released** to avoid deadlock if the callback itself interacts with
 * the registry.  The callback receives a copy of the @ref
 * registered_thread_info_backend.
 *
 * @par Querying
 * query() returns a @ref query_view holding a **snapshot** of the registry at
 * the moment of the call.  Subsequent changes to the registry (new
 * registrations, unregistrations) are not reflected in an existing @ref
 * query_view. The same functional-style helpers (filter, map, for_each, etc.)
 * are inherited from @ref detail::query_facade_mixin.
 *
 * @par Scheduling helpers
 * set_affinity(), set_priority(), set_scheduling_policy(), and set_name()
 * look up the @ref thread_control_block for the given TID under a shared lock
 * and delegate to the control block.  Returns @c std::errc::no_such_process if
 * the TID is not registered or has no control block.
 */
class thread_registry_backend
    : public detail::query_facade_mixin<thread_registry_backend>
{
public:
  thread_registry_backend() = default;
  thread_registry_backend(thread_registry_backend const&) = delete;
  auto operator=(thread_registry_backend const&)
      -> thread_registry_backend& = delete;

  void
  register_current_thread(std::string name = std::string(),
                          std::string component = std::string())
  {
    registered_thread_info_backend info;
    info.tid = thread_info::get_thread_id();
    info.std_id = std::this_thread::get_id();
    info.name = std::move(name);
    info.component = std::move(component);
    info.alive = true;
    try_register(std::move(info));
  }

  void
  register_current_thread(
      std::shared_ptr<thread_control_block> const& control_block,
      std::string name = std::string(), std::string component = std::string())
  {
    if (!control_block)
      return;
    registered_thread_info_backend info;
    info.tid = control_block->tid();
    info.std_id = control_block->std_id();
    info.name = std::move(name);
    info.component = std::move(component);
    info.alive = true;
    info.control = control_block;
    try_register(std::move(info));
  }

  void
  unregister_current_thread()
  {
    native_thread_id const tid = thread_info::get_thread_id();
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = threads_.find(tid);
    if (it != threads_.end())
      {
        it->second.alive = false;
        auto info = it->second;
        threads_.erase(it);
        if (on_unregister_)
          {
            auto cb = on_unregister_;
            lock.unlock();
            cb(info);
          }
      }
  }

  // Lookup
  [[nodiscard]] auto
  get(native_thread_id tid) const
      -> std::optional<registered_thread_info_backend>
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = threads_.find(tid);
    if (it == threads_.end())
      return std::nullopt;
    return it->second;
  }

  /**
   * @brief Lazy, functional-style query/filter view over a snapshot of
   *        registered threads.
   *
   * A query_view is produced by thread_registry_backend::query() (or by
   * chaining operations on an existing query_view).  It holds an internal
   * @c std::vector<registered_thread_info_backend> that is a **snapshot** -
   * mutations to the originating thread_registry_backend after the query_view
   * was created are not visible.
   *
   * @par Value semantics
   * query_view is a regular value type (copyable and movable).  All
   * transformation methods (filter, take, skip) return a **new** query_view,
   * leaving the original unchanged.
   *
   * @par Thread safety
   * A single query_view instance is **not** safe to use concurrently from
   * multiple threads.  However, it is safe to create multiple QueryViews
   * concurrently from the same @ref thread_registry_backend, since creation
   * acquires a shared lock on the registry.
   *
   * @par API
   * Provides a functional-style interface:
   * - **filter(pred)** - returns a new query_view containing only entries
   *   that satisfy @p pred.
   * - **map(fn)** - transforms each entry and returns a
   *   @c std::vector<R>.
   * - **for_each(fn)** - applies @p fn to every entry.
   * - **find_if(pred)** - returns the first matching entry, or
   *   @c std::nullopt.
   * - **any / all / none(pred)** - boolean aggregation predicates.
   * - **take(n) / skip(n)** - positional slicing, returning new
   *   QueryViews.
   * - **count() / empty()** - size queries.
   * - **entries()** - direct access to the underlying vector.
   */
  class query_view
  {
  public:
    explicit query_view(std::vector<registered_thread_info_backend> entries)
        : entries_(std::move(entries))
    {
    }

    template <typename Predicate>
    auto
    filter(Predicate&& pred) const -> query_view
    {
      std::vector<registered_thread_info_backend> filtered;
      filtered.reserve(entries_.size());
      for (auto const& entry : entries_)
        {
          if (pred(entry))
            filtered.push_back(entry);
        }
      return query_view(std::move(filtered));
    }

    template <typename Fn>
    void
    for_each(Fn&& fn) const
    {
      for (auto const& entry : entries_)
        {
          fn(entry);
        }
    }

    [[nodiscard]] auto
    count() const -> size_t
    {
      return entries_.size();
    }

    [[nodiscard]] auto
    empty() const -> bool
    {
      return entries_.empty();
    }

    [[nodiscard]] auto
    entries() const -> std::vector<registered_thread_info_backend> const&
    {
      return entries_;
    }

    // Transform entries to a vector of another type
    template <typename Fn>
    [[nodiscard]] auto
    map(Fn&& fn) const -> std::vector<
        std::invoke_result_t<Fn, registered_thread_info_backend const&>>
    {
      std::vector<
          std::invoke_result_t<Fn, registered_thread_info_backend const&>>
          result;
      result.reserve(entries_.size());
      for (auto const& entry : entries_)
        {
          result.push_back(fn(entry));
        }
      return result;
    }

    // Find first entry matching predicate
    template <typename Predicate>
    [[nodiscard]] auto
    find_if(Predicate&& pred) const
        -> std::optional<registered_thread_info_backend>
    {
      for (auto const& entry : entries_)
        {
          if (pred(entry))
            return entry;
        }
      return std::nullopt;
    }

    template <typename Predicate>
    [[nodiscard]] auto
    any(Predicate&& pred) const -> bool
    {
      for (auto const& entry : entries_)
        {
          if (pred(entry))
            return true;
        }
      return false;
    }

    template <typename Predicate>
    [[nodiscard]] auto
    all(Predicate&& pred) const -> bool
    {
      for (auto const& entry : entries_)
        {
          if (!pred(entry))
            return false;
        }
      return true;
    }

    template <typename Predicate>
    [[nodiscard]] auto
    none(Predicate&& pred) const -> bool
    {
      return !any(std::forward<Predicate>(pred));
    }

    [[nodiscard]] auto
    take(size_t n) const -> query_view
    {
      auto result = entries_;
      if (result.size() > n)
        result.resize(n);
      return query_view(std::move(result));
    }

    [[nodiscard]] auto
    skip(size_t n) const -> query_view
    {
      std::vector<registered_thread_info_backend> result;
      if (n < entries_.size())
        {
          result.assign(entries_.begin() + n, entries_.end());
        }
      return query_view(std::move(result));
    }

  private:
    std::vector<registered_thread_info_backend> entries_;
  };

  // Create a query view over all registered threads
  [[nodiscard]] auto
  query() const -> query_view
  {
    std::vector<registered_thread_info_backend> snapshot;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    snapshot.reserve(threads_.size());
    for (auto const& kv : threads_)
      {
        snapshot.push_back(kv.second);
      }
    return query_view(std::move(snapshot));
  }

  [[nodiscard]] auto
  set_affinity(native_thread_id tid,
               native_thread_affinity const& affinity) const
      -> expected<void, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->set_affinity(affinity);
  }

  [[nodiscard]] auto
  set_priority(native_thread_id tid, native_thread_priority priority) const
      -> expected<void, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->set_priority(priority);
  }

  [[nodiscard]] auto
  set_nice_value(native_thread_id tid, int nice_value) const
      -> expected<void, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->set_nice_value(nice_value);
  }

  [[nodiscard]] auto
  get_nice_value(native_thread_id tid) const -> expected<int, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->get_nice_value();
  }

  [[nodiscard]] auto
  set_scheduling_policy(native_thread_id tid, native_scheduling_policy policy,
                        native_thread_priority priority) const
      -> expected<void, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->set_scheduling_policy(policy, priority);
  }

  [[nodiscard]] auto
  configure(native_thread_id tid, native_scheduling_config const& config) const
      -> expected<void, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->configure(config);
  }

  [[nodiscard]] auto
  configure(native_thread_id tid, native_thread_config const& config) const
      -> expected<void, std::error_code>
  {
    if (!config.name.empty())
      {
        auto named = set_name(tid, config.name);
        if (!named)
          return unexpected(named.error());
      }
    auto scheduled = configure(tid, config.scheduling);
    if (!scheduled)
      return unexpected(scheduled.error());
    if (config.affinity.has_value())
      return set_affinity(tid, *config.affinity);
    return {};
  }

  [[nodiscard]] auto
  set_name(native_thread_id tid, std::string const& name) const
      -> expected<void, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->set_name(name);
  }

  // Register/unregister hooks (system integration)
  void
  set_on_register(registry_callback cb)
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    on_register_ = std::move(cb);
  }

  template <typename Callback,
            std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<Callback>,
                                             registry_callback>,
                             int> = 0>
  void
  set_on_register(Callback&& cb)
  {
    static_assert(std::is_invocable_r_v<void, Callback&,
                                        registered_thread_info_backend const&>,
                  "Register callback must be invocable with "
                  "registered_thread_info_backend "
                  "const&");
    std::unique_lock<std::shared_mutex> lock(mutex_);
    on_register_ = detail::make_copyable_callable<void(
        registered_thread_info_backend const&)>(std::forward<Callback>(cb));
  }

  void
  set_on_unregister(registry_callback cb)
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    on_unregister_ = std::move(cb);
  }

  template <typename Callback,
            std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<Callback>,
                                             registry_callback>,
                             int> = 0>
  void
  set_on_unregister(Callback&& cb)
  {
    static_assert(std::is_invocable_r_v<void, Callback&,
                                        registered_thread_info_backend const&>,
                  "Unregister callback must be invocable with "
                  "registered_thread_info_backend "
                  "const&");
    std::unique_lock<std::shared_mutex> lock(mutex_);
    on_unregister_ = detail::make_copyable_callable<void(
        registered_thread_info_backend const&)>(std::forward<Callback>(cb));
  }

private:
  void
  try_register(registered_thread_info_backend info)
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = threads_.find(info.tid);
    if (it != threads_.end())
      return;
    auto stored = info;
    threads_.emplace(info.tid, std::move(info));
    if (on_register_)
      {
        auto cb = on_register_;
        lock.unlock();
        cb(stored);
      }
  }

  [[nodiscard]] auto
  lock_block(native_thread_id tid) const
      -> std::shared_ptr<thread_control_block>
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = threads_.find(tid);
    if (it == threads_.end())
      return nullptr;
    return it->second.control;
  }
  mutable std::shared_mutex mutex_;
  std::unordered_map<native_thread_id, registered_thread_info_backend>
      threads_;

  registry_callback on_register_;
  registry_callback on_unregister_;
};

/**
 * @name Global registry access
 *
 * These free functions provide access to a process-wide @ref
 * thread_registry_backend singleton and allow injecting a custom instance.
 *
 * @par Header-only mode (default)
 * Both registry() and set_external_registry() are @c inline functions that
 * use function-local statics (Meyer's singleton pattern).  registry()
 * returns the externally set registry if one was provided via
 * set_external_registry(), otherwise a function-local static instance.
 *
 * @par Runtime / shared-library mode (@c THREADSCHEDULE_RUNTIME defined)
 * The functions are declared here but **defined** in
 * @c runtime_registry.cpp.  This ensures a single registry instance across
 * shared-library boundaries even when the header is included from multiple
 * translation units in different DSOs.
 *
 * @{
 */

namespace detail
{
#if defined(THREADSCHEDULE_RUNTIME)
THREADSCHEDULE_API auto runtime_registry() -> thread_registry_backend&;
THREADSCHEDULE_API void
runtime_set_external_registry(thread_registry_backend* reg);
#else
/** @cond INTERNAL */
inline auto
registry_storage() -> thread_registry_backend*&
{
  static thread_registry_backend* external = nullptr;
  return external;
}
/** @endcond */

inline auto
runtime_registry() -> thread_registry_backend&
{
  thread_registry_backend*& ext = registry_storage();
  if (ext != nullptr)
    return *ext;
  static thread_registry_backend local;
  return local;
}

inline void
runtime_set_external_registry(thread_registry_backend* reg)
{
  registry_storage() = reg;
}
#endif
} // namespace detail

#if defined(THREADSCHEDULE_RUNTIME)
THREADSCHEDULE_API auto registry() -> thread_registry_backend&;

THREADSCHEDULE_API void set_external_registry(thread_registry_backend* reg);
#else

/**
 * @brief Returns a reference to the process-wide @ref thread_registry_backend.
 *
 * If set_external_registry() was called with a non-null pointer, that
 * registry is returned.  Otherwise a function-local static instance is
 * used (Meyer's singleton; thread-safe initialisation guaranteed by C++11).
 *
 * @return Reference to the active @ref thread_registry_backend.
 */
inline auto
registry() -> thread_registry_backend&
{
  return detail::runtime_registry();
}

/**
 * @brief Injects a custom @ref thread_registry_backend as the global
 * singleton.
 *
 * After this call, registry() returns @p reg instead of the default
 * function-local static instance.  Pass @c nullptr to revert to the
 * built-in singleton.
 *
 * @param reg Pointer to the registry to use globally.  The caller must
 *            ensure @p reg remains valid for the lifetime of all threads
 *            that call registry().
 *
 * @warning Must be called **before** any threads are registered if the
 *          intent is to capture all threads in a single registry.
 *          Calling it after registrations have already occurred leaves
 *          those earlier entries in the old (default) registry.
 */
inline void
set_external_registry(thread_registry_backend* reg)
{
  detail::runtime_set_external_registry(reg);
}
/** @} */
#endif

/**
 * @brief Indicates whether the library was compiled in header-only or
 *        runtime (shared library) mode.
 *
 * The value is determined at compile time by the presence of the
 * @c THREADSCHEDULE_RUNTIME preprocessor macro.
 *
 * @see current_build_mode(), build_mode_string(), is_runtime_build
 */
enum class build_mode : std::uint8_t
{
  header_only, ///< All symbols are inline / header-only.
  runtime      ///< Core symbols are compiled into a shared library.
};

} // namespace threadschedule

namespace threadschedule
{

#if defined(THREADSCHEDULE_RUNTIME)
inline constexpr bool is_runtime_build
    = true; ///< @c true when compiled with @c THREADSCHEDULE_RUNTIME.

/**
 * @brief Returns the build mode detected at compile time (runtime variant).
 * @return build_mode::runtime.
 */
THREADSCHEDULE_API auto current_build_mode() -> build_mode;
#else
inline constexpr bool is_runtime_build
    = false; ///< @c true when compiled with @c THREADSCHEDULE_RUNTIME.

/**
 * @brief Returns the build mode detected at compile time (header-only
 * variant).
 * @return build_mode::header_only.
 */
inline auto
current_build_mode() -> build_mode
{
  return build_mode::header_only;
}
#endif

/**
 * @brief Returns a human-readable C string describing the active build mode.
 * @return @c "runtime" or @c "header-only".
 */
inline auto
build_mode_string() -> char const*
{
  return is_runtime_build ? "runtime" : "header-only";
}

/**
 * @brief Aggregates multiple thread_registry_backend instances into a single
 * queryable view.
 *
 * composite_thread_registry_backend is useful when threads are spread across
 * several independent @ref thread_registry_backend instances (e.g. one per
 * shared library) and you want a unified query interface over all of them.
 *
 * @par Thread safety
 * All public methods are thread-safe.  The internal list of attached
 * registries is protected by a @c std::mutex.
 *
 * @par Copyability / movability
 * Not copyable and not movable (holds a @c std::mutex).
 *
 * @par Ownership
 * attach() stores **raw pointers** to the supplied registries.  The caller
 * is responsible for ensuring that every attached thread_registry_backend
 * outlives this composite_thread_registry_backend.  Violating this results in
 * undefined behaviour.
 *
 * @par Deduplication
 * No deduplication is performed.  If the same TID appears in multiple
 * attached registries, it will appear multiple times in the merged
 * query_view.
 *
 * @par Querying
 * query() iterates over every attached registry, calls its own query(), and
 * concatenates the results into a single @ref
 * thread_registry_backend::query_view snapshot. The same functional-style
 * helpers (filter, map, for_each, etc.) are inherited from @ref
 * detail::query_facade_mixin.
 */
class composite_thread_registry_backend
    : public detail::query_facade_mixin<composite_thread_registry_backend>
{
public:
  void
  attach(thread_registry_backend* reg)
  {
    if (reg == nullptr)
      return;
    std::lock_guard<std::mutex> lock(mutex_);
    registries_.push_back(reg);
  }

  [[nodiscard]] auto
  query() const -> thread_registry_backend::query_view
  {
    std::vector<registered_thread_info_backend> merged;
    std::vector<thread_registry_backend*> regs;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      regs = registries_;
    }
    for (auto* r : regs)
      {
        auto view = r->query();
        auto const& entries = view.entries();
        merged.insert(merged.end(), entries.begin(), entries.end());
      }
    return thread_registry_backend::query_view(std::move(merged));
  }

private:
  mutable std::mutex mutex_;
  std::vector<thread_registry_backend*> registries_;
};

/**
 * @brief RAII guard that registers the current thread on construction and
 *        unregisters it on destruction.
 *
 * auto_register_current_thread creates a @ref thread_control_block for the
 * calling thread, sets its OS-visible name via
 * thread_control_block::set_name(), and registers it in either the global
 * registry() or a caller-supplied
 * @ref thread_registry_backend.
 *
 * @par Copyability / movability
 * - **Not copyable** (deleted).
 * - **Movable** - move construction / assignment transfers registration
 *   ownership to the new instance and disarms the source.
 *
 * @par Thread safety
 * Construction and destruction interact with the target
 * thread_registry_backend, which is itself thread-safe.  The guard object
 * itself must not be shared across threads without external synchronisation.
 *
 * @par Lifetime / ownership
 * - If constructed with a specific @c thread_registry_backend&, that registry
 * **must** outlive this guard.
 * - If constructed without an explicit registry, the global registry()
 *   singleton is used, which has static storage duration.
 *
 * @par Typical usage
 * @code
 * void worker_func() {
 *     threadschedule::auto_register_current_thread guard("worker", "pool");
 *     // ... thread body ...
 * }   // automatically unregistered here
 * @endcode
 *
 * @par Caveats
 * - Must be constructed **from** the thread it represents (delegates to
 *   thread_control_block::create_for_current_thread()).
 * - On Linux, the name must be at most 15 characters (POSIX thread name
 *   limit); longer names cause thread_control_block::set_name() to fail, but
 *   the thread is still registered.
 */
class auto_register_current_thread
{
public:
  explicit auto_register_current_thread(std::string const& name
                                        = std::string(),
                                        std::string const& component
                                        = std::string())
      : active_(true), external_registry_(nullptr)
  {
    auto block = thread_control_block::create_for_current_thread();
    (void)block->set_name(name);
    detail::runtime_registry().register_current_thread(block, name, component);
  }

  explicit auto_register_current_thread(thread_registry_backend& reg,
                                        std::string const& name
                                        = std::string(),
                                        std::string const& component
                                        = std::string())
      : active_(true), external_registry_(&reg)
  {
    auto block = thread_control_block::create_for_current_thread();
    (void)block->set_name(name);
    external_registry_->register_current_thread(block, name, component);
  }
  ~auto_register_current_thread()
  {
    if (active_)
      {
        if (external_registry_ != nullptr)
          external_registry_->unregister_current_thread();
        else
          detail::runtime_registry().unregister_current_thread();
      }
  }
  auto_register_current_thread(auto_register_current_thread const&) = delete;
  auto operator=(auto_register_current_thread const&)
      -> auto_register_current_thread& = delete;
  auto_register_current_thread(auto_register_current_thread&& other) noexcept
      : active_(other.active_), external_registry_(other.external_registry_)
  {
    other.active_ = false;
    other.external_registry_ = nullptr;
  }
  auto
  operator=(auto_register_current_thread&& other) noexcept
      -> auto_register_current_thread&
  {
    if (this != &other)
      {
        if (active_)
          {
            if (external_registry_ != nullptr)
              external_registry_->unregister_current_thread();
            else
              detail::runtime_registry().unregister_current_thread();
          }
        active_ = other.active_;
        external_registry_ = other.external_registry_;
        other.active_ = false;
        other.external_registry_ = nullptr;
      }
    return *this;
  }

private:
  bool active_{ false };
  thread_registry_backend* external_registry_{ nullptr };
};

} // namespace threadschedule

#ifndef _WIN32
namespace threadschedule
{
/**
 * @brief Attaches a thread to a Linux cgroup by writing its TID to the
 *        appropriate control file.
 *
 * Tries the following files inside @p cgroup_dir, in order:
 * 1. @c cgroup.threads (cgroup v2)
 * 2. @c tasks (cgroup v1 / hybrid)
 * 3. @c cgroup.procs (cgroup v2 process-level; works for single-threaded
 *    workloads)
 *
 * The first file that can be opened and written to successfully is used.
 *
 * @param cgroup_dir Absolute path to the target cgroup directory
 *                  (e.g. @c "/sys/fs/cgroup/my_group").
 * @param tid       OS-level thread ID to attach.
 * @return Success, or @c std::errc::operation_not_permitted if none of the
 *         candidate files could be written.
 *
 * @note Linux-only.  This function is not available on Windows builds.
 * @note The calling process needs appropriate permissions (typically
 *       @c CAP_SYS_ADMIN or ownership of the cgroup directory) to write
 *       to cgroup control files.
 */
inline auto
cgroup_attach_tid(std::string const& cgroup_dir, native_thread_id tid)
    -> expected<void, std::error_code>
{
  std::vector<std::string> candidates
      = { "cgroup.threads", "tasks", "cgroup.procs" };
  for (auto const& file : candidates)
    {
      std::string path = cgroup_dir + "/" + file;
      std::ofstream out(path);
      if (!out)
        continue;
      out << tid;
      out.flush();
      if (out)
        return {};
    }
  return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}
} // namespace threadschedule
#endif
