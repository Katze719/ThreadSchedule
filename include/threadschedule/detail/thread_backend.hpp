#pragma once

/**
 * @file detail/thread_backend.hpp
 * @brief C++17 thread wrappers and non-owning views.
 */

#include "../expected.hpp"
#include "../scheduler_policy.hpp"
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <libloaderapi.h>
#  include <windows.h>
#else
#  include <dirent.h>
#  include <fstream>
#  include <sys/prctl.h>
#  include <sys/resource.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

namespace threadschedule
{

namespace detail
{
class thread_identity_state
{
public:
  void
  publish(native_thread_id tid)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      tid_ = tid;
      ready_ = true;
    }
    ready_condition_.notify_all();
  }

  [[nodiscard]] auto
  wait() -> native_thread_id
  {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_condition_.wait(lock, [this] { return ready_; });
    return tid_;
  }

private:
  std::mutex mutex_;
  std::condition_variable ready_condition_;
  native_thread_id tid_{};
  bool ready_{ false };
};

[[nodiscard]] inline auto
current_native_thread_id() noexcept -> native_thread_id
{
#ifdef _WIN32
  return GetCurrentThreadId();
#else
  return static_cast<pid_t>(syscall(SYS_gettid));
#endif
}

/** @brief Tag type selecting owning (value) storage in thread_storage. */
struct owning_tag
{
};
/** @brief Tag type selecting non-owning (pointer) storage in thread_storage.
 */
struct non_owning_tag
{
};

template <typename ThreadType, typename OwnershipTag>
class thread_storage;

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
 * Not copyable (deleted by the underlying thread type). Movable if @p
 * ThreadType is movable.
 *
 * @par Thread Safety
 * Not thread-safe. Access must be externally synchronized.
 */
template <typename ThreadType>
class thread_storage<ThreadType, owning_tag>
{
protected:
  thread_storage() = default;

  [[nodiscard]] auto
  underlying() noexcept -> ThreadType&
  {
    return thread_;
  }
  [[nodiscard]] auto
  underlying() const noexcept -> ThreadType const&
  {
    return thread_;
  }

  ThreadType thread_;
};

/**
 * @brief Non-owning thread storage - holds a raw pointer to an external
 * thread.
 *
 * @tparam ThreadType The thread type (e.g. std::thread, std::jthread).
 *
 * Stores a non-owning raw pointer to a thread object managed elsewhere.
 * Does @b not join or detach on destruction.
 *
 * @warning The caller is responsible for ensuring the referenced thread object
 *          outlives this storage instance. Dangling pointer access is
 * undefined behavior.
 *
 * @par Copyability
 * Trivially copyable (pointer copy). Multiple instances may alias the same
 * thread.
 *
 * @par Thread Safety
 * Not thread-safe. Access must be externally synchronized.
 */
template <typename ThreadType>
class thread_storage<ThreadType, non_owning_tag>
{
protected:
  thread_storage() = default;
  explicit thread_storage(ThreadType& t) : external_thread_(&t) {}

  [[nodiscard]] auto
  underlying() noexcept -> ThreadType&
  {
    return *external_thread_;
  }
  [[nodiscard]] auto
  underlying() const noexcept -> ThreadType const&
  {
    return *external_thread_;
  }

  ThreadType* external_thread_ = nullptr; // non-owning
};

template <typename ThreadLike>
inline auto
configure_thread(ThreadLike& thread, std::string const& name,
                 native_scheduling_policy policy,
                 native_thread_priority priority)
    -> expected<void, std::error_code>
{
  auto named = thread.set_name(name);
  if (!named)
    return unexpected(named.error());
  return thread.set_scheduling_policy(policy, priority);
}

template <typename ThreadLike>
inline auto
configure_thread(ThreadLike& thread, native_thread_config const& config)
    -> expected<void, std::error_code>
{
  if (!config.name.empty())
    {
      auto named = thread.set_name(config.name);
      if (!named)
        return unexpected(named.error());
    }
  auto scheduled = thread.configure(config.scheduling);
  if (!scheduled)
    return unexpected(scheduled.error());
  if (config.affinity.has_value())
    return thread.set_affinity(*config.affinity);
  return {};
}
} // namespace detail

/**
 * @brief Polymorphic base providing common thread management operations.
 *
 * @tparam ThreadType    The underlying thread type (std::thread or
 * std::jthread).
 * @tparam OwnershipTag  detail::owning_tag (default) or
 * detail::non_owning_tag.
 *
 * Provides a uniform interface for join, detach, naming, priority, affinity,
 * scheduling policy, and nice-value control on top of any standard thread
 * type. Derived classes
 * (`thread_backend` and `thread_view_backend`) customize ownership
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
 * - **Windows**: dynamically loads @c SetThreadDescription from Kernel32.dll
 *   or KernelBase.dll.
 *   Names may be longer. Returns @c errc::function_not_supported if the API is
 *   unavailable (pre-Windows 10 1607).
 *
 * @par get_name()
 * Returns @c expected<std::string, std::error_code> so unavailable APIs,
 * invalid handles, OS failures, and UTF conversion failures remain
 * diagnosable.
 *
 * @par set_priority()
 * Maps through scheduler_parameters::create_for_policy(). On Linux, uses
 * @c pthread_setschedparam and may require @c CAP_SYS_NICE or root privileges
 * for real-time policies. On Windows, maps to @c SetThreadPriority constants.
 *
 * @par set_scheduling_policy()
 * Linux-specific concept; on Windows this falls back to set_priority().
 *
 * @par set_affinity()
 * - **Linux**: @c pthread_setaffinity_np with @c cpu_set_t.
 * - **Windows**: prefers @c SetThreadGroupAffinity (multi-processor-group
 * aware) and falls back to @c SetThreadAffinityMask on single-group systems.
 *
 * @par set_nice_value() / get_nice_value()
 * Per-thread operation. On Linux, calls @c setpriority(PRIO_PROCESS, tid, ...)
 * using the captured kernel TID. On Windows, maps to @c SetThreadPriority /
 * @c GetThreadPriority without changing the process priority class.
 *
 * @par Return Values
 * All @c set_* methods return @c expected<void, std::error_code>. Always check
 * the return value; failures are silent unless inspected.
 *
 * @par Thread Safety
 * Individual method calls are safe if the underlying OS call is safe, but
 * concurrent mutation of the same wrapper from multiple threads is not
 * synchronized internally.
 */
namespace detail
{

template <typename ThreadType, typename OwnershipTag = detail::owning_tag>
class basic_thread_backend
    : protected detail::thread_storage<ThreadType, OwnershipTag>
{
public:
  using native_handle_type = typename ThreadType::native_handle_type;
  using id = typename ThreadType::id;

  basic_thread_backend() = default;
  explicit basic_thread_backend(ThreadType& t)
      : detail::thread_storage<ThreadType, OwnershipTag>(t)
  {
  }
  basic_thread_backend(ThreadType& t, native_thread_id tid)
      : detail::thread_storage<ThreadType, OwnershipTag>(t), native_id_(tid)
  {
  }
  virtual ~basic_thread_backend() = default;

  // Thread management
  void
  join()
  {
    if (underlying().joinable())
      {
        underlying().join();
      }
  }

  void
  detach()
  {
    if (underlying().joinable())
      {
        underlying().detach();
      }
  }

  [[nodiscard]] auto
  joinable() const noexcept -> bool
  {
    return underlying().joinable();
  }
  [[nodiscard]] auto
  get_id() const noexcept -> id
  {
    return underlying().get_id();
  }
  [[nodiscard]] auto
  native_handle() noexcept -> native_handle_type
  {
    return underlying().native_handle();
  }

  [[nodiscard]] auto
  set_name(std::string const& name) -> expected<void, std::error_code>
  {
    return detail::apply_name(native_handle(), name);
  }

  [[nodiscard]] auto
  get_name() const -> expected<std::string, std::error_code>
  {
    return detail::read_name(
        const_cast<basic_thread_backend*>(this)->native_handle());
  }

  [[nodiscard]] auto
  set_priority(native_thread_priority priority)
      -> expected<void, std::error_code>
  {
    return detail::apply_priority(native_handle(), priority);
  }

  [[nodiscard]] auto
  set_scheduling_policy(native_scheduling_policy policy,
                        native_thread_priority priority)
      -> expected<void, std::error_code>
  {
    return detail::apply_scheduling_policy(native_handle(), policy, priority);
  }

  [[nodiscard]] auto
  configure(native_scheduling_config const& config)
      -> expected<void, std::error_code>
  {
    return detail::apply_scheduling_config(native_handle(), native_id_,
                                           config);
  }

  [[nodiscard]] auto
  set_nice_value(int nice_value) -> expected<void, std::error_code>
  {
#ifdef _WIN32
    return detail::apply_nice_value(native_handle(), nice_value);
#else
    if (native_id_ <= 0)
      return unexpected(
          std::make_error_code(std::errc::operation_not_supported));
    return detail::apply_nice_value(native_id_, nice_value);
#endif
  }

  [[nodiscard]] auto
  get_nice_value() const -> expected<int, std::error_code>
  {
    return detail::read_effective_nice(
        const_cast<basic_thread_backend*>(this)->native_handle(), native_id_);
  }

  [[nodiscard]] auto
  configure(native_thread_config const& config)
      -> expected<void, std::error_code>
  {
    return detail::configure_thread(*this, config);
  }

  [[nodiscard]] auto
  set_affinity(native_thread_affinity const& affinity)
      -> expected<void, std::error_code>
  {
    return detail::apply_affinity(native_handle(), affinity);
  }

  [[nodiscard]] auto
  get_affinity() const -> std::optional<native_thread_affinity>
  {
    return detail::read_affinity(
        const_cast<basic_thread_backend*>(this)->native_handle());
  }

  [[nodiscard]] auto
  native_id() const noexcept -> native_thread_id
  {
    return native_id_;
  }

protected:
  void
  set_native_id(native_thread_id tid) noexcept
  {
    native_id_ = tid;
  }

  using detail::thread_storage<ThreadType, OwnershipTag>::underlying;
  using detail::thread_storage<ThreadType, OwnershipTag>::thread_storage;

  native_thread_id native_id_{};
};

/**
 * @brief Owning wrapper around std::thread with RAII join-on-destroy
 * semantics.
 *
 * Extends @ref basic_thread_backend to provide an owning, movable,
 * non-copyable wrapper over @c std::thread. Adds automatic lifetime
 * management: the destructor joins the thread if it is still joinable, which
 * means destruction can @b block until the thread finishes.
 *
 * @par Copyability / Movability
 * - **Not copyable** (copy constructor and copy assignment are deleted).
 * - **Movable**. Move construction transfers ownership. Move @b assignment
 * first joins the currently held thread (blocking!) before taking ownership of
 * the source thread.
 *
 * @par Destruction
 * The destructor calls @c join() if the thread is joinable. This will @b block
 * the destroying thread until the managed thread completes. If blocking
 * destruction is undesirable, call @c detach() or @c release() before the
 * wrapper goes out of scope.
 *
 * @par release()
 * Transfers ownership of the underlying @c std::thread out of the wrapper,
 * returning it by value. After release, the wrapper holds a
 * default-constructed (non-joinable) thread and destruction becomes a no-op.
 *
 * @par create_with_config()
 * Factory that creates a thread and attempts to set its name and scheduling
 * policy. Failures from @c set_name() or @c set_scheduling_policy() are
 * silently ignored - the thread will still be running but may not have the
 * requested attributes. Check attributes after construction if they are
 * critical.
 *
 * @par Thread Safety
 * Not thread-safe. A single thread_backend must not be mutated concurrently
 * from multiple threads.
 */
class thread_backend
    : public basic_thread_backend<std::thread, detail::owning_tag>
{
public:
  thread_backend() = default;

  // Construct by taking ownership of an existing std::thread (move)
  thread_backend(std::thread&& t) noexcept
  {
    this->underlying() = std::move(t);
  }

  thread_backend(std::thread&& t, native_thread_id tid) noexcept
  {
    this->underlying() = std::move(t);
    this->set_native_id(tid);
  }

  template <typename F, typename... Args>
  explicit thread_backend(F&& f, Args&&... args) : basic_thread_backend()
  {
    auto identity = std::make_shared<thread_identity_state>();
    using function_type = std::decay_t<F>;
    auto arguments = std::make_tuple(std::forward<Args>(args)...);
    this->underlying() = std::thread(
        [identity, function = function_type(std::forward<F>(f)),
         arguments = std::move(arguments)]() mutable
          {
            identity->publish(current_native_thread_id());
            std::apply(
                [&function](auto&&... stored)
                  {
                    std::invoke(std::move(function),
                                std::forward<decltype(stored)>(stored)...);
                  },
                std::move(arguments));
          });
    this->set_native_id(identity->wait());
  }

  thread_backend(thread_backend const&) = delete;
  auto operator=(thread_backend const&) -> thread_backend& = delete;

  thread_backend(thread_backend&& other) noexcept
  {
    this->underlying() = std::move(other.underlying());
    this->set_native_id(other.native_id());
    other.set_native_id({});
  }

  auto
  operator=(thread_backend&& other) noexcept -> thread_backend&
  {
    if (this != &other)
      {
        if (this->underlying().joinable())
          {
            this->underlying().join();
          }
        this->underlying() = std::move(other.underlying());
        this->set_native_id(other.native_id());
        other.set_native_id({});
      }
    return *this;
  }

  ~thread_backend() override
  {
    if (this->underlying().joinable())
      {
        this->underlying().join();
      }
  }

  // Ownership transfer to std::thread for APIs that take plain std::thread
  auto
  release() noexcept -> std::thread
  {
    return std::move(this->underlying());
  }

  explicit
  operator std::thread() && noexcept
  {
    return std::move(this->underlying());
  }

  // Factory methods
  template <typename F, typename... Args>
  static auto
  create_with_config(std::string const& name, native_scheduling_policy policy,
                     native_thread_priority priority, F&& f, Args&&... args)
      -> thread_backend
  {
    thread_backend wrapper(std::forward<F>(f), std::forward<Args>(args)...);
    (void)wrapper.set_name(name);
    (void)wrapper.set_scheduling_policy(policy, priority);
    return wrapper;
  }

  template <typename F, typename... Args>
  static auto
  create_with_config(native_thread_config const& config, F&& f, Args&&... args)
      -> thread_backend
  {
    thread_backend wrapper(std::forward<F>(f), std::forward<Args>(args)...);
    (void)wrapper.configure(config);
    return wrapper;
  }
};

/**
 * @brief Non-owning view over an externally managed std::thread.
 *
 * Provides the full @ref basic_thread_backend interface (naming, priority,
 * affinity, etc.) without taking ownership of the thread. The destructor is
 * trivial - it does
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
 * Same caveats as basic_thread_backend. Concurrent use of a view and direct
 * use of the underlying thread must be externally synchronized.
 */
class thread_view_backend
    : public basic_thread_backend<std::thread, detail::non_owning_tag>
{
public:
  thread_view_backend(std::thread& t)
      : basic_thread_backend<std::thread, detail::non_owning_tag>(t)
  {
  }

  thread_view_backend(std::thread& t, native_thread_id tid)
      : basic_thread_backend<std::thread, detail::non_owning_tag>(t, tid)
  {
  }

  // Non-owning access to the underlying std::thread
  auto
  get() noexcept -> std::thread&
  {
    return this->underlying();
  }
  [[nodiscard]] auto
  get() const noexcept -> std::thread const&
  {
    return this->underlying();
  }
};

} // namespace detail

/**
 * @brief Looks up an OS thread by its name via /proc and provides scheduling
 * control.
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
class thread_by_name_view
{
public:
#ifdef _WIN32
  using native_handle_type = void*; // unsupported placeholder
#else
  using native_handle_type = native_thread_id; // Linux TID
#endif

  explicit thread_by_name_view(std::string const& name)
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

  [[nodiscard]] auto
  found() const noexcept -> bool
  {
#ifdef _WIN32
    return false;
#else
    return handle_ > 0;
#endif
  }

  [[nodiscard]] auto
  set_name([[maybe_unused]] std::string const& name) const
      -> expected<void, std::error_code>
  {
#ifdef _WIN32
    return unexpected(std::make_error_code(std::errc::function_not_supported));
#else
    if (!found())
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return detail::apply_name(handle_, name);
#endif
  }

  [[nodiscard]] auto
  get_name() const -> expected<std::string, std::error_code>
  {
#ifdef _WIN32
    return unexpected(std::make_error_code(std::errc::function_not_supported));
#else
    if (!found())
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return detail::read_name(handle_);
#endif
  }

  [[nodiscard]] auto
  native_handle() const noexcept -> native_handle_type
  {
    return handle_;
  }

  [[nodiscard]] auto
  set_priority([[maybe_unused]] native_thread_priority priority) const
      -> expected<void, std::error_code>
  {
#ifdef _WIN32
    return unexpected(std::make_error_code(std::errc::function_not_supported));
#else
    if (!found())
      return unexpected(std::make_error_code(std::errc::no_such_process));
    return detail::apply_priority(handle_, priority);
#endif
  }

  [[nodiscard]] auto
  set_scheduling_policy([[maybe_unused]] native_scheduling_policy policy,
                        [[maybe_unused]] native_thread_priority priority) const
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

  [[nodiscard]] auto
  configure(native_scheduling_config const& config) const
      -> expected<void, std::error_code>
  {
#ifdef _WIN32
    (void)config;
    return unexpected(std::make_error_code(std::errc::function_not_supported));
#else
    return detail::apply_scheduling_config(handle_, handle_, config);
#endif
  }

  [[nodiscard]] auto
  configure(native_thread_config const& config) const
      -> expected<void, std::error_code>
  {
    return detail::configure_thread(*this, config);
  }

  [[nodiscard]] auto
  set_affinity([[maybe_unused]] native_thread_affinity const& affinity) const
      -> expected<void, std::error_code>
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
 * @brief Lightweight handle for querying and controlling a specific OS thread.
 *
 * The default constructor binds to the current thread. The explicit
 * constructor binds to a caller-provided OS thread ID (@ref native_thread_id),
 * allowing callers to act on library-owned background threads or any other
 * live thread in the process.
 *
 * @par Provided Queries / Operations
 * - @c hardware_concurrency() - delegates to @c
 * std::thread::hardware_concurrency().
 * - @c get_thread_id() - returns the OS-level thread ID (Linux TID via
 *   @c syscall(SYS_gettid), Windows thread ID via @c GetCurrentThreadId()).
 * - instance methods provide @c set_name, @c get_name, @c set_priority,
 *   @c set_scheduling_policy, @c set_affinity, @c get_affinity,
 *   @c get_policy, and @c get_priority for the bound thread ID.
 * - static @c get_current_policy() / @c get_current_priority() remain as
 *   compatibility conveniences for the current thread.
 *
 * @par Thread Safety
 * Individual operations are thread-safe and delegate to OS syscalls for the
 * bound thread. Default-constructed instances prefer the captured native
 * thread handle when available; @ref thread_info(native_thread_id) continues
 * to use the caller-provided OS thread ID.
 */
class thread_info
{
public:
#ifdef _WIN32
  using native_handle_type = HANDLE;
#else
  using native_handle_type = pthread_t;
#endif

  thread_info() : tid_(get_thread_id())
  {
    bind_current_thread_handle();
  }

  explicit thread_info(native_thread_id tid) : tid_(tid) {}

  [[nodiscard]] auto
  thread_id() const noexcept -> native_thread_id
  {
    return tid_;
  }

  [[nodiscard]] auto
  set_name(std::string const& name) const -> expected<void, std::error_code>
  {
    if (has_native_handle())
      return detail::apply_name(native_handle(), name);
    return detail::apply_name(tid_, name);
  }

  [[nodiscard]] auto
  get_name() const -> expected<std::string, std::error_code>
  {
    if (has_native_handle())
      return detail::read_name(native_handle());
    return detail::read_name(tid_);
  }

  [[nodiscard]] auto
  set_priority(native_thread_priority priority) const
      -> expected<void, std::error_code>
  {
    if (has_native_handle())
      return detail::apply_priority(native_handle(), priority);
    return detail::apply_priority(tid_, priority);
  }

  [[nodiscard]] auto
  set_scheduling_policy(native_scheduling_policy policy,
                        native_thread_priority priority) const
      -> expected<void, std::error_code>
  {
    if (has_native_handle())
      return detail::apply_scheduling_policy(native_handle(), policy,
                                             priority);
    return detail::apply_scheduling_policy(tid_, policy, priority);
  }

  [[nodiscard]] auto
  configure(native_scheduling_config const& config) const
      -> expected<void, std::error_code>
  {
    if (has_native_handle())
      return detail::apply_scheduling_config(native_handle(), tid_, config);
    return detail::apply_scheduling_config(tid_, tid_, config);
  }

  [[nodiscard]] auto
  configure(native_thread_config const& config) const
      -> expected<void, std::error_code>
  {
    return detail::configure_thread(*this, config);
  }

  [[nodiscard]] auto
  set_affinity(native_thread_affinity const& affinity) const
      -> expected<void, std::error_code>
  {
    if (has_native_handle())
      return detail::apply_affinity(native_handle(), affinity);
    return detail::apply_affinity(tid_, affinity);
  }

  [[nodiscard]] auto
  get_affinity() const -> std::optional<native_thread_affinity>
  {
    if (has_native_handle())
      return detail::read_affinity(native_handle());
    return detail::read_affinity(tid_);
  }

  [[nodiscard]] auto
  get_policy() const -> std::optional<native_scheduling_policy>
  {
    if (has_native_handle())
      return detail::read_scheduling_policy(native_handle());
    return detail::read_scheduling_policy(tid_);
  }

  [[nodiscard]] auto
  get_priority() const -> std::optional<int>
  {
    if (has_native_handle())
      return detail::read_priority(native_handle());
    return detail::read_priority(tid_);
  }

  static auto
  hardware_concurrency() -> unsigned int
  {
    return std::thread::hardware_concurrency();
  }

  static auto
  get_thread_id() -> native_thread_id
  {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return static_cast<pid_t>(syscall(SYS_gettid));
#endif
  }

  static auto
  get_current_policy() -> std::optional<native_scheduling_policy>
  {
    return thread_info().get_policy();
  }

  static auto
  get_current_priority() -> std::optional<int>
  {
    return thread_info().get_priority();
  }

private:
  void
  bind_current_thread_handle()
  {
#ifdef _WIN32
    HANDLE real_handle = nullptr;
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                        GetCurrentProcess(), &real_handle,
                        THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION,
                        FALSE, 0)
        != 0)
      {
        native_handle_ = real_handle;
        native_handle_owner_ = std::shared_ptr<void>(
            real_handle,
            [](void* handle)
              {
                if (handle)
                  CloseHandle(static_cast<HANDLE>(handle));
              });
        has_native_handle_ = true;
      }
#else
    native_handle_ = pthread_self();
    has_native_handle_ = true;
#endif
  }

  [[nodiscard]] auto
  has_native_handle() const noexcept -> bool
  {
    return has_native_handle_;
  }

  [[nodiscard]] auto
  native_handle() const noexcept -> native_handle_type
  {
    return native_handle_;
  }

  native_thread_id tid_{};
#ifdef _WIN32
  native_handle_type native_handle_ = nullptr;
  std::shared_ptr<void> native_handle_owner_;
#else
  native_handle_type native_handle_{};
#endif
  bool has_native_handle_{ false };
};

} // namespace threadschedule
