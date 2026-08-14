#pragma once

/** @file detail/registry/thread_registry_backend.hpp
 *  @brief Registry storage, mutation, snapshots, and private runtime hooks.
 */

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
class thread_registry_backend : public detail::query_facade_mixin<thread_registry_backend>
{
public:
  thread_registry_backend() = default;
  ~thread_registry_backend()
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto& entry : threads_)
      if (entry.second.control)
        entry.second.control->deactivate();
  }
  thread_registry_backend(thread_registry_backend const&) = delete;
  auto operator=(thread_registry_backend const&) -> thread_registry_backend& = delete;

  void
  register_current_thread(std::string name = std::string(), std::string component = std::string())
  {
    registered_thread_info_backend info;
    info.tid = thread_info::get_thread_id();
    info.std_id = std::this_thread::get_id();
    info.name = std::move(name);
    info.component = std::move(component);
    info.alive = true;
    (void)try_register(std::move(info));
  }

  void
  register_current_thread(std::shared_ptr<thread_control_block> const& control_block, std::string name = std::string(),
                          std::string component = std::string())
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
    (void)try_register(std::move(info));
  }

  void
  unregister_current_thread()
  {
    unregister_thread(thread_info::get_thread_id());
  }

private:
  void
  unregister_thread(native_thread_id tid, thread_control_block const* expected_control = nullptr) noexcept
  {
    try
      {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = threads_.find(tid);
        if (it == threads_.end() || (expected_control != nullptr && it->second.control.get() != expected_control))
          return;

        auto info = std::move(it->second);
        info.alive = false;
        if (info.control)
          info.control->deactivate();
        threads_.erase(it);

        registry_callback cb;
        if (on_unregister_)
          {
            try
              {
                cb = on_unregister_;
              }
            catch (...)
              {
              }
          }
        lock.unlock();
        if (cb)
          {
            try
              {
                cb(info);
              }
            catch (...)
              {
              }
          }
      }
    catch (...)
      {
      }
  }

public:
  // Lookup
  [[nodiscard]] auto
  get(native_thread_id tid) const -> std::optional<registered_thread_info_backend>
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
    explicit query_view(std::vector<registered_thread_info_backend> entries) : entries_(std::move(entries)) {}

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
    map(Fn&& fn) const -> std::vector<std::invoke_result_t<Fn, registered_thread_info_backend const&>>
    {
      std::vector<std::invoke_result_t<Fn, registered_thread_info_backend const&>> result;
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
    find_if(Predicate&& pred) const -> std::optional<registered_thread_info_backend>
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
  set_affinity(native_thread_id tid, native_thread_affinity const& affinity) const -> expected<void, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->set_affinity(affinity);
  }

  [[nodiscard]] auto
  set_priority(native_thread_id tid, native_thread_priority priority) const -> expected<void, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->set_priority(priority);
  }

  [[nodiscard]] auto
  set_nice_value(native_thread_id tid, int nice_value) const -> expected<void, std::error_code>
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
  set_scheduling_policy(native_thread_id tid, native_scheduling_policy policy, native_thread_priority priority) const
      -> expected<void, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->set_scheduling_policy(policy, priority);
  }

  [[nodiscard]] auto
  configure(native_thread_id tid, native_scheduling_config const& config) const -> expected<void, std::error_code>
  {
    auto blk = lock_block(tid);
    if (!blk)
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return blk->configure(config);
  }

  [[nodiscard]] auto
  configure(native_thread_id tid, native_thread_config const& config) const -> expected<void, std::error_code>
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
  set_name(native_thread_id tid, std::string const& name) const -> expected<void, std::error_code>
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
            std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<Callback>, registry_callback>, int> = 0>
  void
  set_on_register(Callback&& cb)
  {
    static_assert(std::is_invocable_r_v<void, Callback&, registered_thread_info_backend const&>,
                  "Register callback must be invocable with "
                  "registered_thread_info_backend "
                  "const&");
    std::unique_lock<std::shared_mutex> lock(mutex_);
    on_register_
        = detail::make_copyable_callable<void(registered_thread_info_backend const&)>(std::forward<Callback>(cb));
  }

  void
  set_on_unregister(registry_callback cb)
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    on_unregister_ = std::move(cb);
  }

  template <typename Callback,
            std::enable_if_t<!std::is_same_v<detail::remove_cvref_t<Callback>, registry_callback>, int> = 0>
  void
  set_on_unregister(Callback&& cb)
  {
    static_assert(std::is_invocable_r_v<void, Callback&, registered_thread_info_backend const&>,
                  "Unregister callback must be invocable with "
                  "registered_thread_info_backend "
                  "const&");
    std::unique_lock<std::shared_mutex> lock(mutex_);
    on_unregister_
        = detail::make_copyable_callable<void(registered_thread_info_backend const&)>(std::forward<Callback>(cb));
  }

private:
  [[nodiscard]] auto
  try_register(registered_thread_info_backend info) -> bool
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = threads_.find(info.tid);
    if (it != threads_.end())
      return false;
    auto stored = info;
    threads_.emplace(info.tid, std::move(info));
    if (on_register_)
      {
        registry_callback cb;
        try
          {
            cb = on_register_;
          }
        catch (...)
          {
          }
        lock.unlock();
        if (cb)
          {
            try
              {
                cb(stored);
              }
            catch (...)
              {
              }
          }
      }
    return true;
  }

  [[nodiscard]] auto
  register_guard(std::shared_ptr<thread_control_block> const& control_block, std::string const& name,
                 std::string const& component) -> bool
  {
    if (!control_block)
      return false;
    registered_thread_info_backend info;
    info.tid = control_block->tid();
    info.std_id = control_block->std_id();
    info.name = name;
    info.component = component;
    info.alive = true;
    info.control = control_block;
    return try_register(std::move(info));
  }

  [[nodiscard]] auto
  lock_block(native_thread_id tid) const -> std::shared_ptr<thread_control_block>
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = threads_.find(tid);
    if (it == threads_.end())
      return nullptr;
    return it->second.control;
  }
  mutable std::shared_mutex mutex_;
  std::unordered_map<native_thread_id, registered_thread_info_backend> threads_;

  registry_callback on_register_;
  registry_callback on_unregister_;

  friend class registration_guard_backend;
  friend class ::threadschedule::auto_register_current_thread;
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

#if defined(THREADSCHEDULE_RUNTIME)
THREADSCHEDULE_API auto runtime_registry() -> thread_registry_backend&;
THREADSCHEDULE_API void runtime_set_external_registry(thread_registry_backend* reg);
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
