#pragma once

/**
 * @file scheduler_policy.hpp
 * @brief Scheduling policies, thread priority, and CPU affinity types.
 */

#include "expected.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#endif

namespace threadschedule
{
// expected/result are provided by expected.hpp

/**
 * @brief Enumeration of available thread scheduling policies.
 *
 * Represents the OS-level scheduling policy applied to a thread. On Linux, the
 * enumerator values map directly to the POSIX `SCHED_*` constants defined in
 * `<sched.h>`. On Windows, they are stored as portable integer values and
 * translated to Windows-specific priority classes / scheduling behaviour at the
 * point of application.
 *
 * ### Linux behaviour
 * | Policy     | Description                                                                 | Privileges required          |
 * |------------|-----------------------------------------------------------------------------|------------------------------|
 * | OTHER      | Default CFS (Completely Fair Scheduler) time-sharing.                       | None                         |
 * | FIFO       | Real-time FIFO - runs until it yields or a higher-priority thread arrives.  | `CAP_SYS_NICE` or root       |
 * | RR         | Real-time round-robin - like FIFO but with a per-thread time quantum.       | `CAP_SYS_NICE` or root       |
 * | BATCH      | Like OTHER but the scheduler assumes the thread is CPU-bound (longer slices).| None                        |
 * | IDLE       | Extremely low priority; runs only when no other runnable thread exists.      | None                        |
 * | DEADLINE   | EDF (Earliest Deadline First) real-time scheduling (Linux >= 3.14).          | `CAP_SYS_NICE` or root       |
 *
 * ### Windows behaviour
 * Windows does not expose POSIX scheduling policies. The library maps each
 * enumerator to an appropriate combination of process priority class and thread
 * priority level when applying the policy. FIFO and RR are both treated as
 * elevated real-time priorities; BATCH and IDLE are mapped to below-normal and
 * idle priority levels respectively.
 *
 * @note DEADLINE is only available on Linux when `SCHED_DEADLINE` is defined by
 *       the kernel headers. It is not available on Windows.
 *
 * @warning Setting FIFO, RR, or DEADLINE without adequate privileges will fail
 *          with a permission error (`EPERM` on Linux).
 */
enum class SchedulingPolicy : std::uint_fast8_t
{
#ifdef _WIN32
    // Windows doesn't have the same scheduling policies as Linux
    // We'll use generic values
    OTHER = 0, ///< Standard scheduling
    FIFO = 1,  ///< First in, first out
    RR = 2,    ///< Round-robin
    BATCH = 3, ///< For batch style execution
    IDLE = 4   ///< For very low priority background tasks
#else
    OTHER = SCHED_OTHER, ///< Standard round-robin time-sharing
    FIFO = SCHED_FIFO,   ///< First in, first out
    RR = SCHED_RR,       ///< Round-robin
    BATCH = SCHED_BATCH, ///< For batch style execution
    IDLE = SCHED_IDLE,   ///< For very low priority background tasks
#ifdef SCHED_DEADLINE
    DEADLINE = SCHED_DEADLINE ///< Real-time deadline scheduling
#endif
#endif
};

/**
 * @brief Value-semantic wrapper for a thread scheduling priority.
 *
 * Encapsulates a single integer priority in the range **[-20, 19]** - the same
 * range used by POSIX nice values on Linux. The value is silently clamped to
 * this range on construction (via `std::clamp`), so out-of-range inputs never
 * produce an invalid object.
 *
 * ### Semantics
 * Lower numeric values denote **higher** scheduling priority (following the
 * Unix nice convention): -20 is the most favourable and 19 is the least.
 *
 * ### Platform notes
 * - **Linux:** The value is used directly as the nice level for `SCHED_OTHER`
 *   / `SCHED_BATCH` / `SCHED_IDLE`, or clamped to the real-time priority
 *   range for `SCHED_FIFO` / `SCHED_RR` by SchedulerParams::create_for_policy().
 * - **Windows:** The value is mapped to a Windows thread priority constant
 *   (e.g. `THREAD_PRIORITY_HIGHEST`, `THREAD_PRIORITY_LOWEST`) when applied.
 *
 * ### Type traits
 * - Trivially copyable and trivially movable.
 * - `constexpr`-constructible - can be used in compile-time contexts.
 * - All relational operators (`==`, `!=`, `<`, `<=`, `>`, `>=`) are provided
 *   and compare the underlying integer value.
 * - Not thread-safe: concurrent mutation of the same instance requires
 *   external synchronisation. Distinct instances may be used freely from
 *   different threads.
 *
 * @see SchedulerParams::create_for_policy
 */
class ThreadPriority
{
  public:
    constexpr explicit ThreadPriority(int priority = 0) : priority_(std::clamp(priority, min_priority, max_priority))
    {
    }

    [[nodiscard]] constexpr auto value() const noexcept -> int
    {
        return priority_;
    }
    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool
    {
        return priority_ >= min_priority && priority_ <= max_priority;
    }

    [[nodiscard]] static constexpr auto lowest() noexcept -> ThreadPriority
    {
        return ThreadPriority(min_priority);
    }
    [[nodiscard]] static constexpr auto normal() noexcept -> ThreadPriority
    {
        return ThreadPriority(0);
    }
    [[nodiscard]] static constexpr auto highest() noexcept -> ThreadPriority
    {
        return ThreadPriority(max_priority);
    }

    [[nodiscard]] constexpr auto operator==(ThreadPriority const& other) const noexcept -> bool
    {
        return priority_ == other.priority_;
    }
    [[nodiscard]] constexpr auto operator!=(ThreadPriority const& other) const noexcept -> bool
    {
        return priority_ != other.priority_;
    }
    [[nodiscard]] constexpr auto operator<(ThreadPriority const& other) const noexcept -> bool
    {
        return priority_ < other.priority_;
    }
    [[nodiscard]] constexpr auto operator<=(ThreadPriority const& other) const noexcept -> bool
    {
        return priority_ <= other.priority_;
    }
    [[nodiscard]] constexpr auto operator>(ThreadPriority const& other) const noexcept -> bool
    {
        return priority_ > other.priority_;
    }
    [[nodiscard]] constexpr auto operator>=(ThreadPriority const& other) const noexcept -> bool
    {
        return priority_ >= other.priority_;
    }

    [[nodiscard]] auto to_string() const -> std::string
    {
        std::ostringstream oss;
        oss << "ThreadPriority(" << priority_ << ")";
        return oss.str();
    }

  private:
    static constexpr int min_priority = -20;
    static constexpr int max_priority = 19;
    int priority_;
};

/**
 * @brief Manages a set of CPU indices to which a thread may be bound.
 *
 * ThreadAffinity is a value-semantic type that represents a CPU affinity mask.
 * It abstracts away the platform-specific details of `cpu_set_t` (Linux) and
 * processor-group bitmasks (Windows).
 *
 * ### Linux
 * Backed by a `cpu_set_t`. Supports CPU indices in the range
 * `[0, CPU_SETSIZE)` (typically 0-1023). The `native_handle()` accessor
 * provides a `const cpu_set_t&` for direct use with `pthread_setaffinity_np`
 * or `sched_setaffinity`.
 *
 * ### Windows
 * Backed by a 64-bit bitmask plus a processor group index (`WORD`). Windows
 * organises logical processors into groups of up to 64. This class supports
 * **a single group at a time**: the group is determined by the first CPU added
 * via `add_cpu()`. Subsequent calls to `add_cpu()` for CPUs that belong to a
 * different group are **silently ignored**. Use `get_group()` and `get_mask()`
 * to retrieve the platform-native values for `SetThreadGroupAffinity`.
 *
 * ### Thread safety
 * None. ThreadAffinity is a plain value type with no internal synchronisation.
 * Concurrent reads are safe; concurrent mutation (or a read concurrent with a
 * write) requires external locking.
 *
 * ### Copyability / movability
 * Implicitly copyable and movable (compiler-generated special members).
 *
 * @warning On Windows, CPUs from different processor groups cannot be combined
 *          in a single ThreadAffinity instance. If you need cross-group
 *          affinity you must apply separate ThreadAffinity objects per group.
 */
class ThreadAffinity
{
  public:
    ThreadAffinity()
    {
#ifdef _WIN32
        group_ = 0;
        mask_ = 0;
#else
        CPU_ZERO(&cpuset_);
#endif
    }

    explicit ThreadAffinity(std::vector<int> const& cpus) : ThreadAffinity()
    {
        for (int cpu : cpus)
        {
            add_cpu(cpu);
        }
    }

    // Adds a CPU index. On Windows, indices >= 64 select group = cpu/64 automatically.
    void add_cpu(int cpu)
    {
#ifdef _WIN32
        if (cpu < 0)
            return;
        WORD g = static_cast<WORD>(cpu / 64);
        int bit = cpu % 64;
        if (!has_any())
        {
            group_ = g;
        }
        if (g != group_)
        {
            // Single-group affinity object: ignore CPUs from other groups
            return;
        }
        mask_ |= (static_cast<unsigned long long>(1) << bit);
#else
        if (cpu >= 0 && cpu < CPU_SETSIZE)
        {
            CPU_SET(cpu, &cpuset_);
        }
#endif
    }

    void remove_cpu(int cpu)
    {
#ifdef _WIN32
        if (cpu < 0)
            return;
        WORD g = static_cast<WORD>(cpu / 64);
        int bit = cpu % 64;
        if (g == group_)
        {
            mask_ &= ~(static_cast<unsigned long long>(1) << bit);
        }
#else
        if (cpu >= 0 && cpu < CPU_SETSIZE)
        {
            CPU_CLR(cpu, &cpuset_);
        }
#endif
    }

    [[nodiscard]] auto is_set(int cpu) const -> bool
    {
#ifdef _WIN32
        if (cpu < 0)
            return false;
        WORD g = static_cast<WORD>(cpu / 64);
        int bit = cpu % 64;
        return g == group_ && (mask_ & (static_cast<unsigned long long>(1) << bit)) != 0;
#else
        return cpu >= 0 && cpu < CPU_SETSIZE && CPU_ISSET(cpu, &cpuset_);
#endif
    }

    [[nodiscard]] auto has_cpu(int cpu) const -> bool
    {
        return is_set(cpu);
    }

    void clear()
    {
#ifdef _WIN32
        mask_ = 0;
#else
        CPU_ZERO(&cpuset_);
#endif
    }

    [[nodiscard]] auto get_cpus() const -> std::vector<int>
    {
        std::vector<int> cpus;
#ifdef _WIN32
        for (int i = 0; i < 64; ++i)
        {
            if (mask_ & (static_cast<unsigned long long>(1) << i))
            {
                cpus.push_back(static_cast<int>(group_) * 64 + i);
            }
        }
#else
        for (int i = 0; i < CPU_SETSIZE; ++i)
        {
            if (CPU_ISSET(i, &cpuset_))
            {
                cpus.push_back(i);
            }
        }
#endif
        return cpus;
    }

#ifdef _WIN32
    [[nodiscard]] unsigned long long get_mask() const
    {
        return mask_;
    }
    [[nodiscard]] WORD get_group() const
    {
        return group_;
    }
    [[nodiscard]] bool has_any() const
    {
        return mask_ != 0;
    }
#else
    [[nodiscard]] auto native_handle() const -> cpu_set_t const&
    {
        return cpuset_;
    }
#endif

    [[nodiscard]] auto to_string() const -> std::string
    {
        auto cpus = get_cpus();
        std::ostringstream oss;
        oss << "ThreadAffinity({";
        for (size_t i = 0; i < cpus.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << cpus[i];
        }
        oss << "})";
        return oss.str();
    }

  private:
#ifdef _WIN32
    WORD group_;
    unsigned long long mask_;
#else
    cpu_set_t cpuset_;
#endif
};

/**
 * @brief Static utility class for constructing OS-native scheduling parameters.
 *
 * SchedulerParams translates the portable SchedulingPolicy and
 * ThreadPriority types into the platform-specific structures required by
 * the OS scheduling APIs (`sched_param` on Linux, a compatible POD on Windows).
 *
 * ### `create_for_policy`
 * Builds a native scheduling-parameter structure for a given policy/priority
 * pair. The priority is **clamped** to the valid range for the requested policy
 * (queried at runtime on Linux via `sched_get_priority_min` /
 * `sched_get_priority_max`), so callers never need to pre-validate the range
 * themselves. Returns an @ref expected - on failure (e.g. an unrecognised
 * policy value) an `std::error_code` is returned instead.
 *
 * ### `get_priority_range`
 * Returns the width of the valid priority range (max - min) for a policy.
 * Useful for normalising priorities across policies.
 *
 * ### Platform differences
 * - **Linux:** Delegates directly to POSIX `sched_get_priority_min` /
 *   `sched_get_priority_max` and populates a `sched_param`.
 * - **Windows:** Returns a fixed range of 30 (mapping to the -15 ... +15
 *   Windows thread priority levels) and stores the raw priority in a
 *   lightweight `sched_param_win` POD.
 *
 * ### Thread safety
 * All members are static and stateless; concurrent calls from any number of
 * threads are safe.
 *
 * @note This class is not intended to be instantiated.
 *
 * @see SchedulingPolicy, ThreadPriority
 */
class SchedulerParams
{
  public:
#ifdef _WIN32
    // Windows doesn't use sched_param, but we'll define a compatible type
    struct sched_param_win
    {
        int sched_priority;
    };

    static expected<sched_param_win, std::error_code> create_for_policy(SchedulingPolicy policy,
                                                                        ThreadPriority priority)
    {
        sched_param_win param{};
        // On Windows, priority is directly used
        param.sched_priority = priority.value();
        return param;
    }

    static expected<int, std::error_code> get_priority_range(SchedulingPolicy policy)
    {
        // Windows thread priorities range from -15 to +15
        return 30;
    }
#else
    static auto create_for_policy(SchedulingPolicy policy, ThreadPriority priority)
        -> expected<sched_param, std::error_code>
    {
        sched_param param{};

        int const policy_int = static_cast<int>(policy);
        int const min_prio = sched_get_priority_min(policy_int);
        int const max_prio = sched_get_priority_max(policy_int);

        if (min_prio == -1 || max_prio == -1)
        {
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        param.sched_priority = std::clamp(priority.value(), min_prio, max_prio);
        return param;
    }

    static auto get_priority_range(SchedulingPolicy policy) -> expected<int, std::error_code>
    {
        int const policy_int = static_cast<int>(policy);
        int const min_prio = sched_get_priority_min(policy_int);
        int const max_prio = sched_get_priority_max(policy_int);

        if (min_prio == -1 || max_prio == -1)
        {
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        return max_prio - min_prio;
    }
#endif
};

/**
 * @brief String conversion utilities
 */
inline auto to_string(SchedulingPolicy policy) -> std::string
{
    switch (policy)
    {
    case SchedulingPolicy::OTHER:
        return "OTHER";
    case SchedulingPolicy::FIFO:
        return "FIFO";
    case SchedulingPolicy::RR:
        return "RR";
    case SchedulingPolicy::BATCH:
        return "BATCH";
    case SchedulingPolicy::IDLE:
        return "IDLE";
#if defined(SCHED_DEADLINE) && !defined(_WIN32)
    case SchedulingPolicy::DEADLINE:
        return "DEADLINE";
#endif
    default:
        return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// detail:: free functions for thread configuration (priority, policy, affinity)
//
// Overloaded by handle type so that every wrapper class can delegate with a
// single call: detail::apply_priority(handle, priority).
// ---------------------------------------------------------------------------

namespace detail
{

#ifdef _WIN32

inline auto map_priority_to_win32(int prio_val) -> int
{
    if (prio_val <= -10)
        return THREAD_PRIORITY_IDLE;
    if (prio_val <= -5)
        return THREAD_PRIORITY_LOWEST;
    if (prio_val < 0)
        return THREAD_PRIORITY_BELOW_NORMAL;
    if (prio_val == 0)
        return THREAD_PRIORITY_NORMAL;
    if (prio_val <= 5)
        return THREAD_PRIORITY_ABOVE_NORMAL;
    if (prio_val <= 10)
        return THREAD_PRIORITY_HIGHEST;
    return THREAD_PRIORITY_TIME_CRITICAL;
}

inline auto apply_priority(HANDLE handle, ThreadPriority priority) -> expected<void, std::error_code>
{
    if (!handle)
        return unexpected(std::make_error_code(std::errc::no_such_process));
    if (SetThreadPriority(handle, map_priority_to_win32(priority.value())) != 0)
        return {};
    return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

inline auto apply_scheduling_policy(HANDLE handle, SchedulingPolicy /*policy*/, ThreadPriority priority)
    -> expected<void, std::error_code>
{
    return apply_priority(handle, priority);
}

inline auto apply_affinity(HANDLE handle, ThreadAffinity const& affinity) -> expected<void, std::error_code>
{
    if (!handle)
        return unexpected(std::make_error_code(std::errc::no_such_process));
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
    DWORD_PTR mask = static_cast<DWORD_PTR>(affinity.get_mask());
    if (SetThreadAffinityMask(handle, mask) != 0)
        return {};
    return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

inline auto apply_name(HANDLE handle, std::string const& name) -> expected<void, std::error_code>
{
    if (!handle)
        return unexpected(std::make_error_code(std::errc::no_such_process));
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
    if (!hMod)
        return unexpected(std::make_error_code(std::errc::function_not_supported));
    auto set_desc = reinterpret_cast<SetThreadDescriptionFn>(
        reinterpret_cast<void*>(GetProcAddress(hMod, "SetThreadDescription")));
    if (!set_desc)
        return unexpected(std::make_error_code(std::errc::function_not_supported));
    std::wstring wide(name.begin(), name.end());
    if (SUCCEEDED(set_desc(handle, wide.c_str())))
        return {};
    return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

inline auto read_name(HANDLE handle) -> std::optional<std::string>
{
    if (!handle)
        return std::nullopt;
    using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
    if (!hMod)
        return std::nullopt;
    auto get_desc = reinterpret_cast<GetThreadDescriptionFn>(
        reinterpret_cast<void*>(GetProcAddress(hMod, "GetThreadDescription")));
    if (!get_desc)
        return std::nullopt;
    PWSTR thread_name = nullptr;
    if (SUCCEEDED(get_desc(handle, &thread_name)) && thread_name)
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
    return std::nullopt;
}

inline auto read_affinity(HANDLE handle) -> std::optional<ThreadAffinity>
{
    if (!handle)
        return std::nullopt;
    using GetThreadGroupAffinityFn = BOOL(WINAPI*)(HANDLE, PGROUP_AFFINITY);
    HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
    if (!hMod)
        return std::nullopt;
    auto get_group_affinity = reinterpret_cast<GetThreadGroupAffinityFn>(
        reinterpret_cast<void*>(GetProcAddress(hMod, "GetThreadGroupAffinity")));
    if (!get_group_affinity)
        return std::nullopt;
    GROUP_AFFINITY ga{};
    if (get_group_affinity(handle, &ga) != 0)
    {
        ThreadAffinity affinity;
        for (int i = 0; i < 64; ++i)
        {
            if ((ga.Mask & (static_cast<KAFFINITY>(1) << i)) != 0)
                affinity.add_cpu(static_cast<int>(ga.Group) * 64 + i);
        }
        if (affinity.has_any())
            return affinity;
    }
    return std::nullopt;
}

#else // POSIX

// --- shared implementation for pthread_t and pid_t scheduling ---

template <typename SetSchedFn>
inline auto apply_sched_params(SchedulingPolicy policy, ThreadPriority priority, SetSchedFn&& set_sched)
    -> expected<void, std::error_code>
{
    int const policy_int = static_cast<int>(policy);
    auto params_result = SchedulerParams::create_for_policy(policy, priority);
    if (!params_result.has_value())
        return unexpected(params_result.error());
    if (set_sched(policy_int, &params_result.value()) == 0)
        return {};
    return unexpected(std::error_code(errno, std::generic_category()));
}

// --- pthread_t overloads (BaseThreadWrapper, ThreadControlBlock, PThreadWrapper) ---

inline auto apply_scheduling_policy(pthread_t handle, SchedulingPolicy policy, ThreadPriority priority)
    -> expected<void, std::error_code>
{
    return apply_sched_params(policy, priority,
                              [handle](int p, sched_param* sp) { return pthread_setschedparam(handle, p, sp); });
}

inline auto apply_priority(pthread_t handle, ThreadPriority priority) -> expected<void, std::error_code>
{
    return apply_scheduling_policy(handle, SchedulingPolicy::OTHER, priority);
}

inline auto apply_affinity(pthread_t handle, ThreadAffinity const& affinity) -> expected<void, std::error_code>
{
    if (pthread_setaffinity_np(handle, sizeof(cpu_set_t), &affinity.native_handle()) == 0)
        return {};
    return unexpected(std::error_code(errno, std::generic_category()));
}

inline auto apply_name(pthread_t handle, std::string const& name) -> expected<void, std::error_code>
{
    if (name.length() > 15)
        return unexpected(std::make_error_code(std::errc::invalid_argument));
    if (pthread_setname_np(handle, name.c_str()) == 0)
        return {};
    return unexpected(std::error_code(errno, std::generic_category()));
}

inline auto read_name(pthread_t handle) -> std::optional<std::string>
{
    char name[16];
    if (pthread_getname_np(handle, name, sizeof(name)) == 0)
        return std::string(name);
    return std::nullopt;
}

inline auto read_affinity(pthread_t handle) -> std::optional<ThreadAffinity>
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (pthread_getaffinity_np(handle, sizeof(cpu_set_t), &cpuset) == 0)
    {
        std::vector<int> cpus;
        for (int i = 0; i < CPU_SETSIZE; ++i)
        {
            if (CPU_ISSET(i, &cpuset))
                cpus.push_back(i);
        }
        return ThreadAffinity(cpus);
    }
    return std::nullopt;
}

// --- pid_t / TID overloads (ThreadByNameView) ---

inline auto apply_scheduling_policy(pid_t tid, SchedulingPolicy policy, ThreadPriority priority)
    -> expected<void, std::error_code>
{
    return apply_sched_params(policy, priority,
                              [tid](int p, sched_param* sp) { return sched_setscheduler(tid, p, sp); });
}

inline auto apply_priority(pid_t tid, ThreadPriority priority) -> expected<void, std::error_code>
{
    return apply_scheduling_policy(tid, SchedulingPolicy::OTHER, priority);
}

inline auto apply_affinity(pid_t tid, ThreadAffinity const& affinity) -> expected<void, std::error_code>
{
    if (sched_setaffinity(tid, sizeof(cpu_set_t), &affinity.native_handle()) == 0)
        return {};
    return unexpected(std::error_code(errno, std::generic_category()));
}

#endif

} // namespace detail

} // namespace threadschedule
