#pragma once

/**
 * @file pthread_wrapper.hpp
 * @brief RAII wrapper around POSIX threads (Linux only).
 */

#include "concepts.hpp"
#include "expected.hpp"
#include "scheduler_policy.hpp"
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#ifdef _WIN32
#include <windows.h>
// PThreadWrapper is not available on Windows as it's POSIX-specific
// Users should use ThreadWrapper or JThreadWrapper instead
#else
#include <pthread.h>
#endif

namespace threadschedule
{

#ifndef _WIN32
/**
 * @brief RAII wrapper around POSIX threads with a modern C++ interface.
 *
 * Linux-only - not available on Windows (guarded by @c _WIN32).
 *
 * Non-copyable, movable. The destructor automatically joins the thread
 * if it is still joinable, which **blocks** until the thread finishes.
 *
 * Internally stores the callable in a heap-allocated @c std::function
 * so that it can be passed through the C @c pthread_create API.
 *
 * @note Thread names are limited to 15 characters on Linux
 *       (enforced by @c pthread_setname_np).
 * @note cancel() sends a POSIX cancellation request to the thread.
 *       set_cancel_state() and set_cancel_type() are @c static and
 *       affect the **calling** thread, not the PThreadWrapper's thread.
 *
 * @par Factory methods
 * - create_with_config()      - creates a thread and applies name/policy/priority.
 * - create_with_attributes()  - creates a thread from a raw @c pthread_attr_t.
 *
 * @see is_thread_like<PThreadWrapper> (specialised to @c true_type at end of file)
 */
class PThreadWrapper
{
  public:
    using native_handle_type = pthread_t;
    using id = pthread_t;

    PThreadWrapper() : thread_(0), joined_(false)
    {
    }

    template <typename F, typename... Args>
    explicit PThreadWrapper(F&& func, Args&&... args) : thread_(0), joined_(false)
    {

        auto callable =
            std::make_unique<std::function<void()>>([fn = std::forward<F>(func),
                                                     tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                std::apply(std::move(fn), std::move(tup));
            });

        int const result = pthread_create(&thread_, nullptr, thread_function, callable.release());

        if (result != 0)
        {
            throw std::runtime_error("Failed to create pthread: " + std::to_string(result));
        }
    }

    // Non-copyable
    PThreadWrapper(PThreadWrapper const&) = delete;
    auto operator=(PThreadWrapper const&) -> PThreadWrapper& = delete;

    // Movable
    PThreadWrapper(PThreadWrapper&& other) noexcept : thread_(other.thread_), joined_(other.joined_.load())
    {
        other.thread_ = 0;
        other.joined_.store(true);
    }

    auto operator=(PThreadWrapper&& other) noexcept -> PThreadWrapper&
    {
        if (this != &other)
        {
            if (joinable())
            {
                join();
            }
            thread_ = other.thread_;
            joined_.store(other.joined_.load());
            other.thread_ = 0;
            other.joined_.store(true);
        }
        return *this;
    }

    ~PThreadWrapper()
    {
        if (joinable())
        {
            join();
        }
    }

    // Thread management
    void join()
    {
        if (joinable())
        {
            void* retval;
            int const result = pthread_join(thread_, &retval);
            if (result == 0)
            {
                joined_ = true;
            }
        }
    }

    void detach()
    {
        if (joinable())
        {
            int const result = pthread_detach(thread_);
            if (result == 0)
            {
                joined_ = true;
            }
        }
    }

    [[nodiscard]] auto joinable() const -> bool
    {
        return thread_ != 0 && !joined_;
    }

    [[nodiscard]] auto get_id() const -> id
    {
        return thread_;
    }
    [[nodiscard]] auto native_handle() const -> native_handle_type
    {
        return thread_;
    }

    [[nodiscard]] auto set_name(std::string const& name) const -> expected<void, std::error_code>
    {
        return detail::apply_name(thread_, name);
    }

    [[nodiscard]] auto get_name() const -> std::optional<std::string>
    {
        return detail::read_name(thread_);
    }

    [[nodiscard]] auto set_priority(ThreadPriority priority) const -> expected<void, std::error_code>
    {
        return detail::apply_priority(thread_, priority);
    }

    [[nodiscard]] auto set_scheduling_policy(SchedulingPolicy policy, ThreadPriority priority) const
        -> expected<void, std::error_code>
    {
        return detail::apply_scheduling_policy(thread_, policy, priority);
    }

    [[nodiscard]] auto set_affinity(ThreadAffinity const& affinity) const -> expected<void, std::error_code>
    {
        return detail::apply_affinity(thread_, affinity);
    }

    [[nodiscard]] auto get_affinity() const -> std::optional<ThreadAffinity>
    {
        return detail::read_affinity(thread_);
    }

    // Cancellation support
    [[nodiscard]] auto cancel() const -> expected<void, std::error_code>
    {
        if (pthread_cancel(thread_) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
    }

    static auto set_cancel_state(bool enabled) -> expected<void, std::error_code>
    {
        int const state = enabled ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE;
        int old_state;
        if (pthread_setcancelstate(state, &old_state) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
    }

    static auto set_cancel_type(bool asynchronous) -> expected<void, std::error_code>
    {
        int const type = asynchronous ? PTHREAD_CANCEL_ASYNCHRONOUS : PTHREAD_CANCEL_DEFERRED;
        int old_type;
        if (pthread_setcanceltype(type, &old_type) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
    }

    // Factory methods
    template <typename F, typename... Args>
    static auto create_with_config(std::string const& name, SchedulingPolicy policy, ThreadPriority priority, F&& f,
                                   Args&&... args) -> PThreadWrapper
    {

        PThreadWrapper wrapper(std::forward<F>(f), std::forward<Args>(args)...);
        (void)wrapper.set_name(name);
        (void)wrapper.set_scheduling_policy(policy, priority);
        return wrapper;
    }

    template <typename F, typename... Args>
    static auto create_with_attributes(pthread_attr_t const& attr, F&& func, Args&&... args) -> PThreadWrapper
    {

        PThreadWrapper wrapper;
        auto callable =
            std::make_unique<std::function<void()>>([fn = std::forward<F>(func),
                                                     tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                std::apply(std::move(fn), std::move(tup));
            });

        int const result = pthread_create(&wrapper.thread_, &attr, thread_function, callable.release());

        if (result != 0)
        {
            throw std::runtime_error("Failed to create pthread with attributes: " + std::to_string(result));
        }

        return wrapper;
    }

  private:
    pthread_t thread_;
    std::atomic<bool> joined_;

    static auto thread_function(void* arg) -> void*
    {
        std::unique_ptr<std::function<void()>> func(static_cast<std::function<void()>*>(arg));

        try
        {
            (*func)();
        }
        catch (...)
        {
            // Handle exceptions - could add logging here
        }

        return nullptr;
    }
};

/**
 * @brief RAII wrapper for @c pthread_attr_t with a builder-style API.
 *
 * Non-copyable, movable. The move constructor and move assignment
 * operator call @c std::terminate if the re-initialisation of the
 * moved-from attribute object fails (cannot throw from @c noexcept).
 *
 * The destructor always calls @c pthread_attr_destroy.
 *
 * Provides fluent setters for detach state, stack size, guard size,
 * scheduling policy, priority, inherit-sched, and contention scope.
 */
class PThreadAttributes
{
  public:
    PThreadAttributes()
    {
        if (pthread_attr_init(&attr_) != 0)
        {
            throw std::runtime_error("Failed to initialize pthread attributes");
        }
    }

    ~PThreadAttributes()
    {
        pthread_attr_destroy(&attr_);
    }

    // Non-copyable
    PThreadAttributes(PThreadAttributes const&) = delete;
    auto operator=(PThreadAttributes const&) -> PThreadAttributes& = delete;

    // Movable
    PThreadAttributes(PThreadAttributes&& other) noexcept : attr_(other.attr_)
    {
        if (pthread_attr_init(&other.attr_) != 0)
        {
            std::terminate(); // Can't throw from move constructor
        }
    }

    auto operator=(PThreadAttributes&& other) noexcept -> PThreadAttributes&
    {
        if (this != &other)
        {
            pthread_attr_destroy(&attr_);
            attr_ = other.attr_;
            if (pthread_attr_init(&other.attr_) != 0)
            {
                std::terminate(); // Can't throw from move assignment
            }
        }
        return *this;
    }

    [[nodiscard]] auto get() const -> pthread_attr_t const&
    {
        return attr_;
    }
    auto get() -> pthread_attr_t&
    {
        return attr_;
    }

    // Attribute setters
    auto set_detach_state(bool detached) -> bool
    {
        int const state = detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
        return pthread_attr_setdetachstate(&attr_, state) == 0;
    }

    auto set_stack_size(size_t stack_size) -> bool
    {
        return pthread_attr_setstacksize(&attr_, stack_size) == 0;
    }

    auto set_guard_size(size_t guard_size) -> bool
    {
        return pthread_attr_setguardsize(&attr_, guard_size) == 0;
    }

    auto set_scheduling_policy(SchedulingPolicy policy) -> bool
    {
        int const policy_int = static_cast<int>(policy);
        return pthread_attr_setschedpolicy(&attr_, policy_int) == 0;
    }

    auto set_scheduling_parameter(ThreadPriority priority) -> bool
    {
        sched_param param{};
        param.sched_priority = priority.value();
        return pthread_attr_setschedparam(&attr_, &param) == 0;
    }

    auto set_inherit_sched(bool inherit) -> bool
    {
        int const inherit_val = inherit ? PTHREAD_INHERIT_SCHED : PTHREAD_EXPLICIT_SCHED;
        return pthread_attr_setinheritsched(&attr_, inherit_val) == 0;
    }

    auto set_scope(bool system_scope) -> bool
    {
        int const scope = system_scope ? PTHREAD_SCOPE_SYSTEM : PTHREAD_SCOPE_PROCESS;
        return pthread_attr_setscope(&attr_, scope) == 0;
    }

    // Attribute getters
    [[nodiscard]] auto get_detach_state() const -> std::optional<bool>
    {
        int state;
        if (pthread_attr_getdetachstate(&attr_, &state) == 0)
        {
            return state == PTHREAD_CREATE_DETACHED;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto get_stack_size() const -> std::optional<size_t>
    {
        size_t stack_size;
        if (pthread_attr_getstacksize(&attr_, &stack_size) == 0)
        {
            return stack_size;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto get_guard_size() const -> std::optional<size_t>
    {
        size_t guard_size;
        if (pthread_attr_getguardsize(&attr_, &guard_size) == 0)
        {
            return guard_size;
        }
        return std::nullopt;
    }

  private:
    pthread_attr_t attr_;
};

/**
 * @brief RAII wrapper for @c pthread_mutex_t.
 *
 * Non-copyable, **non-movable**. Satisfies the @e BasicLockable
 * named requirement (lock / unlock / try_lock), so it can be used
 * with @c std::lock_guard and similar RAII lock holders.
 *
 * @note The constructor throws @c std::runtime_error if
 *       @c pthread_mutex_init fails. Unusually for a mutex type,
 *       lock() and unlock() also throw on error - callers should be
 *       aware of this when mixing with code that assumes non-throwing
 *       mutex operations.
 */
class PThreadMutex
{
  public:
    PThreadMutex()
    {
        if (pthread_mutex_init(&mutex_, nullptr) != 0)
        {
            throw std::runtime_error("Failed to initialize pthread mutex");
        }
    }

    explicit PThreadMutex(pthread_mutexattr_t const* attr)
    {
        if (pthread_mutex_init(&mutex_, attr) != 0)
        {
            throw std::runtime_error("Failed to initialize pthread mutex with attributes");
        }
    }

    ~PThreadMutex()
    {
        pthread_mutex_destroy(&mutex_);
    }

    PThreadMutex(PThreadMutex const&) = delete;
    auto operator=(PThreadMutex const&) -> PThreadMutex& = delete;

    void lock()
    {
        if (pthread_mutex_lock(&mutex_) != 0)
        {
            throw std::runtime_error("Failed to lock pthread mutex");
        }
    }

    auto try_lock() -> bool
    {
        return pthread_mutex_trylock(&mutex_) == 0;
    }

    void unlock()
    {
        if (pthread_mutex_unlock(&mutex_) != 0)
        {
            throw std::runtime_error("Failed to unlock pthread mutex");
        }
    }

    auto native_handle() -> pthread_mutex_t*
    {
        return &mutex_;
    }

  private:
    pthread_mutex_t mutex_;
};

template <>
struct is_thread_like<PThreadWrapper> : std::true_type
{
};

#endif // !_WIN32

} // namespace threadschedule
