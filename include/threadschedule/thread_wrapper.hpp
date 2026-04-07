#pragma once

/**
 * @file thread_wrapper.hpp
 * @brief Enhanced thread wrappers: ThreadWrapper, JThreadWrapper, and non-owning views.
 */

#include "expected.hpp"
#include "scheduler_policy.hpp"
#include <optional>
#include <string>
#include <thread>

#ifdef _WIN32
#    include <libloaderapi.h>
#    include <windows.h>
#else
#    include <dirent.h>
#    include <fstream>
#    include <sys/prctl.h>
#    include <sys/resource.h>
#    include <sys/syscall.h>
#    include <unistd.h>
#endif

namespace threadschedule
{

namespace detail
{
/** @brief Tag type selecting owning (value) storage in ThreadStorage. */
struct OwningTag
{
};
/** @brief Tag type selecting non-owning (pointer) storage in ThreadStorage. */
struct NonOwningTag
{
};

template <typename ThreadType, typename OwnershipTag>
class ThreadStorage;

/**
 * @brief Owning thread storage - holds the thread object by value.
 *
 * @tparam ThreadType The thread type (e.g. std::thread, std::jthread).
 *
 * Stores the thread object directly as a member, introducing zero indirection
 * overhead beyond the thread object itself. This specialization is used by
 * wrappers that own and manage the lifetime of their thread.
 *
 * @par Copyability
 * Not copyable (deleted by the underlying thread type). Movable if @p ThreadType is movable.
 *
 * @par Thread Safety
 * Not thread-safe. Access must be externally synchronized.
 */
template <typename ThreadType>
class ThreadStorage<ThreadType, OwningTag>
{
  protected:
    ThreadStorage() = default;

    [[nodiscard]] auto underlying() noexcept -> ThreadType&
    {
        return thread_;
    }
    [[nodiscard]] auto underlying() const noexcept -> ThreadType const&
    {
        return thread_;
    }

    ThreadType thread_;
};

/**
 * @brief Non-owning thread storage - holds a raw pointer to an external thread.
 *
 * @tparam ThreadType The thread type (e.g. std::thread, std::jthread).
 *
 * Stores a non-owning raw pointer to a thread object managed elsewhere.
 * Does @b not join or detach on destruction.
 *
 * @warning The caller is responsible for ensuring the referenced thread object
 *          outlives this storage instance. Dangling pointer access is undefined behavior.
 *
 * @par Copyability
 * Trivially copyable (pointer copy). Multiple instances may alias the same thread.
 *
 * @par Thread Safety
 * Not thread-safe. Access must be externally synchronized.
 */
template <typename ThreadType>
class ThreadStorage<ThreadType, NonOwningTag>
{
  protected:
    ThreadStorage() = default;
    explicit ThreadStorage(ThreadType& t) : external_thread_(&t)
    {
    }

    [[nodiscard]] auto underlying() noexcept -> ThreadType&
    {
        return *external_thread_;
    }
    [[nodiscard]] auto underlying() const noexcept -> ThreadType const&
    {
        return *external_thread_;
    }

    ThreadType* external_thread_ = nullptr; // non-owning
};
} // namespace detail

/**
 * @brief Polymorphic base providing common thread management operations.
 *
 * @tparam ThreadType    The underlying thread type (std::thread or std::jthread).
 * @tparam OwnershipTag  detail::OwningTag (default) or detail::NonOwningTag.
 *
 * Provides a uniform interface for join, detach, naming, priority, affinity, scheduling
 * policy, and nice-value control on top of any standard thread type. Derived classes
 * (ThreadWrapper, JThreadWrapper, and their View counterparts) customize ownership
 * semantics while inheriting all of these operations.
 *
 * @par Virtual Destructor
 * Has a virtual destructor so it can be used as a polymorphic base.
 *
 * @par join() / detach()
 * Both are safe to call even if the thread is not joinable (they check first).
 *
 * @par set_name()
 * - **Linux**: uses @c pthread_setname_np; names are limited to 15 characters
 *   (returns @c errc::invalid_argument if exceeded).
 * - **Windows**: dynamically loads @c SetThreadDescription from kernel32.dll.
 *   Names may be longer. Returns @c errc::function_not_supported if the API is
 *   unavailable (pre-Windows 10 1607).
 *
 * @par set_priority()
 * Maps through SchedulerParams::create_for_policy(). On Linux, uses
 * @c pthread_setschedparam and may require @c CAP_SYS_NICE or root privileges
 * for real-time policies. On Windows, maps to @c SetThreadPriority constants.
 *
 * @par set_scheduling_policy()
 * Linux-specific concept; on Windows this falls back to set_priority().
 *
 * @par set_affinity()
 * - **Linux**: @c pthread_setaffinity_np with @c cpu_set_t.
 * - **Windows**: prefers @c SetThreadGroupAffinity (multi-processor-group aware)
 *   and falls back to @c SetThreadAffinityMask on single-group systems.
 *
 * @par set_nice_value() / get_nice_value()
 * @b Process-level operation - affects **all** threads in the process.
 * On Linux calls @c setpriority(PRIO_PROCESS, ...).
 * On Windows maps to @c SetPriorityClass / @c GetPriorityClass.
 *
 * @par Return Values
 * All @c set_* methods (except set_nice_value) return
 * @c expected<void, std::error_code>. Always check the return value;
 * failures are silent unless inspected.
 *
 * @par Thread Safety
 * Individual method calls are safe if the underlying OS call is safe, but
 * concurrent mutation of the same wrapper from multiple threads is not
 * synchronized internally.
 */
template <typename ThreadType, typename OwnershipTag = detail::OwningTag>
class BaseThreadWrapper : protected detail::ThreadStorage<ThreadType, OwnershipTag>
{
  public:
    using native_handle_type = typename ThreadType::native_handle_type;
    using id = typename ThreadType::id;

    BaseThreadWrapper() = default;
    explicit BaseThreadWrapper(ThreadType& t) : detail::ThreadStorage<ThreadType, OwnershipTag>(t)
    {
    }
    virtual ~BaseThreadWrapper() = default;

    // Thread management
    void join()
    {
        if (underlying().joinable())
        {
            underlying().join();
        }
    }

    void detach()
    {
        if (underlying().joinable())
        {
            underlying().detach();
        }
    }

    [[nodiscard]] auto joinable() const noexcept -> bool
    {
        return underlying().joinable();
    }
    [[nodiscard]] auto get_id() const noexcept -> id
    {
        return underlying().get_id();
    }
    [[nodiscard]] auto native_handle() noexcept -> native_handle_type
    {
        return underlying().native_handle();
    }

    [[nodiscard]] auto set_name(std::string const& name) -> expected<void, std::error_code>
    {
        return detail::apply_name(native_handle(), name);
    }

    [[nodiscard]] auto get_name() const -> std::optional<std::string>
    {
        return detail::read_name(const_cast<BaseThreadWrapper*>(this)->native_handle());
    }

    [[nodiscard]] auto set_priority(ThreadPriority priority) -> expected<void, std::error_code>
    {
        return detail::apply_priority(native_handle(), priority);
    }

    [[nodiscard]] auto set_scheduling_policy(SchedulingPolicy policy, ThreadPriority priority)
        -> expected<void, std::error_code>
    {
        return detail::apply_scheduling_policy(native_handle(), policy, priority);
    }

    [[nodiscard]] auto set_affinity(ThreadAffinity const& affinity) -> expected<void, std::error_code>
    {
        return detail::apply_affinity(native_handle(), affinity);
    }

    [[nodiscard]] auto get_affinity() const -> std::optional<ThreadAffinity>
    {
        return detail::read_affinity(const_cast<BaseThreadWrapper*>(this)->native_handle());
    }

    // Nice value (process-level, affects all threads)
    static auto set_nice_value(int nice_value) -> bool
    {
#ifdef _WIN32
        // Windows has process priority classes, not nice values
        // We'll use SetPriorityClass for the process
        DWORD priority_class;
        if (nice_value <= -15)
        {
            priority_class = HIGH_PRIORITY_CLASS;
        }
        else if (nice_value <= -10)
        {
            priority_class = ABOVE_NORMAL_PRIORITY_CLASS;
        }
        else if (nice_value < 10)
        {
            priority_class = NORMAL_PRIORITY_CLASS;
        }
        else if (nice_value < 19)
        {
            priority_class = BELOW_NORMAL_PRIORITY_CLASS;
        }
        else
        {
            priority_class = IDLE_PRIORITY_CLASS;
        }
        return SetPriorityClass(GetCurrentProcess(), priority_class) != 0;
#else
        return setpriority(PRIO_PROCESS, 0, nice_value) == 0;
#endif
    }

    static auto get_nice_value() -> std::optional<int>
    {
#ifdef _WIN32
        // Get Windows process priority class and map to nice value
        DWORD priority_class = GetPriorityClass(GetCurrentProcess());
        if (priority_class == 0)
        {
            return std::nullopt;
        }

        // Map Windows priority class to nice value
        switch (priority_class)
        {
        case HIGH_PRIORITY_CLASS:
            return -15;
        case ABOVE_NORMAL_PRIORITY_CLASS:
            return -10;
        case NORMAL_PRIORITY_CLASS:
            return 0;
        case BELOW_NORMAL_PRIORITY_CLASS:
            return 10;
        case IDLE_PRIORITY_CLASS:
            return 19;
        default:
            return 0;
        }
#else
        errno = 0;
        int const nice = getpriority(PRIO_PROCESS, 0);
        if (errno == 0)
        {
            return nice;
        }
        return std::nullopt;
#endif
    }

  protected:
    using detail::ThreadStorage<ThreadType, OwnershipTag>::underlying;
    using detail::ThreadStorage<ThreadType, OwnershipTag>::ThreadStorage;
};

/**
 * @brief Owning wrapper around std::thread with RAII join-on-destroy semantics.
 *
 * Extends @ref BaseThreadWrapper to provide an owning, movable, non-copyable wrapper
 * over @c std::thread. Adds automatic lifetime management: the destructor joins
 * the thread if it is still joinable, which means destruction can @b block until
 * the thread finishes.
 *
 * @par Copyability / Movability
 * - **Not copyable** (copy constructor and copy assignment are deleted).
 * - **Movable**. Move construction transfers ownership. Move @b assignment first
 *   joins the currently held thread (blocking!) before taking ownership of the
 *   source thread.
 *
 * @par Destruction
 * The destructor calls @c join() if the thread is joinable. This will @b block
 * the destroying thread until the managed thread completes. If blocking
 * destruction is undesirable, call @c detach() or @c release() before the
 * wrapper goes out of scope.
 *
 * @par release()
 * Transfers ownership of the underlying @c std::thread out of the wrapper,
 * returning it by value. After release, the wrapper holds a default-constructed
 * (non-joinable) thread and destruction becomes a no-op.
 *
 * @par create_with_config()
 * Factory that creates a thread and attempts to set its name and scheduling
 * policy. Failures from @c set_name() or @c set_scheduling_policy() are
 * silently ignored - the thread will still be running but may not have the
 * requested attributes. Check attributes after construction if they are
 * critical.
 *
 * @par Thread Safety
 * Not thread-safe. A single ThreadWrapper must not be mutated concurrently
 * from multiple threads.
 */
class ThreadWrapper : public BaseThreadWrapper<std::thread, detail::OwningTag>
{
  public:
    ThreadWrapper() = default;

    // Construct by taking ownership of an existing std::thread (move)
    ThreadWrapper(std::thread&& t) noexcept
    {
        this->underlying() = std::move(t);
    }

    template <typename F, typename... Args>
    explicit ThreadWrapper(F&& f, Args&&... args) : BaseThreadWrapper()
    {
        this->underlying() = std::thread(std::forward<F>(f), std::forward<Args>(args)...);
    }

    ThreadWrapper(ThreadWrapper const&) = delete;
    auto operator=(ThreadWrapper const&) -> ThreadWrapper& = delete;

    ThreadWrapper(ThreadWrapper&& other) noexcept
    {
        this->underlying() = std::move(other.underlying());
    }

    auto operator=(ThreadWrapper&& other) noexcept -> ThreadWrapper&
    {
        if (this != &other)
        {
            if (this->underlying().joinable())
            {
                this->underlying().join();
            }
            this->underlying() = std::move(other.underlying());
        }
        return *this;
    }

    ~ThreadWrapper() override
    {
        if (this->underlying().joinable())
        {
            this->underlying().join();
        }
    }

    // Ownership transfer to std::thread for APIs that take plain std::thread
    auto release() noexcept -> std::thread
    {
        return std::move(this->underlying());
    }

    explicit operator std::thread() && noexcept
    {
        return std::move(this->underlying());
    }

    // Factory methods
    template <typename F, typename... Args>
    static auto create_with_config(std::string const& name, SchedulingPolicy policy, ThreadPriority priority, F&& f,
                                   Args&&... args) -> ThreadWrapper
    {
        ThreadWrapper wrapper(std::forward<F>(f), std::forward<Args>(args)...);
        (void)wrapper.set_name(name);
        (void)wrapper.set_scheduling_policy(policy, priority);
        return wrapper;
    }
};

/**
 * @brief Non-owning view over an externally managed std::thread.
 *
 * Provides the full @ref BaseThreadWrapper interface (naming, priority, affinity, etc.)
 * without taking ownership of the thread. The destructor is trivial - it does
 * @b not join or detach.
 *
 * @warning The referenced @c std::thread must outlive this view. If the thread
 *          object is destroyed or moved while a view still references it, all
 *          subsequent operations through the view invoke undefined behavior.
 *
 * @par Copyability / Movability
 * Implicitly copyable and movable (pointer semantics). Multiple views may
 * alias the same thread.
 *
 * @par Thread Safety
 * Same caveats as BaseThreadWrapper. Concurrent use of a view and direct use
 * of the underlying thread must be externally synchronized.
 */
class ThreadWrapperView : public BaseThreadWrapper<std::thread, detail::NonOwningTag>
{
  public:
    ThreadWrapperView(std::thread& t) : BaseThreadWrapper<std::thread, detail::NonOwningTag>(t)
    {
    }

    // Non-owning access to the underlying std::thread
    auto get() noexcept -> std::thread&
    {
        return this->underlying();
    }
    [[nodiscard]] auto get() const noexcept -> std::thread const&
    {
        return this->underlying();
    }
};

/**
 * @brief Owning wrapper around std::jthread with cooperative cancellation (C++20).
 *
 * Analogous to @ref ThreadWrapper but wraps @c std::jthread, inheriting its built-in
 * cooperative stop semantics. On destruction the underlying @c std::jthread
 * automatically requests a stop and joins, so the destructor may @b block
 * until the thread acknowledges the stop request and finishes.
 *
 * Exposes @c request_stop(), @c stop_requested(), @c get_stop_token(), and
 * @c get_stop_source() for cooperative cancellation.
 *
 * @par Copyability / Movability
 * - **Not copyable** (copy constructor and copy assignment are deleted).
 * - **Movable**. Move assignment transfers ownership directly (the source
 *   @c jthread's destructor handles its own cleanup).
 *
 * @par Destruction
 * Delegates to @c std::jthread's destructor which calls @c request_stop()
 * then @c join(). This will block until the managed thread finishes.
 *
 * @par release()
 * Transfers ownership of the underlying @c std::jthread out of the wrapper.
 *
 * @par create_with_config()
 * Factory that creates a jthread and attempts to set its name and scheduling
 * policy. Failures from set_name() or set_scheduling_policy() are silently
 * ignored.
 *
 * @par Pre-C++20 Fallback
 * When compiled below C++20, @c JThreadWrapper is a type alias for
 * @ref ThreadWrapper (which lacks stop-token support).
 *
 * @par Thread Safety
 * Not thread-safe. A single JThreadWrapper must not be mutated concurrently
 * from multiple threads. The stop token/source obtained from the wrapper are
 * independently thread-safe per the standard.
 */
#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
class JThreadWrapper : public BaseThreadWrapper<std::jthread, detail::OwningTag>
{
  public:
    JThreadWrapper() = default;

    // Construct by taking ownership of an existing std::jthread (move)
    JThreadWrapper(std::jthread&& t) noexcept : BaseThreadWrapper()
    {
        this->underlying() = std::move(t);
    }

    // Ownership transfer to std::jthread for APIs that take plain std::jthread
    auto release() noexcept -> std::jthread
    {
        return std::move(this->underlying());
    }

    explicit operator std::jthread() && noexcept
    {
        return std::move(this->underlying());
    }

    template <typename F, typename... Args>
    explicit JThreadWrapper(F&& f, Args&&... args) : BaseThreadWrapper()
    {
        this->underlying() = std::jthread(std::forward<F>(f), std::forward<Args>(args)...);
    }

    JThreadWrapper(JThreadWrapper const&) = delete;
    auto operator=(JThreadWrapper const&) -> JThreadWrapper& = delete;

    JThreadWrapper(JThreadWrapper&& other) noexcept
    {
        this->underlying() = std::move(other.underlying());
    }

    auto operator=(JThreadWrapper&& other) noexcept -> JThreadWrapper&
    {
        if (this != &other)
        {
            this->underlying() = std::move(other.underlying());
        }
        return *this;
    }

    // jthread-specific functionality
    void request_stop()
    {
        this->underlying().request_stop();
    }
    [[nodiscard]] auto stop_requested() const noexcept -> bool
    {
        return this->underlying().get_stop_token().stop_requested();
    }
    [[nodiscard]] auto get_stop_token() const noexcept -> std::stop_token
    {
        return this->underlying().get_stop_token();
    }
    [[nodiscard]] auto get_stop_source() noexcept -> std::stop_source
    {
        return this->underlying().get_stop_source();
    }

    // Factory methods
    template <typename F, typename... Args>
    static auto create_with_config(std::string const& name, SchedulingPolicy policy, ThreadPriority priority, F&& f,
                                   Args&&... args) -> JThreadWrapper
    {
        JThreadWrapper wrapper(std::forward<F>(f), std::forward<Args>(args)...);
        (void)wrapper.set_name(name);
        (void)wrapper.set_scheduling_policy(policy, priority);
        return wrapper;
    }
};

/**
 * @brief Non-owning view over an externally managed std::jthread (C++20).
 *
 * Provides the full @ref BaseThreadWrapper interface plus jthread-specific cooperative
 * cancellation methods (request_stop, stop_requested, get_stop_token,
 * get_stop_source) without taking ownership. The destructor is trivial - it
 * does @b not request a stop, join, or detach.
 *
 * @warning The referenced @c std::jthread must outlive this view. Accessing a
 *          view after the underlying jthread has been destroyed or moved is
 *          undefined behavior.
 *
 * @par Copyability / Movability
 * Implicitly copyable and movable (pointer semantics). Multiple views may
 * alias the same jthread.
 *
 * @par Pre-C++20 Fallback
 * When compiled below C++20, @c JThreadWrapperView is a type alias for
 * @ref ThreadWrapperView.
 *
 * @par Thread Safety
 * Same caveats as BaseThreadWrapper. The stop token/source obtained from the
 * view are independently thread-safe per the standard.
 */
class JThreadWrapperView : public BaseThreadWrapper<std::jthread, detail::NonOwningTag>
{
  public:
    JThreadWrapperView(std::jthread& t) : BaseThreadWrapper<std::jthread, detail::NonOwningTag>(t)
    {
    }

    void request_stop()
    {
        this->underlying().request_stop();
    }
    [[nodiscard]] auto stop_requested() const noexcept -> bool
    {
        return this->underlying().get_stop_token().stop_requested();
    }
    [[nodiscard]] auto get_stop_token() const noexcept -> std::stop_token
    {
        return this->underlying().get_stop_token();
    }
    [[nodiscard]] auto get_stop_source() noexcept -> std::stop_source
    {
        return this->underlying().get_stop_source();
    }

    // Non-owning access to the underlying std::jthread
    auto get() noexcept -> std::jthread&
    {
        return this->underlying();
    }
    [[nodiscard]] auto get() const noexcept -> std::jthread const&
    {
        return this->underlying();
    }
};
#else
// Fallback for compilers without C++20 support
using JThreadWrapper = ThreadWrapper;
using JThreadWrapperView = ThreadWrapperView;
#endif // C++20

/**
 * @brief Looks up an OS thread by its name via /proc and provides scheduling control.
 *
 * On construction, scans @c /proc/self/task/ to find a thread whose
 * @c comm matches the given name. If found, the Linux TID is cached and
 * subsequent calls operate on that TID via @c sched_setscheduler /
 * @c sched_setaffinity (TID-based syscalls, @b not pthread_setschedparam).
 *
 * @par Platform Support
 * - **Linux only**. On Windows every method is a no-op or returns
 *   @c errc::function_not_supported, and found() always returns @c false.
 *
 * @par Snapshot Semantics
 * The /proc scan happens once at construction time. If the target thread
 * exits or changes its name after construction, this view becomes stale.
 * There is no live tracking.
 *
 * @par Thread Name Limit
 * Linux thread names are limited to 15 characters. Names longer than 15
 * characters will never match, and set_name() rejects them.
 *
 * @par Scheduling
 * Uses @c sched_setscheduler(tid, ...) rather than @c pthread_setschedparam().
 * Changing real-time policies may require @c CAP_SYS_NICE.
 *
 * @par Copyability / Movability
 * Trivially copyable and movable (stores only a TID/handle).
 *
 * @par Thread Safety
 * Methods are safe to call concurrently from different threads as long as
 * the target thread still exists, but the class itself provides no
 * internal synchronization.
 */
class ThreadByNameView
{
  public:
#ifdef _WIN32
    using native_handle_type = void*; // unsupported placeholder
#else
    using native_handle_type = pid_t; // Linux TID
#endif

    explicit ThreadByNameView(const std::string& name)
    {
#ifdef _WIN32
        // Not supported on Windows in this implementation
        (void)name;
#else
        DIR* dir = opendir("/proc/self/task");
        if (dir == nullptr)
            return;
        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (entry->d_name[0] == '.')
                continue;
            std::string tid_str(entry->d_name);
            std::string path = std::string("/proc/self/task/") + tid_str + "/comm";
            std::ifstream in(path);
            if (!in)
                continue;
            std::string current;
            std::getline(in, current);
            if (!current.empty() && current.back() == '\n')
                current.pop_back();
            if (current == name)
            {
                handle_ = static_cast<pid_t>(std::stoi(tid_str));
                break;
            }
        }
        closedir(dir);
#endif
    }

    [[nodiscard]] auto found() const noexcept -> bool
    {
#ifdef _WIN32
        return false;
#else
        return handle_ > 0;
#endif
    }

    [[nodiscard]] auto set_name(std::string const& name) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        return unexpected(std::make_error_code(std::errc::function_not_supported));
#else
        if (!found())
            return unexpected(std::make_error_code(std::errc::no_such_process));
        if (name.length() > 15)
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        std::string path = std::string("/proc/self/task/") + std::to_string(handle_) + "/comm";
        std::ofstream out(path);
        if (!out)
            return unexpected(std::error_code(errno, std::generic_category()));
        out << name;
        out.flush();
        if (!out)
            return unexpected(std::error_code(errno, std::generic_category()));
        return {};
#endif
    }

    [[nodiscard]] auto get_name() const -> std::optional<std::string>
    {
#ifdef _WIN32
        return std::nullopt;
#else
        if (!found())
            return std::nullopt;
        std::string path = std::string("/proc/self/task/") + std::to_string(handle_) + "/comm";
        std::ifstream in(path);
        if (!in)
            return std::nullopt;
        std::string current;
        std::getline(in, current);
        if (!current.empty() && current.back() == '\n')
            current.pop_back();
        return current;
#endif
    }

    [[nodiscard]] auto native_handle() const noexcept -> native_handle_type
    {
        return handle_;
    }

    [[nodiscard]] auto set_priority(ThreadPriority priority) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        return unexpected(std::make_error_code(std::errc::function_not_supported));
#else
        if (!found())
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return detail::apply_priority(handle_, priority);
#endif
    }

    [[nodiscard]] auto set_scheduling_policy(SchedulingPolicy policy, ThreadPriority priority) const
        -> expected<void, std::error_code>
    {
#ifdef _WIN32
        return unexpected(std::make_error_code(std::errc::function_not_supported));
#else
        if (!found())
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return detail::apply_scheduling_policy(handle_, policy, priority);
#endif
    }

    [[nodiscard]] auto set_affinity(ThreadAffinity const& affinity) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        return unexpected(std::make_error_code(std::errc::function_not_supported));
#else
        if (!found())
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return detail::apply_affinity(handle_, affinity);
#endif
    }

  private:
#ifdef _WIN32
    native_handle_type handle_ = nullptr;
#else
    native_handle_type handle_ = 0;
#endif
};

/**
 * @brief Static utility class providing hardware and scheduling introspection.
 *
 * All methods are static; the class holds no state and should not be instantiated.
 *
 * @par Provided Queries
 * - @c hardware_concurrency() - delegates to @c std::thread::hardware_concurrency().
 * - @c get_thread_id() - returns the OS-level thread ID (Linux TID via
 *   @c syscall(SYS_gettid), Windows thread ID via @c GetCurrentThreadId()).
 * - @c get_current_policy() - returns the calling thread's scheduling policy.
 *   On Windows this always returns @c SchedulingPolicy::OTHER.
 * - @c get_current_priority() - returns the calling thread's scheduling priority.
 *
 * @par Thread Safety
 * All methods are thread-safe (they query per-thread or immutable system state).
 */
class ThreadInfo
{
  public:
    static auto hardware_concurrency() -> unsigned int
    {
        return std::thread::hardware_concurrency();
    }

    static auto get_thread_id()
    {
#ifdef _WIN32
        return GetCurrentThreadId();
#else
        return static_cast<pid_t>(syscall(SYS_gettid));
#endif
    }

    static auto get_current_policy() -> std::optional<SchedulingPolicy>
    {
#ifdef _WIN32
        // Windows doesn't have Linux-style scheduling policies
        // Return OTHER as a default
        return SchedulingPolicy::OTHER;
#else
        const int policy = sched_getscheduler(0);
        if (policy == -1)
        {
            return std::nullopt;
        }
        return static_cast<SchedulingPolicy>(policy);
#endif
    }

    static auto get_current_priority() -> std::optional<int>
    {
#ifdef _WIN32
        HANDLE thread = GetCurrentThread();
        int priority = GetThreadPriority(thread);
        if (priority == THREAD_PRIORITY_ERROR_RETURN)
        {
            return std::nullopt;
        }
        return priority;
#else
        sched_param param;
        if (sched_getparam(0, &param) == 0)
        {
            return param.sched_priority;
        }
        return std::nullopt;
#endif
    }
};

} // namespace threadschedule
