#pragma once

#include "concepts.hpp"
#include "scheduler_policy.hpp"
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>

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
 * @brief RAII pthread wrapper with modern C++ interface
 */
class PThreadWrapper
{
  public:
    using native_handle_type = pthread_t;
    using id                 = pthread_t;

    PThreadWrapper() : thread_(0), joined_(false)
    {
    }

    template <typename F, typename... Args>
    explicit PThreadWrapper(F &&func, Args &&...args) : thread_(0), joined_(false)
    {

        // Store the callable in a way pthread can handle
        auto callable =
            std::make_unique<std::function<void()>>(std::bind(std::forward<F>(func), std::forward<Args>(args)...));

        const int result = pthread_create(&thread_, nullptr, thread_function, callable.release());

        if (result != 0)
        {
            throw std::runtime_error("Failed to create pthread: " + std::to_string(result));
        }
    }

    // Non-copyable
    PThreadWrapper(const PThreadWrapper &)            = delete;
    PThreadWrapper &operator=(const PThreadWrapper &) = delete;

    // Movable
    PThreadWrapper(PThreadWrapper &&other) noexcept : thread_(other.thread_), joined_(other.joined_.load())
    {
        other.thread_ = 0;
        other.joined_.store(true);
    }

    PThreadWrapper &operator=(PThreadWrapper &&other) noexcept
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
            void     *retval;
            const int result = pthread_join(thread_, &retval);
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
            const int result = pthread_detach(thread_);
            if (result == 0)
            {
                joined_ = true;
            }
        }
    }

    bool joinable() const
    {
        return thread_ != 0 && !joined_;
    }

    id get_id() const
    {
        return thread_;
    }
    native_handle_type native_handle()
    {
        return thread_;
    }

    // Extended pthread functionality
    bool set_name(const std::string &name)
    {
        if (name.length() > 15)
            return false; // Linux limit
        return pthread_setname_np(thread_, name.c_str()) == 0;
    }

    std::optional<std::string> get_name() const
    {
        char name[16]; // Linux limit + 1
        if (pthread_getname_np(thread_, name, sizeof(name)) == 0)
        {
            return std::string(name);
        }
        return std::nullopt;
    }

    bool set_priority(ThreadPriority priority)
    {
        const int policy        = SCHED_OTHER;
        auto      params_result = SchedulerParams::create_for_policy(SchedulingPolicy::OTHER, priority);

        if (!params_result.has_value())
        {
            return false;
        }

        return pthread_setschedparam(thread_, policy, &params_result.value()) == 0;
    }

    bool set_scheduling_policy(SchedulingPolicy policy, ThreadPriority priority)
    {
        const int policy_int    = static_cast<int>(policy);
        auto      params_result = SchedulerParams::create_for_policy(policy, priority);

        if (!params_result.has_value())
        {
            return false;
        }

        return pthread_setschedparam(thread_, policy_int, &params_result.value()) == 0;
    }

    bool set_affinity(const ThreadAffinity &affinity)
    {
        return pthread_setaffinity_np(thread_, sizeof(cpu_set_t), &affinity.native_handle()) == 0;
    }

    std::optional<ThreadAffinity> get_affinity() const
    {
        ThreadAffinity affinity;
        if (pthread_getaffinity_np(thread_, sizeof(cpu_set_t), const_cast<cpu_set_t *>(&affinity.native_handle())) == 0)
        {
            return affinity;
        }
        return std::nullopt;
    }

    // Cancellation support
    bool cancel()
    {
        return pthread_cancel(thread_) == 0;
    }

    bool set_cancel_state(bool enabled)
    {
        const int state = enabled ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE;
        int       old_state;
        return pthread_setcancelstate(state, &old_state) == 0;
    }

    bool set_cancel_type(bool asynchronous)
    {
        const int type = asynchronous ? PTHREAD_CANCEL_ASYNCHRONOUS : PTHREAD_CANCEL_DEFERRED;
        int       old_type;
        return pthread_setcanceltype(type, &old_type) == 0;
    }

    // Factory methods
    template <typename F, typename... Args>
    static PThreadWrapper create_with_config(
        const std::string &name, SchedulingPolicy policy, ThreadPriority priority, F &&f, Args &&...args
    )
    {

        PThreadWrapper wrapper(std::forward<F>(f), std::forward<Args>(args)...);
        wrapper.set_name(name);
        wrapper.set_scheduling_policy(policy, priority);
        return wrapper;
    }

    template <typename F, typename... Args>
    static PThreadWrapper create_with_attributes(const pthread_attr_t &attr, F &&func, Args &&...args)
    {

        PThreadWrapper wrapper;
        auto           callable =
            std::make_unique<std::function<void()>>(std::bind(std::forward<F>(func), std::forward<Args>(args)...));

        const int result = pthread_create(&wrapper.thread_, &attr, thread_function, callable.release());

        if (result != 0)
        {
            throw std::runtime_error("Failed to create pthread with attributes: " + std::to_string(result));
        }

        return wrapper;
    }

  private:
    pthread_t         thread_;
    std::atomic<bool> joined_;

    static void *thread_function(void *arg)
    {
        std::unique_ptr<std::function<void()>> func(static_cast<std::function<void()> *>(arg));

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
 * @brief RAII pthread attribute wrapper
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
    PThreadAttributes(const PThreadAttributes &)            = delete;
    PThreadAttributes &operator=(const PThreadAttributes &) = delete;

    // Movable
    PThreadAttributes(PThreadAttributes &&other) noexcept : attr_(other.attr_)
    {
        if (pthread_attr_init(&other.attr_) != 0)
        {
            std::terminate(); // Can't throw from move constructor
        }
    }

    PThreadAttributes &operator=(PThreadAttributes &&other) noexcept
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

    const pthread_attr_t &get() const
    {
        return attr_;
    }
    pthread_attr_t &get()
    {
        return attr_;
    }

    // Attribute setters
    bool set_detach_state(bool detached)
    {
        const int state = detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
        return pthread_attr_setdetachstate(&attr_, state) == 0;
    }

    bool set_stack_size(size_t stack_size)
    {
        return pthread_attr_setstacksize(&attr_, stack_size) == 0;
    }

    bool set_guard_size(size_t guard_size)
    {
        return pthread_attr_setguardsize(&attr_, guard_size) == 0;
    }

    bool set_scheduling_policy(SchedulingPolicy policy)
    {
        const int policy_int = static_cast<int>(policy);
        return pthread_attr_setschedpolicy(&attr_, policy_int) == 0;
    }

    bool set_scheduling_parameter(ThreadPriority priority)
    {
        sched_param param{};
        param.sched_priority = priority.value();
        return pthread_attr_setschedparam(&attr_, &param) == 0;
    }

    bool set_inherit_sched(bool inherit)
    {
        const int inherit_val = inherit ? PTHREAD_INHERIT_SCHED : PTHREAD_EXPLICIT_SCHED;
        return pthread_attr_setinheritsched(&attr_, inherit_val) == 0;
    }

    bool set_scope(bool system_scope)
    {
        const int scope = system_scope ? PTHREAD_SCOPE_SYSTEM : PTHREAD_SCOPE_PROCESS;
        return pthread_attr_setscope(&attr_, scope) == 0;
    }

    // Attribute getters
    std::optional<bool> get_detach_state() const
    {
        int state;
        if (pthread_attr_getdetachstate(&attr_, &state) == 0)
        {
            return state == PTHREAD_CREATE_DETACHED;
        }
        return std::nullopt;
    }

    std::optional<size_t> get_stack_size() const
    {
        size_t stack_size;
        if (pthread_attr_getstacksize(&attr_, &stack_size) == 0)
        {
            return stack_size;
        }
        return std::nullopt;
    }

    std::optional<size_t> get_guard_size() const
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
 * @brief RAII pthread mutex wrapper
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

    explicit PThreadMutex(const pthread_mutexattr_t *attr)
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

    PThreadMutex(const PThreadMutex &)            = delete;
    PThreadMutex &operator=(const PThreadMutex &) = delete;

    void lock()
    {
        if (pthread_mutex_lock(&mutex_) != 0)
        {
            throw std::runtime_error("Failed to lock pthread mutex");
        }
    }

    bool try_lock()
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

    pthread_mutex_t *native_handle()
    {
        return &mutex_;
    }

  private:
    pthread_mutex_t mutex_;
};

#endif // !_WIN32

} // namespace threadschedule
