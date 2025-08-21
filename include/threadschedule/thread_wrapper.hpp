#pragma once

#include "concepts.hpp"
#include "scheduler_policy.hpp"
#include <chrono>
#include <optional>
#include <string>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace threadschedule
{

/**
 * @brief Base thread wrapper with common functionality
 */
template <typename ThreadType> class BaseThreadWrapper
{
  public:
    using native_handle_type = typename ThreadType::native_handle_type;
    using id                 = typename ThreadType::id;

    BaseThreadWrapper()          = default;
    virtual ~BaseThreadWrapper() = default;

    // Thread management
    void join()
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    void detach()
    {
        if (thread_.joinable())
        {
            thread_.detach();
        }
    }

    bool joinable() const
    {
        return thread_.joinable();
    }
    id get_id() const
    {
        return thread_.get_id();
    }
    native_handle_type native_handle()
    {
        return thread_.native_handle();
    }

    // Extended functionality
    bool set_name(const std::string &name)
    {
        if (name.length() > 15)
            return false; // Linux limit

        const auto handle = native_handle();
        return pthread_setname_np(handle, name.c_str()) == 0;
    }

    std::optional<std::string> get_name() const
    {
        char       name[16]; // Linux limit + 1
        const auto handle = const_cast<BaseThreadWrapper *>(this)->native_handle();

        if (pthread_getname_np(handle, name, sizeof(name)) == 0)
        {
            return std::string(name);
        }
        return std::nullopt;
    }

    bool set_priority(ThreadPriority priority)
    {
        const auto handle = native_handle();
        const int  policy = SCHED_OTHER;

        auto params_result = SchedulerParams::create_for_policy(SchedulingPolicy::OTHER, priority);

        if (!params_result.has_value())
        {
            return false;
        }

        return pthread_setschedparam(handle, policy, &params_result.value()) == 0;
    }

    bool set_scheduling_policy(
        SchedulingPolicy policy,
        ThreadPriority   priority
    )
    {
        const auto handle     = native_handle();
        const int  policy_int = static_cast<int>(policy);

        auto params_result = SchedulerParams::create_for_policy(policy, priority);
        if (!params_result.has_value())
        {
            return false;
        }

        return pthread_setschedparam(handle, policy_int, &params_result.value()) == 0;
    }

    bool set_affinity(const ThreadAffinity &affinity)
    {
        const auto handle = native_handle();
        return pthread_setaffinity_np(handle, sizeof(cpu_set_t), &affinity.native_handle()) == 0;
    }

    std::optional<ThreadAffinity> get_affinity() const
    {
        ThreadAffinity affinity;
        const auto     handle = const_cast<BaseThreadWrapper *>(this)->native_handle();

        if (pthread_getaffinity_np(handle, sizeof(cpu_set_t), &affinity.native_handle()) == 0)
        {
            return affinity;
        }
        return std::nullopt;
    }

    // Nice value (process-level, affects all threads)
    static bool set_nice_value(int nice_value)
    {
        return setpriority(PRIO_PROCESS, 0, nice_value) == 0;
    }

    static std::optional<int> get_nice_value()
    {
        errno          = 0;
        const int nice = getpriority(PRIO_PROCESS, 0);
        if (errno == 0)
        {
            return nice;
        }
        return std::nullopt;
    }

  protected:
    ThreadType thread_;
};

/**
 * @brief Enhanced std::thread wrapper
 */
class ThreadWrapper : public BaseThreadWrapper<std::thread>
{
  public:
    ThreadWrapper() = default;

    template <
        typename F,
        typename... Args>
    explicit ThreadWrapper(
        F &&f,
        Args &&...args
    )
        : BaseThreadWrapper()
    {
        thread_ = std::thread(std::forward<F>(f), std::forward<Args>(args)...);
    }

    ThreadWrapper(const ThreadWrapper &)            = delete;
    ThreadWrapper &operator=(const ThreadWrapper &) = delete;

    ThreadWrapper(ThreadWrapper &&other) noexcept : BaseThreadWrapper()
    {
        thread_ = std::move(other.thread_);
    }

    ThreadWrapper &operator=(ThreadWrapper &&other) noexcept
    {
        if (this != &other)
        {
            if (thread_.joinable())
            {
                thread_.join();
            }
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    ~ThreadWrapper()
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    // Factory methods
    template <
        typename F,
        typename... Args>
    static ThreadWrapper create_with_config(
        const std::string &name,
        SchedulingPolicy   policy,
        ThreadPriority     priority,
        F                &&f,
        Args &&...args
    )
    {

        ThreadWrapper wrapper(std::forward<F>(f), std::forward<Args>(args)...);
        wrapper.set_name(name);
        wrapper.set_scheduling_policy(policy, priority);
        return wrapper;
    }
};

/**
 * @brief Enhanced std::jthread wrapper (C++20)
 */
#if __cplusplus >= 202002L
class JThreadWrapper : public BaseThreadWrapper<std::jthread>
{
  public:
    JThreadWrapper() = default;

    template <
        typename F,
        typename... Args>
    explicit JThreadWrapper(
        F &&f,
        Args &&...args
    )
        : BaseThreadWrapper()
    {
        thread_ = std::jthread(std::forward<F>(f), std::forward<Args>(args)...);
    }

    JThreadWrapper(const JThreadWrapper &)            = delete;
    JThreadWrapper &operator=(const JThreadWrapper &) = delete;

    JThreadWrapper(JThreadWrapper &&other) noexcept : BaseThreadWrapper()
    {
        thread_ = std::move(other.thread_);
    }

    JThreadWrapper &operator=(JThreadWrapper &&other) noexcept
    {
        if (this != &other)
        {
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    // jthread-specific functionality
    void request_stop()
    {
        thread_.request_stop();
    }
    bool stop_requested()
    {
        return thread_.get_stop_token().stop_requested();
    }
    std::stop_token get_stop_token() const
    {
        return thread_.get_stop_token();
    }
    std::stop_source get_stop_source()
    {
        return thread_.get_stop_source();
    }

    // Factory methods
    template <
        typename F,
        typename... Args>
    static JThreadWrapper create_with_config(
        const std::string &name,
        SchedulingPolicy   policy,
        ThreadPriority     priority,
        F                &&f,
        Args &&...args
    )
    {

        JThreadWrapper wrapper(std::forward<F>(f), std::forward<Args>(args)...);
        wrapper.set_name(name);
        wrapper.set_scheduling_policy(policy, priority);
        return wrapper;
    }
};
#else
// Fallback for compilers without C++20 support
using JThreadWrapper = ThreadWrapper;
#endif

// Static hardware information
class ThreadInfo
{
  public:
    static unsigned int hardware_concurrency()
    {
        return std::thread::hardware_concurrency();
    }

    static pid_t get_thread_id()
    {
        return static_cast<pid_t>(syscall(SYS_gettid));
    }

    static std::optional<SchedulingPolicy> get_current_policy()
    {
        const int policy = sched_getscheduler(0);
        if (policy == -1)
        {
            return std::nullopt;
        }
        return static_cast<SchedulingPolicy>(policy);
    }

    static std::optional<int> get_current_priority()
    {
        sched_param param;
        if (sched_getparam(0, &param) == 0)
        {
            return param.sched_priority;
        }
        return std::nullopt;
    }
};

} // namespace threadschedule
