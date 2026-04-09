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
#include <vector>

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
    std::optional<ThreadAffinity> affinity;
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

namespace detail
{

/**
 * @brief Apply policy + optional affinity to any type exposing
 *        set_scheduling_policy() and set_affinity().
 */
template <typename T>
inline auto apply_profile_to(T& t, ThreadProfile const& p) -> expected<void, std::error_code>
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
 * @brief Apply configure_threads + optional affinity to any pool type.
 */
template <typename PoolType>
inline auto apply_profile_to_pool(PoolType& pool, std::string const& name_prefix, ThreadProfile const& p)
    -> expected<void, std::error_code>
{
    bool ok = true;
    if (!pool.configure_threads(name_prefix, p.policy, p.priority).has_value())
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

} // namespace detail

/**
 * @brief Apply a profile to a thread wrapper or view.
 *
 * @tparam ThreadLike A type satisfying the is_thread_like trait.
 * @param t   Thread wrapper or view to configure.
 * @param p   Profile to apply.
 * @return    Empty expected on success, or @c operation_not_permitted.
 */
template <typename ThreadLike, std::enable_if_t<is_thread_like_v<ThreadLike>, int> = 0>
inline auto apply_profile(ThreadLike& t, ThreadProfile const& p) -> expected<void, std::error_code>
{
    return detail::apply_profile_to(t, p);
}

/**
 * @brief Apply a profile to a ThreadControlBlock directly.
 */
inline auto apply_profile(ThreadControlBlock& t, ThreadProfile const& p) -> expected<void, std::error_code>
{
    return detail::apply_profile_to(t, p);
}

/**
 * @brief Apply a profile to a registered thread via its info record.
 */
inline auto apply_profile(RegisteredThreadInfo& t, ThreadProfile const& p) -> expected<void, std::error_code>
{
    return apply_profile(*t.control, p);
}

/**
 * @brief Apply a profile to every worker in a ThreadPool.
 */
inline auto apply_profile(ThreadPool& pool, ThreadProfile const& p) -> expected<void, std::error_code>
{
    return detail::apply_profile_to_pool(pool, "pool", p);
}

/**
 * @brief Apply a profile to every worker in a FastThreadPool.
 */
inline auto apply_profile(FastThreadPool& pool, ThreadProfile const& p) -> expected<void, std::error_code>
{
    return detail::apply_profile_to_pool(pool, "fast", p);
}

/**
 * @brief Apply a profile to every worker in a HighPerformancePool.
 */
inline auto apply_profile(HighPerformancePool& pool, ThreadProfile const& p) -> expected<void, std::error_code>
{
    return detail::apply_profile_to_pool(pool, "hp", p);
}

/**
 * @brief Apply a profile to a registry-managed thread identified by TID.
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

/**
 * @brief Apply a profile and return per-step error codes.
 *
 * Unlike @ref apply_profile (which aggregates into a single
 * @c operation_not_permitted), this function returns a vector with one
 * entry per configuration step. Successful steps have a default
 * (zero) error code; failed steps carry the specific OS error.
 *
 * The steps are, in order:
 *  0 - set_scheduling_policy
 *  1 - set_affinity (only present when @c p.affinity has a value)
 *
 * @tparam ThreadLike A type satisfying the is_thread_like trait.
 * @return Vector of error codes, one per step attempted.
 */
template <typename ThreadLike, std::enable_if_t<is_thread_like_v<ThreadLike>, int> = 0>
inline auto apply_profile_detailed(ThreadLike& t, ThreadProfile const& p) -> std::vector<std::error_code>
{
    std::vector<std::error_code> results;
    auto policy_result = t.set_scheduling_policy(p.policy, p.priority);
    results.push_back(policy_result.has_value() ? std::error_code{} : policy_result.error());
    if (p.affinity.has_value())
    {
        auto aff_result = t.set_affinity(*p.affinity);
        results.push_back(aff_result.has_value() ? std::error_code{} : aff_result.error());
    }
    return results;
}

/**
 * @brief Apply a profile to a ThreadControlBlock with per-step errors.
 * @see apply_profile_detailed(ThreadLike&, ThreadProfile const&)
 */
inline auto apply_profile_detailed(ThreadControlBlock& t, ThreadProfile const& p) -> std::vector<std::error_code>
{
    std::vector<std::error_code> results;
    auto policy_result = t.set_scheduling_policy(p.policy, p.priority);
    results.push_back(policy_result.has_value() ? std::error_code{} : policy_result.error());
    if (p.affinity.has_value())
    {
        auto aff_result = t.set_affinity(*p.affinity);
        results.push_back(aff_result.has_value() ? std::error_code{} : aff_result.error());
    }
    return results;
}

} // namespace threadschedule
