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

#include "scheduler_policy.hpp"
#include "thread_pool.hpp"
#include "thread_registry.hpp"
#include "thread_wrapper.hpp"
#include <optional>
#include <string>

namespace threadschedule
{

/**
 * @brief Declarative profile describing desired scheduling.
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
 * @brief Apply a profile to a single thread wrapper or view.
 */
template <typename ThreadLike>
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
 * @brief Apply a profile to all workers of a simple ThreadPool.
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
 * @brief Apply a profile to all workers of a HighPerformancePool.
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
 * @brief Apply a profile to a registry-controlled thread by TID.
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
