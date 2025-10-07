#pragma once

#include "concepts.hpp"
#include "scheduler_policy.hpp"
#include <chrono>
#include <optional>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

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
#ifdef _WIN32
        // Windows supports longer thread names
        const auto   handle = native_handle();
        std::wstring wide_name(name.begin(), name.end());
        HRESULT      hr = SetThreadDescription(handle, wide_name.c_str());
        return SUCCEEDED(hr);
#else
        if (name.length() > 15)
            return false; // Linux limit

        const auto handle = native_handle();
        return pthread_setname_np(handle, name.c_str()) == 0;
#endif
    }

    std::optional<std::string> get_name() const
    {
#ifdef _WIN32
        const auto handle = const_cast<BaseThreadWrapper *>(this)->native_handle();
        PWSTR      thread_name;
        HRESULT    hr = GetThreadDescription(handle, &thread_name);
        if (SUCCEEDED(hr))
        {
            // Convert wide string to narrow string
            int size = WideCharToMultiByte(CP_UTF8, 0, thread_name, -1, nullptr, 0, nullptr, nullptr);
            if (size > 0)
            {
                std::string result(size - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, thread_name, -1, &result[0], size, nullptr, nullptr);
                LocalFree(thread_name);
                return result;
            }
            LocalFree(thread_name);
        }
        return std::nullopt;
#else
        char       name[16]; // Linux limit + 1
        const auto handle = const_cast<BaseThreadWrapper *>(this)->native_handle();

        if (pthread_getname_np(handle, name, sizeof(name)) == 0)
        {
            return std::string(name);
        }
        return std::nullopt;
#endif
    }

    bool set_priority(ThreadPriority priority)
    {
#ifdef _WIN32
        const auto handle = native_handle();
        // Map ThreadPriority to Windows priority
        // Windows thread priorities range from -15 (THREAD_PRIORITY_IDLE) to +15 (THREAD_PRIORITY_TIME_CRITICAL)
        // We'll map the priority value to Windows constants
        int win_priority;
        int prio_val = priority.value();

        if (prio_val <= -10)
        {
            win_priority = THREAD_PRIORITY_IDLE;
        }
        else if (prio_val <= -5)
        {
            win_priority = THREAD_PRIORITY_LOWEST;
        }
        else if (prio_val < 0)
        {
            win_priority = THREAD_PRIORITY_BELOW_NORMAL;
        }
        else if (prio_val == 0)
        {
            win_priority = THREAD_PRIORITY_NORMAL;
        }
        else if (prio_val <= 5)
        {
            win_priority = THREAD_PRIORITY_ABOVE_NORMAL;
        }
        else if (prio_val <= 10)
        {
            win_priority = THREAD_PRIORITY_HIGHEST;
        }
        else
        {
            win_priority = THREAD_PRIORITY_TIME_CRITICAL;
        }

        return SetThreadPriority(handle, win_priority) != 0;
#else
        const auto handle = native_handle();
        const int  policy = SCHED_OTHER;

        auto params_result = SchedulerParams::create_for_policy(SchedulingPolicy::OTHER, priority);

        if (!params_result.has_value())
        {
            return false;
        }

        return pthread_setschedparam(handle, policy, &params_result.value()) == 0;
#endif
    }

    bool set_scheduling_policy(SchedulingPolicy policy, ThreadPriority priority)
    {
#ifdef _WIN32
        // Windows doesn't have the same scheduling policy concept as Linux
        // We'll just set the priority and return success
        return set_priority(priority);
#else
        const auto handle     = native_handle();
        const int  policy_int = static_cast<int>(policy);

        auto params_result = SchedulerParams::create_for_policy(policy, priority);
        if (!params_result.has_value())
        {
            return false;
        }

        return pthread_setschedparam(handle, policy_int, &params_result.value()) == 0;
#endif
    }

    bool set_affinity(const ThreadAffinity &affinity)
    {
#ifdef _WIN32
        const auto handle = native_handle();
        // Windows uses DWORD_PTR for affinity mask
        DWORD_PTR mask = affinity.get_mask();
        return SetThreadAffinityMask(handle, mask) != 0;
#else
        const auto handle = native_handle();
        return pthread_setaffinity_np(handle, sizeof(cpu_set_t), &affinity.native_handle()) == 0;
#endif
    }

    std::optional<ThreadAffinity> get_affinity() const
    {
#ifdef _WIN32
        // Windows doesn't have a direct API to get thread affinity
        // We can only set it, not get it reliably
        // Return nullopt to indicate this is not supported on Windows
        return std::nullopt;
#else
        ThreadAffinity affinity;
        const auto     handle = const_cast<BaseThreadWrapper *>(this)->native_handle();

        if (pthread_getaffinity_np(handle, sizeof(cpu_set_t), &affinity.native_handle()) == 0)
        {
            return affinity;
        }
        return std::nullopt;
#endif
    }

    // Nice value (process-level, affects all threads)
    static bool set_nice_value(int nice_value)
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

    static std::optional<int> get_nice_value()
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
        errno          = 0;
        const int nice = getpriority(PRIO_PROCESS, 0);
        if (errno == 0)
        {
            return nice;
        }
        return std::nullopt;
#endif
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

    template <typename F, typename... Args> explicit ThreadWrapper(F &&f, Args &&...args) : BaseThreadWrapper()
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
    template <typename F, typename... Args>
    static ThreadWrapper create_with_config(
        const std::string &name, SchedulingPolicy policy, ThreadPriority priority, F &&f, Args &&...args
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

    template <typename F, typename... Args> explicit JThreadWrapper(F &&f, Args &&...args) : BaseThreadWrapper()
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
    template <typename F, typename... Args>
    static JThreadWrapper create_with_config(
        const std::string &name, SchedulingPolicy policy, ThreadPriority priority, F &&f, Args &&...args
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

    static auto get_thread_id()
    {
#ifdef _WIN32
        return GetCurrentThreadId();
#else
        return static_cast<pid_t>(syscall(SYS_gettid));
#endif
    }

    static std::optional<SchedulingPolicy> get_current_policy()
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

    static std::optional<int> get_current_priority()
    {
#ifdef _WIN32
        HANDLE thread   = GetCurrentThread();
        int    priority = GetThreadPriority(thread);
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
