#pragma once

#include "expected.hpp"
#include "scheduler_policy.hpp"
#include "thread_wrapper.hpp" // for ThreadInfo, ThreadAffinity
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
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <sys/types.h>
#endif

namespace threadschedule
{

// Optional export macro for building a runtime (shared/dll) variant
#if defined(_WIN32) || defined(_WIN64)
#if defined(THREADSCHEDULE_EXPORTS)
#define THREADSCHEDULE_API __declspec(dllexport)
#else
#define THREADSCHEDULE_API __declspec(dllimport)
#endif
#else
#define THREADSCHEDULE_API
#endif

#ifdef _WIN32
using Tid = unsigned long; // DWORD thread id
#else
using Tid = pid_t; // Linux TID via gettid()
#endif

/**
 * @brief Snapshot of metadata for a single registered thread.
 *
 * This is a POD-like value type that captures thread identity, lifecycle state,
 * and an optional handle to the underlying ThreadControlBlock.  Instances are
 * returned by ThreadRegistry queries and are safe to store, copy, and inspect
 * from any thread.
 *
 * @par Thread safety
 * Instances are plain value types and carry no internal synchronisation.
 * Concurrent reads are safe; concurrent read/write on the *same* instance is
 * not.  The @c control shared_ptr is ref-counted and the pointee
 * (@ref ThreadControlBlock) is itself thread-safe.
 *
 * @par Copyability / movability
 * Fully copyable and movable (regular value semantics).
 *
 * @par Lifetime
 * A RegisteredThreadInfo is a *snapshot* -- it may outlive the thread it
 * describes.  The @c alive flag reflects the state at the time the snapshot
 * was taken; it is **not** updated retroactively when the thread unregisters.
 *
 * @par Fields
 * - @c tid   -- OS-level thread identifier (@c pid_t on Linux via
 *               @c gettid(), @c DWORD on Windows).
 * - @c stdId -- The corresponding @c std::thread::id.
 * - @c name  -- Human-readable name given at registration time.
 * - @c componentTag -- Optional logical grouping tag (e.g. "io", "compute").
 * - @c alive -- @c true while the thread is registered; set to @c false when
 *               the thread calls @c unregister_current_thread().
 * - @c control -- Shared pointer to the thread's @ref ThreadControlBlock.  May be
 *                 @c nullptr if the thread was registered without a control
 *                 block (i.e. via the name-only overload of
 *                 @c register_current_thread()).
 */
struct RegisteredThreadInfo
{
    Tid tid{};
    std::thread::id stdId;
    std::string name;
    std::string componentTag;
    bool alive{true};
    std::shared_ptr<class ThreadControlBlock> control;
};

/**
 * @brief Per-thread control handle for OS-level scheduling operations.
 *
 * A ThreadControlBlock captures the native thread handle (pthread_t on Linux,
 * a duplicated @c HANDLE on Windows) at construction time and exposes
 * cross-platform methods to modify the thread's affinity, priority,
 * scheduling policy, and OS-visible name.
 *
 * @par Creation
 * Always use the static factory create_for_current_thread().  It **must** be
 * called from the thread it will represent, because it snapshots
 * @c pthread_self() / @c GetCurrentThread().
 *
 * @par Ownership
 * ThreadControlBlock is intended to be held via @c std::shared_ptr so that
 * the registry, the owning thread, and any observers can all share the same
 * instance.  The static factory already returns a @c shared_ptr.
 *
 * @par Thread safety
 * - The object is **not** copyable and **not** movable (identity type).
 * - All @c set_* methods are safe to call from **any** thread -- they operate
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
class ThreadControlBlock
{
  public:
    ThreadControlBlock() = default;
    ThreadControlBlock(ThreadControlBlock const&) = delete;
    auto operator=(ThreadControlBlock const&) -> ThreadControlBlock& = delete;
    ThreadControlBlock(ThreadControlBlock&&) = delete;
    auto operator=(ThreadControlBlock&&) -> ThreadControlBlock& = delete;

    ~ThreadControlBlock()
    {
#ifdef _WIN32
        if (handle_)
        {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
#endif
    }

    [[nodiscard]] auto tid() const noexcept -> Tid
    {
        return tid_;
    }
    [[nodiscard]] auto std_id() const noexcept -> std::thread::id
    {
        return stdId_;
    }
    // Removed name/component metadata from control block; metadata lives in RegisteredThreadInfo

    [[nodiscard]] auto set_affinity(ThreadAffinity const& affinity) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        return detail::apply_affinity(handle_, affinity);
#else
        return detail::apply_affinity(pthreadHandle_, affinity);
#endif
    }

    [[nodiscard]] auto set_priority(ThreadPriority priority) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        return detail::apply_priority(handle_, priority);
#else
        return detail::apply_priority(pthreadHandle_, priority);
#endif
    }

    [[nodiscard]] auto set_scheduling_policy(SchedulingPolicy policy, ThreadPriority priority) const
        -> expected<void, std::error_code>
    {
#ifdef _WIN32
        return detail::apply_scheduling_policy(handle_, policy, priority);
#else
        return detail::apply_scheduling_policy(pthreadHandle_, policy, priority);
#endif
    }

    [[nodiscard]] auto set_name(std::string const& name) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        return detail::apply_name(handle_, name);
#else
        return detail::apply_name(pthreadHandle_, name);
#endif
    }

    static auto create_for_current_thread() -> std::shared_ptr<ThreadControlBlock>
    {
        auto block = std::make_shared<ThreadControlBlock>();
        block->tid_ = ThreadInfo::get_thread_id();
        block->stdId_ = std::this_thread::get_id();
#ifdef _WIN32
        HANDLE realHandle = nullptr;
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &realHandle,
                        THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, 0);
        block->handle_ = realHandle;
#else
        block->pthreadHandle_ = pthread_self();
#endif
        return block;
    }

  private:
    Tid tid_{};
    std::thread::id stdId_;
#ifdef _WIN32
    HANDLE handle_ = nullptr;
#else
    pthread_t pthreadHandle_{};
#endif
};

/**
 * @brief Central registry of threads indexed by OS-level thread ID (Tid).
 *
 * ThreadRegistry maintains a map of currently registered threads together
 * with their metadata and optional @ref ThreadControlBlock handles.  It provides
 * a functional-style query API (via @ref QueryView) and convenience methods that
 * delegate scheduling operations to each thread's control block.
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
 * the registry.  The callback receives a copy of the @ref RegisteredThreadInfo.
 *
 * @par Querying
 * query() returns a @ref QueryView holding a **snapshot** of the registry at the
 * moment of the call.  Subsequent changes to the registry (new
 * registrations, unregistrations) are not reflected in an existing @ref QueryView.
 *
 * @par Scheduling helpers
 * set_affinity(), set_priority(), set_scheduling_policy(), and set_name()
 * look up the @ref ThreadControlBlock for the given TID under a shared lock and
 * delegate to the control block.  Returns @c std::errc::no_such_process if
 * the TID is not registered or has no control block.
 */
class ThreadRegistry
{
  public:
    ThreadRegistry() = default;
    ThreadRegistry(ThreadRegistry const&) = delete;
    auto operator=(ThreadRegistry const&) -> ThreadRegistry& = delete;

    // Register/unregister the CURRENT thread (to be called inside the running thread)
    void register_current_thread(std::string name = std::string(), std::string componentTag = std::string())
    {
        Tid const tid = ThreadInfo::get_thread_id();
        RegisteredThreadInfo info;
        info.tid = tid;
        info.stdId = std::this_thread::get_id();
        info.name = std::move(name);
        info.componentTag = std::move(componentTag);
        info.alive = true;

        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            auto it = threads_.find(tid);
            if (it == threads_.end())
            {
                auto stored = info; // copy for callback
                threads_.emplace(tid, std::move(info));
                if (onRegister_)
                {
                    auto cb = onRegister_;
                    lock.unlock();
                    cb(stored);
                }
            }
            else
            {
                // Duplicate registration of the same TID is a no-op (first registration wins)
            }
        }
    }

    void register_current_thread(std::shared_ptr<ThreadControlBlock> const& controlBlock,
                                 std::string name = std::string(), std::string componentTag = std::string())
    {
        if (!controlBlock)
            return;
        RegisteredThreadInfo info;
        info.tid = controlBlock->tid();
        info.stdId = controlBlock->std_id();
        info.name = std::move(name);
        info.componentTag = std::move(componentTag);
        info.alive = true;
        info.control = controlBlock;
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = threads_.find(info.tid);
        if (it == threads_.end())
        {
            auto stored = info; // copy for callback
            threads_.emplace(info.tid, std::move(info));
            if (onRegister_)
            {
                auto cb = onRegister_;
                lock.unlock();
                cb(stored);
            }
        }
        else
        {
            // Duplicate registration of the same TID is a no-op (first registration wins)
        }
    }

    void unregister_current_thread()
    {
        Tid const tid = ThreadInfo::get_thread_id();
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = threads_.find(tid);
        if (it != threads_.end())
        {
            it->second.alive = false;
            auto info = it->second;
            threads_.erase(it);
            if (onUnregister_)
            {
                auto cb = onUnregister_;
                lock.unlock();
                cb(info);
            }
        }
    }

    // Lookup
    [[nodiscard]] auto get(Tid tid) const -> std::optional<RegisteredThreadInfo>
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
     * A QueryView is produced by ThreadRegistry::query() (or by chaining
     * operations on an existing QueryView).  It holds an internal
     * @c std::vector<RegisteredThreadInfo> that is a **snapshot** -- mutations
     * to the originating ThreadRegistry after the QueryView was created are
     * not visible.
     *
     * @par Value semantics
     * QueryView is a regular value type (copyable and movable).  All
     * transformation methods (filter, take, skip) return a **new** QueryView,
     * leaving the original unchanged.
     *
     * @par Thread safety
     * A single QueryView instance is **not** safe to use concurrently from
     * multiple threads.  However, it is safe to create multiple QueryViews
     * concurrently from the same @ref ThreadRegistry, since creation acquires a
     * shared lock on the registry.
     *
     * @par API
     * Provides a functional-style interface:
     * - **filter(pred)** -- returns a new QueryView containing only entries
     *   that satisfy @p pred.
     * - **map(fn)** -- transforms each entry and returns a
     *   @c std::vector<R>.
     * - **for_each(fn)** -- applies @p fn to every entry.
     * - **find_if(pred)** -- returns the first matching entry, or
     *   @c std::nullopt.
     * - **any / all / none(pred)** -- boolean aggregation predicates.
     * - **take(n) / skip(n)** -- positional slicing, returning new
     *   QueryViews.
     * - **count() / empty()** -- size queries.
     * - **entries()** -- direct access to the underlying vector.
     */
    class QueryView
    {
      public:
        explicit QueryView(std::vector<RegisteredThreadInfo> entries) : entries_(std::move(entries))
        {
        }

        template <typename Predicate>
        auto filter(Predicate&& pred) const -> QueryView
        {
            std::vector<RegisteredThreadInfo> filtered;
            filtered.reserve(entries_.size());
            for (auto const& entry : entries_)
            {
                if (pred(entry))
                    filtered.push_back(entry);
            }
            return QueryView(std::move(filtered));
        }

        template <typename Fn>
        void for_each(Fn&& fn) const
        {
            for (auto const& entry : entries_)
            {
                fn(entry);
            }
        }

        [[nodiscard]] auto count() const -> size_t
        {
            return entries_.size();
        }

        [[nodiscard]] auto empty() const -> bool
        {
            return entries_.empty();
        }

        [[nodiscard]] auto entries() const -> std::vector<RegisteredThreadInfo> const&
        {
            return entries_;
        }

        // Transform entries to a vector of another type
        template <typename Fn>
        [[nodiscard]] auto map(Fn&& fn) const -> std::vector<std::invoke_result_t<Fn, RegisteredThreadInfo const&>>
        {
            std::vector<std::invoke_result_t<Fn, RegisteredThreadInfo const&>> result;
            result.reserve(entries_.size());
            for (auto const& entry : entries_)
            {
                result.push_back(fn(entry));
            }
            return result;
        }

        // Find first entry matching predicate
        template <typename Predicate>
        [[nodiscard]] auto find_if(Predicate&& pred) const -> std::optional<RegisteredThreadInfo>
        {
            for (auto const& entry : entries_)
            {
                if (pred(entry))
                    return entry;
            }
            return std::nullopt;
        }

        template <typename Predicate>
        [[nodiscard]] auto any(Predicate&& pred) const -> bool
        {
            for (auto const& entry : entries_)
            {
                if (pred(entry))
                    return true;
            }
            return false;
        }

        template <typename Predicate>
        [[nodiscard]] auto all(Predicate&& pred) const -> bool
        {
            for (auto const& entry : entries_)
            {
                if (!pred(entry))
                    return false;
            }
            return true;
        }

        template <typename Predicate>
        [[nodiscard]] auto none(Predicate&& pred) const -> bool
        {
            return !any(std::forward<Predicate>(pred));
        }

        [[nodiscard]] auto take(size_t n) const -> QueryView
        {
            auto result = entries_;
            if (result.size() > n)
                result.resize(n);
            return QueryView(std::move(result));
        }

        [[nodiscard]] auto skip(size_t n) const -> QueryView
        {
            std::vector<RegisteredThreadInfo> result;
            if (n < entries_.size())
            {
                result.assign(entries_.begin() + n, entries_.end());
            }
            return QueryView(std::move(result));
        }

      private:
        std::vector<RegisteredThreadInfo> entries_;
    };

    // Create a query view over all registered threads
    [[nodiscard]] auto query() const -> QueryView
    {
        std::vector<RegisteredThreadInfo> snapshot;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        snapshot.reserve(threads_.size());
        for (auto const& kv : threads_)
        {
            snapshot.push_back(kv.second);
        }
        return QueryView(std::move(snapshot));
    }

    template <typename Predicate>
    [[nodiscard]] auto filter(Predicate&& pred) const -> QueryView
    {
        return query().filter(std::forward<Predicate>(pred));
    }

    [[nodiscard]] auto count() const -> size_t
    {
        return query().count();
    }

    [[nodiscard]] auto empty() const -> bool
    {
        return query().empty();
    }

    template <typename Fn>
    void for_each(Fn&& fn) const
    {
        query().for_each(std::forward<Fn>(fn));
    }

    template <typename Predicate, typename Fn>
    void apply(Predicate&& pred, Fn&& fn) const
    {
        query().filter(std::forward<Predicate>(pred)).for_each(std::forward<Fn>(fn));
    }

    template <typename Fn>
    [[nodiscard]] auto map(Fn&& fn) const -> std::vector<std::invoke_result_t<Fn, RegisteredThreadInfo const&>>
    {
        return query().map(std::forward<Fn>(fn));
    }

    template <typename Predicate>
    [[nodiscard]] auto find_if(Predicate&& pred) const -> std::optional<RegisteredThreadInfo>
    {
        return query().find_if(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto any(Predicate&& pred) const -> bool
    {
        return query().any(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto all(Predicate&& pred) const -> bool
    {
        return query().all(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto none(Predicate&& pred) const -> bool
    {
        return query().none(std::forward<Predicate>(pred));
    }

    [[nodiscard]] auto take(size_t n) const -> QueryView
    {
        return query().take(n);
    }

    [[nodiscard]] auto skip(size_t n) const -> QueryView
    {
        return query().skip(n);
    }

    [[nodiscard]] auto set_affinity(Tid tid, ThreadAffinity const& affinity) const -> expected<void, std::error_code>
    {
        auto blk = lock_block(tid);
        if (!blk)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return blk->set_affinity(affinity);
    }

    [[nodiscard]] auto set_priority(Tid tid, ThreadPriority priority) const -> expected<void, std::error_code>
    {
        auto blk = lock_block(tid);
        if (!blk)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return blk->set_priority(priority);
    }

    [[nodiscard]] auto set_scheduling_policy(Tid tid, SchedulingPolicy policy, ThreadPriority priority) const
        -> expected<void, std::error_code>
    {
        auto blk = lock_block(tid);
        if (!blk)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return blk->set_scheduling_policy(policy, priority);
    }

    [[nodiscard]] auto set_name(Tid tid, std::string const& name) const -> expected<void, std::error_code>
    {
        auto blk = lock_block(tid);
        if (!blk)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return blk->set_name(name);
    }

    // Register/unregister hooks (system integration)
    void set_on_register(std::function<void(RegisteredThreadInfo const&)> cb)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        onRegister_ = std::move(cb);
    }

    void set_on_unregister(std::function<void(RegisteredThreadInfo const&)> cb)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        onUnregister_ = std::move(cb);
    }

  private:
    [[nodiscard]] auto lock_block(Tid tid) const -> std::shared_ptr<ThreadControlBlock>
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = threads_.find(tid);
        if (it == threads_.end())
            return nullptr;
        return it->second.control;
    }
    mutable std::shared_mutex mutex_;
    std::unordered_map<Tid, RegisteredThreadInfo> threads_;

    // Integration hooks
    std::function<void(RegisteredThreadInfo const&)> onRegister_;
    std::function<void(RegisteredThreadInfo const&)> onUnregister_;
};

/**
 * @name Global registry access
 *
 * These free functions provide access to a process-wide @ref ThreadRegistry
 * singleton and allow injecting a custom instance.
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
THREADSCHEDULE_API auto registry() -> ThreadRegistry&;
THREADSCHEDULE_API void set_external_registry(ThreadRegistry* reg);
#else
/** @cond INTERNAL */
inline auto registry_storage() -> ThreadRegistry*&
{
    static ThreadRegistry* external = nullptr;
    return external;
}
/** @endcond */

/**
 * @brief Returns a reference to the process-wide @ref ThreadRegistry.
 *
 * If set_external_registry() was called with a non-null pointer, that
 * registry is returned.  Otherwise a function-local static instance is
 * used (Meyer's singleton; thread-safe initialisation guaranteed by C++11).
 *
 * @return Reference to the active @ref ThreadRegistry.
 */
inline auto registry() -> ThreadRegistry&
{
    ThreadRegistry*& ext = registry_storage();
    if (ext != nullptr)
        return *ext;
    static ThreadRegistry local;
    return local;
}

/**
 * @brief Injects a custom @ref ThreadRegistry as the global singleton.
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
inline void set_external_registry(ThreadRegistry* reg)
{
    registry_storage() = reg;
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
 * @see build_mode(), build_mode_string(), is_runtime_build
 */
enum class BuildMode : std::uint8_t
{
    HEADER_ONLY, ///< All symbols are inline / header-only.
    RUNTIME      ///< Core symbols are compiled into a shared library.
};

#if defined(THREADSCHEDULE_RUNTIME)
inline constexpr bool is_runtime_build = true; ///< @c true when compiled with @c THREADSCHEDULE_RUNTIME.

/**
 * @brief Returns the build mode detected at compile time (runtime variant).
 * @return BuildMode::RUNTIME.
 */
THREADSCHEDULE_API auto build_mode() -> BuildMode;
#else
inline constexpr bool is_runtime_build = false; ///< @c true when compiled with @c THREADSCHEDULE_RUNTIME.

/**
 * @brief Returns the build mode detected at compile time (header-only variant).
 * @return BuildMode::HEADER_ONLY.
 */
inline auto build_mode() -> BuildMode
{
    return BuildMode::HEADER_ONLY;
}
#endif

/**
 * @brief Returns a human-readable C string describing the active build mode.
 * @return @c "runtime" or @c "header-only".
 */
inline auto build_mode_string() -> char const*
{
    return is_runtime_build ? "runtime" : "header-only";
}

namespace detail
{

/**
 * @brief CRTP mixin that provides functional-style query facade methods.
 *
 * The derived class must implement a public @c query() method returning a
 * @ref ThreadRegistry::QueryView. All facade methods (filter, map, for_each,
 * find_if, any, all, none, take, skip, count, empty, apply) delegate to it.
 *
 * @tparam Derived CRTP derived type.
 */
template <typename Derived>
class QueryFacadeMixin
{
    auto self() const -> Derived const& { return static_cast<Derived const&>(*this); }

  public:
    template <typename Predicate>
    [[nodiscard]] auto filter(Predicate&& pred) const -> ThreadRegistry::QueryView
    {
        return self().query().filter(std::forward<Predicate>(pred));
    }

    [[nodiscard]] auto count() const -> size_t { return self().query().count(); }

    [[nodiscard]] auto empty() const -> bool { return self().query().empty(); }

    template <typename Fn>
    void for_each(Fn&& fn) const
    {
        self().query().for_each(std::forward<Fn>(fn));
    }

    template <typename Predicate, typename Fn>
    void apply(Predicate&& pred, Fn&& fn) const
    {
        self().query().filter(std::forward<Predicate>(pred)).for_each(std::forward<Fn>(fn));
    }

    template <typename Fn>
    [[nodiscard]] auto map(Fn&& fn) const -> std::vector<std::invoke_result_t<Fn, RegisteredThreadInfo const&>>
    {
        return self().query().map(std::forward<Fn>(fn));
    }

    template <typename Predicate>
    [[nodiscard]] auto find_if(Predicate&& pred) const -> std::optional<RegisteredThreadInfo>
    {
        return self().query().find_if(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto any(Predicate&& pred) const -> bool
    {
        return self().query().any(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto all(Predicate&& pred) const -> bool
    {
        return self().query().all(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto none(Predicate&& pred) const -> bool
    {
        return self().query().none(std::forward<Predicate>(pred));
    }

    [[nodiscard]] auto take(size_t n) const -> ThreadRegistry::QueryView
    {
        return self().query().take(n);
    }

    [[nodiscard]] auto skip(size_t n) const -> ThreadRegistry::QueryView
    {
        return self().query().skip(n);
    }
};

} // namespace detail

/**
 * @brief Aggregates multiple ThreadRegistry instances into a single queryable
 *        view.
 *
 * CompositeThreadRegistry is useful when threads are spread across several
 * independent @ref ThreadRegistry instances (e.g. one per shared library) and you
 * want a unified query interface over all of them.
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
 * is responsible for ensuring that every attached ThreadRegistry outlives this
 * CompositeThreadRegistry.  Violating this results in undefined behaviour.
 *
 * @par Deduplication
 * No deduplication is performed.  If the same TID appears in multiple
 * attached registries, it will appear multiple times in the merged
 * QueryView.
 *
 * @par Querying
 * query() iterates over every attached registry, calls its own query(), and
 * concatenates the results into a single @ref ThreadRegistry::QueryView snapshot.
 * The same functional-style helpers (filter, map, for_each, etc.) are
 * inherited from @ref detail::QueryFacadeMixin.
 */
class CompositeThreadRegistry : public detail::QueryFacadeMixin<CompositeThreadRegistry>
{
  public:
    void attach(ThreadRegistry* reg)
    {
        if (reg == nullptr)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        registries_.push_back(reg);
    }

    [[nodiscard]] auto query() const -> ThreadRegistry::QueryView
    {
        std::vector<RegisteredThreadInfo> merged;
        std::vector<ThreadRegistry*> regs;
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
        return ThreadRegistry::QueryView(std::move(merged));
    }

  private:
    mutable std::mutex mutex_;
    std::vector<ThreadRegistry*> registries_;
};

/**
 * @brief RAII guard that registers the current thread on construction and
 *        unregisters it on destruction.
 *
 * AutoRegisterCurrentThread creates a @ref ThreadControlBlock for the calling
 * thread, sets its OS-visible name via ThreadControlBlock::set_name(), and
 * registers it in either the global registry() or a caller-supplied
 * @ref ThreadRegistry.
 *
 * @par Copyability / movability
 * - **Not copyable** (deleted).
 * - **Movable** -- move construction / assignment transfers registration
 *   ownership to the new instance and disarms the source.
 *
 * @par Thread safety
 * Construction and destruction interact with the target ThreadRegistry, which
 * is itself thread-safe.  The guard object itself must not be shared across
 * threads without external synchronisation.
 *
 * @par Lifetime / ownership
 * - If constructed with a specific @c ThreadRegistry&, that registry **must**
 *   outlive this guard.
 * - If constructed without an explicit registry, the global registry()
 *   singleton is used, which has static storage duration.
 *
 * @par Typical usage
 * @code
 * void worker_func() {
 *     threadschedule::AutoRegisterCurrentThread guard("worker", "pool");
 *     // ... thread body ...
 * }   // automatically unregistered here
 * @endcode
 *
 * @par Caveats
 * - Must be constructed **from** the thread it represents (delegates to
 *   ThreadControlBlock::create_for_current_thread()).
 * - On Linux, the name must be at most 15 characters (POSIX thread name
 *   limit); longer names cause ThreadControlBlock::set_name() to fail, but
 *   the thread is still registered.
 */
class AutoRegisterCurrentThread
{
  public:
    explicit AutoRegisterCurrentThread(std::string const& name = std::string(),
                                       std::string const& componentTag = std::string())
        : active_(true), externalReg_(nullptr)
    {
        auto block = ThreadControlBlock::create_for_current_thread();
        (void)block->set_name(name);
        registry().register_current_thread(block, name, componentTag);
    }

    explicit AutoRegisterCurrentThread(ThreadRegistry& reg, std::string const& name = std::string(),
                                       std::string const& componentTag = std::string())
        : active_(true), externalReg_(&reg)
    {
        auto block = ThreadControlBlock::create_for_current_thread();
        (void)block->set_name(name);
        externalReg_->register_current_thread(block, name, componentTag);
    }
    ~AutoRegisterCurrentThread()
    {
        if (active_)
        {
            if (externalReg_ != nullptr)
                externalReg_->unregister_current_thread();
            else
                registry().unregister_current_thread();
        }
    }
    AutoRegisterCurrentThread(AutoRegisterCurrentThread const&) = delete;
    auto operator=(AutoRegisterCurrentThread const&) -> AutoRegisterCurrentThread& = delete;
    AutoRegisterCurrentThread(AutoRegisterCurrentThread&& other) noexcept
        : active_(other.active_), externalReg_(other.externalReg_)
    {
        other.active_ = false;
        other.externalReg_ = nullptr;
    }
    auto operator=(AutoRegisterCurrentThread&& other) noexcept -> AutoRegisterCurrentThread&
    {
        if (this != &other)
        {
            if (active_)
            {
                if (externalReg_ != nullptr)
                    externalReg_->unregister_current_thread();
                else
                    registry().unregister_current_thread();
            }
            active_ = other.active_;
            externalReg_ = other.externalReg_;
            other.active_ = false;
            other.externalReg_ = nullptr;
        }
        return *this;
    }

  private:
    bool active_;
    ThreadRegistry* externalReg_;
};

} // namespace threadschedule

#ifndef _WIN32
namespace threadschedule
{
/**
 * @brief Attaches a thread to a Linux cgroup by writing its TID to the
 *        appropriate control file.
 *
 * Tries the following files inside @p cgroupDir, in order:
 * 1. @c cgroup.threads (cgroup v2)
 * 2. @c tasks (cgroup v1 / hybrid)
 * 3. @c cgroup.procs (cgroup v2 process-level; works for single-threaded
 *    workloads)
 *
 * The first file that can be opened and written to successfully is used.
 *
 * @param cgroupDir Absolute path to the target cgroup directory
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
inline auto cgroup_attach_tid(std::string const& cgroupDir, Tid tid) -> expected<void, std::error_code>
{
    std::vector<std::string> candidates = {"cgroup.threads", "tasks", "cgroup.procs"};
    for (auto const& file : candidates)
    {
        std::string path = cgroupDir + "/" + file;
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
