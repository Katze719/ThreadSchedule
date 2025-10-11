#pragma once

#include "expected.hpp"
#include "scheduler_policy.hpp"
#include <optional>
#include <string>
#include <thread>

#ifdef _WIN32
#include <libloaderapi.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fstream>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace threadschedule
{

namespace detail
{
struct OwningTag
{
};
struct NonOwningTag
{
};

template <typename ThreadType, typename OwnershipTag>
class ThreadStorage;

// Owning storage: no extra overhead
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

// Non-owning storage: reference to external thread
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
 * @brief Base thread wrapper with common functionality
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

    // Extended functionality
    [[nodiscard]] auto set_name(std::string const& name) -> expected<void, std::error_code>
    {
#ifdef _WIN32
        // Windows supports longer thread names. Try SetThreadDescription dynamically.
        auto const handle = native_handle();
        std::wstring wide_name(name.begin(), name.end());

        using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
        HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
        if (hMod)
        {
            auto set_desc = reinterpret_cast<SetThreadDescriptionFn>(
                reinterpret_cast<void*>(GetProcAddress(hMod, "SetThreadDescription")));
            if (set_desc)
            {
                if (SUCCEEDED(set_desc(handle, wide_name.c_str())))
                    return expected<void, std::error_code>();
                return expected<void, std::error_code>(unexpect, std::make_error_code(std::errc::invalid_argument));
            }
        }
        // Fallback unavailable
        return expected<void, std::error_code>(unexpect, std::make_error_code(std::errc::function_not_supported));
#else
        if (name.length() > 15)
            return expected<void, std::error_code>(unexpect, std::make_error_code(std::errc::invalid_argument));

        auto const handle = native_handle();
        if (pthread_setname_np(handle, name.c_str()) == 0)
            return {};
        return expected<void, std::error_code>(unexpect, std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto get_name() const -> std::optional<std::string>
    {
#ifdef _WIN32
        const auto handle = const_cast<BaseThreadWrapper*>(this)->native_handle();
        using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
        HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
        if (hMod)
        {
            auto get_desc = reinterpret_cast<GetThreadDescriptionFn>(
                reinterpret_cast<void*>(GetProcAddress(hMod, "GetThreadDescription")));
            if (get_desc)
            {
                PWSTR thread_name = nullptr;
                HRESULT hr = get_desc(handle, &thread_name);
                if (SUCCEEDED(hr) && thread_name)
                {
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
            }
        }
        return std::nullopt;
#else
        char name[16]; // Linux limit + 1
        auto const handle = const_cast<BaseThreadWrapper*>(this)->native_handle();

        if (pthread_getname_np(handle, name, sizeof(name)) == 0)
        {
            return std::string(name);
        }
        return std::nullopt;
#endif
    }

    [[nodiscard]] auto set_priority(ThreadPriority priority) -> expected<void, std::error_code>
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

        if (SetThreadPriority(handle, win_priority) != 0)
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
#else
        const auto handle = native_handle();
        int const policy = SCHED_OTHER;

        auto params_result = SchedulerParams::create_for_policy(SchedulingPolicy::OTHER, priority);

        if (!params_result.has_value())
        {
            return unexpected(params_result.error());
        }

        if (pthread_setschedparam(handle, policy, &params_result.value()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto set_scheduling_policy(SchedulingPolicy policy, ThreadPriority priority)
        -> expected<void, std::error_code>
    {
#ifdef _WIN32
        // Windows doesn't have the same scheduling policy concept as Linux
        // We'll just set the priority and return success
        return set_priority(priority);
#else
        const auto handle = native_handle();
        int const policy_int = static_cast<int>(policy);

        auto params_result = SchedulerParams::create_for_policy(policy, priority);
        if (!params_result.has_value())
        {
            return unexpected(params_result.error());
        }

        if (pthread_setschedparam(handle, policy_int, &params_result.value()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto set_affinity(ThreadAffinity const& affinity) -> expected<void, std::error_code>
    {
#ifdef _WIN32
        const auto handle = native_handle();
        // Prefer Group Affinity if available
        using SetThreadGroupAffinityFn = BOOL(WINAPI*)(HANDLE, const GROUP_AFFINITY*, PGROUP_AFFINITY);
        HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
        if (hMod)
        {
            auto set_group_affinity = reinterpret_cast<SetThreadGroupAffinityFn>(
                reinterpret_cast<void*>(GetProcAddress(hMod, "SetThreadGroupAffinity")));
            if (set_group_affinity && affinity.has_any())
            {
                GROUP_AFFINITY ga{};
                ga.Mask = static_cast<KAFFINITY>(affinity.get_mask());
                ga.Group = affinity.get_group();
                if (set_group_affinity(handle, &ga, nullptr) != 0)
                    return {};
                return unexpected(std::make_error_code(std::errc::operation_not_permitted));
            }
        }
        // Fallback to legacy mask (single-group systems)
        DWORD_PTR mask = static_cast<DWORD_PTR>(affinity.get_mask());
        if (SetThreadAffinityMask(handle, mask) != 0)
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
#else
        const auto handle = native_handle();
        if (pthread_setaffinity_np(handle, sizeof(cpu_set_t), &affinity.native_handle()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto get_affinity() const -> std::optional<ThreadAffinity>
    {
#ifdef _WIN32
        // Windows doesn't have a direct API to get thread affinity
        // We can only set it, not get it reliably
        // Return nullopt to indicate this is not supported on Windows
        return std::nullopt;
#else
        ThreadAffinity affinity;
        auto const handle = const_cast<BaseThreadWrapper*>(this)->native_handle();

        if (pthread_getaffinity_np(handle, sizeof(cpu_set_t), &affinity.native_handle()) == 0)
        {
            return affinity;
        }
        return std::nullopt;
#endif
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
 * @brief Enhanced std::thread wrapper
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
        if (auto r = wrapper.set_name(name); !r.has_value())
        {
        }
        if (auto r = wrapper.set_scheduling_policy(policy, priority); !r.has_value())
        {
        }
        return wrapper;
    }
};

// Non-owning view over std::thread
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
 * @brief Enhanced std::jthread wrapper (C++20)
 */
#if __cplusplus >= 202002L
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
    JThreadWrapper& operator=(JThreadWrapper const&) = delete;

    JThreadWrapper(JThreadWrapper&& other) noexcept : BaseThreadWrapper()
    {
        this->underlying() = std::move(other.underlying());
    }

    JThreadWrapper& operator=(JThreadWrapper&& other) noexcept
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
    bool stop_requested()
    {
        return this->underlying().get_stop_token().stop_requested();
    }
    std::stop_token get_stop_token() const
    {
        return this->underlying().get_stop_token();
    }
    std::stop_source get_stop_source()
    {
        return this->underlying().get_stop_source();
    }

    // Factory methods
    template <typename F, typename... Args>
    static JThreadWrapper create_with_config(std::string const& name, SchedulingPolicy policy, ThreadPriority priority,
                                             F&& f, Args&&... args)
    {

        JThreadWrapper wrapper(std::forward<F>(f), std::forward<Args>(args)...);
        if (auto r = wrapper.set_name(name); !r.has_value())
        {
        }
        if (auto r = wrapper.set_scheduling_policy(policy, priority); !r.has_value())
        {
        }
        return wrapper;
    }
};

// Non-owning view over std::jthread (C++20)
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
    bool stop_requested()
    {
        return this->underlying().get_stop_token().stop_requested();
    }
    std::stop_token get_stop_token() const
    {
        return this->underlying().get_stop_token();
    }
    std::stop_source get_stop_source()
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
#endif

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
        int const policy = SCHED_OTHER;
        auto params_result = SchedulerParams::create_for_policy(SchedulingPolicy::OTHER, priority);
        if (!params_result.has_value())
            return unexpected(params_result.error());
        if (sched_setscheduler(handle_, policy, &params_result.value()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
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
        int policy_int = static_cast<int>(policy);
        auto params_result = SchedulerParams::create_for_policy(policy, priority);
        if (!params_result.has_value())
            return unexpected(params_result.error());
        if (sched_setscheduler(handle_, policy_int, &params_result.value()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto set_affinity(ThreadAffinity const& affinity) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        return unexpected(std::make_error_code(std::errc::function_not_supported));
#else
        if (!found())
            return unexpected(std::make_error_code(std::errc::no_such_process));
        if (sched_setaffinity(handle_, sizeof(cpu_set_t), &affinity.native_handle()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

  private:
#ifdef _WIN32
    native_handle_type handle_ = nullptr;
#else
    native_handle_type handle_ = 0;
#endif
};

// Static hardware information
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
