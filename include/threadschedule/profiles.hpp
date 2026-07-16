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
struct is_thread_like<detail::thread_backend> : std::true_type
{
};

template <>
struct is_thread_like<detail::thread_view_backend> : std::true_type
{
};

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
struct thread_profile
{
  std::string name;
  native_scheduling_policy policy;
  native_thread_priority priority;
  std::optional<native_thread_affinity> affinity;
};

namespace profiles
{
/**
 * @brief Highest priority profile. Uses FIFO on Linux (if permitted),
 *        falls back to OTHER on Windows.
 */
inline auto
realtime() -> thread_profile
{
  return thread_profile{ "realtime",
#ifdef _WIN32
                         native_scheduling_policy::other,
#else
                         native_scheduling_policy::fifo,
#endif
                         native_thread_priority::realtime_highest(),
                         std::nullopt };
}

/**
 * @brief Low-latency interactive profile that avoids real-time privileges.
 */
inline auto
low_latency() -> thread_profile
{
  auto const config = detail::native_schedule::low_latency();
  auto const scheduling = detail::resolve_scheduling_config(config);
  return thread_profile{ "low_latency", scheduling.policy, scheduling.priority,
                         std::nullopt };
}

/**
 * @brief Throughput-oriented profile favoring batch scheduling.
 */
inline auto
throughput() -> thread_profile
{
  return thread_profile{ "throughput", native_scheduling_policy::batch,
                         native_thread_priority::normal(), std::nullopt };
}

/**
 * @brief Background profile for very low priority work.
 */
inline auto
background() -> thread_profile
{
  return thread_profile{ "background", native_scheduling_policy::idle,
                         native_thread_priority::lowest(), std::nullopt };
}
} // namespace profiles

namespace detail
{

/**
 * @brief Apply policy + optional affinity to any type exposing
 *        set_scheduling_policy() and set_affinity().
 */
template <typename T>
inline auto
apply_profile_to(T& t, thread_profile const& p)
    -> expected<void, std::error_code>
{
  auto scheduled = t.set_scheduling_policy(p.policy, p.priority);
  if (!scheduled)
    return unexpected(scheduled.error());
  if (p.affinity.has_value())
    return t.set_affinity(*p.affinity);
  return {};
}

/**
 * @brief Apply configure_threads + optional affinity to any pool type.
 */
template <typename PoolType>
inline auto
apply_profile_to_pool(PoolType& pool, std::string const& name_prefix,
                      thread_profile const& p)
    -> expected<void, std::error_code>
{
  auto configured = pool.configure_threads(name_prefix, p.policy, p.priority);
  if (!configured)
    return unexpected(configured.error());
  if (p.affinity.has_value())
    return pool.set_affinity(*p.affinity);
  return {};
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
template <typename ThreadLike,
          std::enable_if_t<is_thread_like_v<ThreadLike>, int> = 0>
inline auto
apply_profile(ThreadLike& t, thread_profile const& p)
    -> expected<void, std::error_code>
{
  return detail::apply_profile_to(t, p);
}

/**
 * @brief Apply a profile to a thread_control_block directly.
 */
inline auto
apply_profile(thread_control_block& t, thread_profile const& p)
    -> expected<void, std::error_code>
{
  return detail::apply_profile_to(t, p);
}

/**
 * @brief Apply a profile to a registered thread via its info record.
 */
inline auto
apply_profile(registered_thread_info_backend& t, thread_profile const& p)
    -> expected<void, std::error_code>
{
  if (!t.control)
    return unexpected(std::make_error_code(std::errc::no_such_process));
  return apply_profile(*t.control, p);
}

/**
 * @brief Apply a profile to every worker in a thread_pool_backend.
 */
inline auto
apply_profile(thread_pool_backend& pool, thread_profile const& p)
    -> expected<void, std::error_code>
{
  return detail::apply_profile_to_pool(pool, "pool", p);
}

/**
 * @brief Apply a profile to every worker in a polling_pool_backend.
 */
inline auto
apply_profile(polling_pool_backend& pool, thread_profile const& p)
    -> expected<void, std::error_code>
{
  return detail::apply_profile_to_pool(pool, "fast", p);
}

/**
 * @brief Apply a profile to every worker in a work_stealing_pool_backend.
 */
inline auto
apply_profile(work_stealing_pool_backend& pool, thread_profile const& p)
    -> expected<void, std::error_code>
{
  return detail::apply_profile_to_pool(pool, "hp", p);
}

/**
 * @brief Apply a profile to a registry-managed thread identified by TID.
 */
inline auto
apply_profile(thread_registry_backend& reg, native_thread_id tid,
              thread_profile const& p) -> expected<void, std::error_code>
{
  auto scheduled = reg.set_scheduling_policy(tid, p.policy, p.priority);
  if (!scheduled)
    return unexpected(scheduled.error());
  if (p.affinity.has_value())
    return reg.set_affinity(tid, *p.affinity);
  return {};
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
template <typename ThreadLike,
          std::enable_if_t<is_thread_like_v<ThreadLike>, int> = 0>
inline auto
apply_profile_detailed(ThreadLike& t, thread_profile const& p)
    -> std::vector<std::error_code>
{
  std::vector<std::error_code> results;
  auto policy_result = t.set_scheduling_policy(p.policy, p.priority);
  results.push_back(policy_result.has_value() ? std::error_code{}
                                              : policy_result.error());
  if (p.affinity.has_value())
    {
      auto aff_result = t.set_affinity(*p.affinity);
      results.push_back(aff_result.has_value() ? std::error_code{}
                                               : aff_result.error());
    }
  return results;
}

/**
 * @brief Apply a profile to a thread_control_block with per-step errors.
 * @see apply_profile_detailed(ThreadLike&, thread_profile const&)
 */
inline auto
apply_profile_detailed(thread_control_block& t, thread_profile const& p)
    -> std::vector<std::error_code>
{
  std::vector<std::error_code> results;
  auto policy_result = t.set_scheduling_policy(p.policy, p.priority);
  results.push_back(policy_result.has_value() ? std::error_code{}
                                              : policy_result.error());
  if (p.affinity.has_value())
    {
      auto aff_result = t.set_affinity(*p.affinity);
      results.push_back(aff_result.has_value() ? std::error_code{}
                                               : aff_result.error());
    }
  return results;
}

} // namespace threadschedule
