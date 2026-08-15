#pragma once

#include "../result.hpp"
#include "../scheduling.hpp"
#include "../thread_affinity.hpp"
#include "../thread_config.hpp"
#include "polling_pool.hpp"
#include "raw_thread_pool.hpp"
#include "work_stealing_pool.hpp"

#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace threadschedule::advanced
{

struct thread_profile
{
  std::string name;
  scheduling_config scheduling;
  std::optional<thread_affinity> affinity;
};

namespace profiles
{
[[nodiscard]] inline auto
realtime() -> thread_profile
{
  return { "realtime", schedule::realtime_fifo(realtime_priority{ 99 }), std::nullopt };
}

[[nodiscard]] inline auto
low_latency() -> thread_profile
{
  return { "low_latency", schedule::low_latency(), std::nullopt };
}

[[nodiscard]] inline auto
throughput() -> thread_profile
{
  return { "throughput", schedule::normal(), std::nullopt };
}

[[nodiscard]] inline auto
background() -> thread_profile
{
  return { "background", schedule::background(), std::nullopt };
}
} // namespace profiles

namespace profile_detail
{
[[nodiscard]] inline auto
make_config(thread_profile const& profile, std::string name) -> thread_config
{
  thread_config config;
  config.set_name(std::move(name)).set_scheduling(profile.scheduling);
  if (profile.affinity)
    config.set_affinity(*profile.affinity);
  return config;
}

template <typename ThreadLike>
auto
apply_to_thread(ThreadLike& value, thread_profile const& profile) -> result<void>
{
  return value.configure(make_config(profile, profile.name));
}

template <typename Pool>
auto
apply_to_pool(Pool& pool, std::string name, thread_profile const& profile) -> result<void>
{
  return pool.configure_workers(make_config(profile, std::move(name)));
}
} // namespace profile_detail

template <typename ThreadLike>
auto
apply_profile(ThreadLike& value, thread_profile const& profile) -> result<void>
{
  return profile_detail::apply_to_thread(value, profile);
}

inline auto
apply_profile(raw_thread_pool& pool, thread_profile const& profile) -> result<void>
{
  return profile_detail::apply_to_pool(pool, "pool", profile);
}

inline auto
apply_profile(polling_pool& pool, thread_profile const& profile) -> result<void>
{
  return profile_detail::apply_to_pool(pool, "polling", profile);
}

inline auto
apply_profile(work_stealing_pool& pool, thread_profile const& profile) -> result<void>
{
  return profile_detail::apply_to_pool(pool, "work_stealing", profile);
}

template <typename ThreadLike>
auto
apply_profile_detailed(ThreadLike& value, thread_profile const& profile) -> std::vector<std::error_code>
{
  std::vector<std::error_code> errors;
  thread_config scheduling;
  scheduling.set_name(profile.name).set_scheduling(profile.scheduling);
  auto configured = value.configure(scheduling);
  errors.push_back(configured ? std::error_code{} : configured.error());
  if (profile.affinity)
    {
      auto affinity = value.set_affinity(*profile.affinity);
      errors.push_back(affinity ? std::error_code{} : affinity.error());
    }
  return errors;
}

} // namespace threadschedule::advanced
