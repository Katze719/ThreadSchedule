#pragma once

/**
 * @file profiles.hpp
 * @brief High-level thread configuration profiles and helpers.
 *
 * Provides simple presets (e.g. realtime, low_latency, throughput, background)
 * and utility functions to apply them to single threads, thread pools, or
 * registry-managed threads. Profiles abstract low-level flags like policy,
 * priority, and optional CPU affinity into a single intent.
 */

#include "concepts.hpp"
#include "scheduler_policy.hpp"
#include "thread_pool.hpp"
#include "thread_registry.hpp"
#include <optional>
#include <string>

namespace threadschedule
{

template <>
struct is_thread_like<ThreadWrapper> : std::true_type
{
};

template <>
struct is_thread_like<ThreadWrapperView> : std::true_type
{
};

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
template <>
struct is_thread_like<JThreadWrapper> : std::true_type
{
};

template <>
struct is_thread_like<JThreadWrapperView> : std::true_type
{
};
#endif

/**
 * @brief Declarative profile bundling scheduling intent for a thread.
 *
 * Value type (copyable). Combines a human-readable name, a scheduling
 * policy, a priority level, and an optional CPU affinity mask into a
 * single object that can be passed to the apply_profile() overloads.
 *
 * @see profiles::realtime, profiles::low_latency, profiles::throughput,
 *      profiles::background
 * @see apply_profile()
 */
struct ThreadProfile
{
    std::string name;
    SchedulingPolicy policy;
    ThreadPriority priority;
    std::optional<ThreadAffinity> affinity; // optional pinning
};

namespace profiles
{
/**
 * @brief Highest priority profile. Uses FIFO on Linux (if permitted),
 *        falls back to OTHER on Windows.
 */
inline auto realtime() -> ThreadProfile
{
    return ThreadProfile{"realtime",
#ifdef _WIN32
                         SchedulingPolicy::OTHER,
#else
                         SchedulingPolicy::FIFO,
#endif
                         ThreadPriority::highest(), std::nullopt};
}

/**
 * @brief Low-latency interactive profile using RR scheduling.
 */
inline auto low_latency() -> ThreadProfile
{
    return ThreadProfile{"low_latency", SchedulingPolicy::RR, ThreadPriority{5}, std::nullopt};
}

/**
 * @brief Throughput-oriented profile favoring batch scheduling.
 */
inline auto throughput() -> ThreadProfile
{
    return ThreadProfile{"throughput", SchedulingPolicy::BATCH, ThreadPriority::normal(), std::nullopt};
}

/**
 * @brief Background profile for very low priority work.
 */
inline auto background() -> ThreadProfile
{
    return ThreadProfile{"background", SchedulingPolicy::IDLE, ThreadPriority::lowest(), std::nullopt};
}
} // namespace profiles

/**
 * @brief Apply a profile to a thread wrapper or view.
 *
 * SFINAE-constrained: only participates in overload resolution when
 * @c is_thread_like_v<ThreadLike> is true (ThreadWrapper,
 * JThreadWrapper, PThreadWrapper, and their views).
 *
 * @tparam ThreadLike A type satisfying the is_thread_like trait.
 * @param t   Thread wrapper or view to configure.
 * @param p   Profile to apply.
 * @return    Empty expected on success, or @c operation_not_permitted.
 */
template <typename ThreadLike, std::enable_if_t<is_thread_like_v<ThreadLike>, int> = 0>
inline auto apply_profile(ThreadLike& t, ThreadProfile const& p) -> expected<void, std::error_code>
{
    bool ok = true;
    if (!t.set_scheduling_policy(p.policy, p.priority).has_value())
        ok = false;
    if (p.affinity.has_value())
    {
        if (!t.set_affinity(*p.affinity).has_value())
            ok = false;
    }
    if (ok)
        return {};
    return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

/**
 * @brief Apply a profile to a ThreadControlBlock directly.
 *
 * @param t   Control block whose underlying thread will be reconfigured.
 * @param p   Profile to apply.
 * @return    Empty expected on success, or @c operation_not_permitted.
 */
inline auto apply_profile(ThreadControlBlock& t, ThreadProfile const& p) -> expected<void, std::error_code>
{
    bool ok = true;
    if (!t.set_scheduling_policy(p.policy, p.priority).has_value())
        ok = false;
    if (p.affinity.has_value())
    {
        if (!t.set_affinity(*p.affinity).has_value())
            ok = false;
    }
    if (ok)
        return {};
    return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

/**
 * @brief Apply a profile to a registered thread via its info record.
 *
 * Dereferences @c t.control and delegates to the ThreadControlBlock
 * overload.
 *
 * @warning Undefined behaviour if @c t.control is @c nullptr.
 *
 * @param t   Registered thread info whose control pointer is dereferenced.
 * @param p   Profile to apply.
 * @return    Empty expected on success, or @c operation_not_permitted.
 */
inline auto apply_profile(RegisteredThreadInfo& t, ThreadProfile const& p) -> expected<void, std::error_code>
{
    return apply_profile(*t.control, p);
}

/**
 * @brief Apply a profile to every worker in a ThreadPool.
 *
 * Uses @c "pool" as the thread name prefix passed to
 * ThreadPool::configure_threads().
 *
 * @param pool  Thread pool to configure.
 * @param p     Profile to apply.
 * @return      Empty expected on success, or @c operation_not_permitted.
 */
inline auto apply_profile(ThreadPool& pool, ThreadProfile const& p) -> expected<void, std::error_code>
{
    bool ok = true;
    // Name prefix left to caller via configure_threads; here just policy/priority
    if (!pool.configure_threads("pool", p.policy, p.priority))
        ok = false;
    if (p.affinity.has_value())
    {
        if (!pool.set_affinity(*p.affinity))
            ok = false;
    }
    if (ok)
        return {};
    return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

/**
 * @brief Apply a profile to every worker in a HighPerformancePool.
 *
 * Uses @c "hp" as the thread name prefix passed to
 * HighPerformancePool::configure_threads().
 *
 * @param pool  High-performance pool to configure.
 * @param p     Profile to apply.
 * @return      Empty expected on success, or @c operation_not_permitted.
 */
inline auto apply_profile(HighPerformancePool& pool, ThreadProfile const& p) -> expected<void, std::error_code>
{
    bool ok = true;
    if (!pool.configure_threads("hp", p.policy, p.priority).has_value())
        ok = false;
    if (p.affinity.has_value())
    {
        if (!pool.set_affinity(*p.affinity).has_value())
            ok = false;
    }
    if (ok)
        return {};
    return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

/**
 * @brief Apply a profile to a registry-managed thread identified by TID.
 *
 * @param reg  Thread registry that owns the thread.
 * @param tid  Thread identifier within the registry.
 * @param p    Profile to apply.
 * @return     Empty expected on success, or @c operation_not_permitted.
 */
inline auto apply_profile(ThreadRegistry& reg, Tid tid, ThreadProfile const& p) -> expected<void, std::error_code>
{
    bool ok = true;
    if (!reg.set_scheduling_policy(tid, p.policy, p.priority).has_value())
        ok = false;
    if (p.affinity.has_value())
    {
        if (!reg.set_affinity(tid, *p.affinity).has_value())
            ok = false;
    }
    if (ok)
        return {};
    return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

} // namespace threadschedule
