#pragma once

/**
 * @file advanced/native_thread.hpp
 * @brief Native thread scheduling controls and platform escape hatches.
 */

#include "../core.hpp"

namespace threadschedule::detail
{

struct native_thread_access
{
  [[nodiscard]] static auto
  get(thread& value) -> std::thread::native_handle_type
  {
    return value.impl_.native_handle();
  }

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
  [[nodiscard]] static auto
  get(jthread& value) -> std::jthread::native_handle_type
  {
    return value.impl_.native_handle();
  }
#endif
};

} // namespace threadschedule::detail

namespace threadschedule::advanced
{

[[nodiscard]] inline auto
native_handle(thread& value) -> std::thread::native_handle_type
{
  return ::threadschedule::detail::native_thread_access::get(value);
}

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
[[nodiscard]] inline auto
native_handle(jthread& value) -> std::jthread::native_handle_type
{
  return ::threadschedule::detail::native_thread_access::get(value);
}
#endif

using native_thread_id = ::threadschedule::detail::native_thread_id;
using native_thread_priority = ::threadschedule::detail::native_thread_priority;
using native_thread_affinity = ::threadschedule::detail::native_thread_affinity;
using native_scheduling_policy = ::threadschedule::detail::native_scheduling_policy;
using native_scheduling_config = ::threadschedule::detail::native_scheduling_config;
using native_thread_config = ::threadschedule::detail::native_thread_config;
using scheduler_parameters = ::threadschedule::detail::scheduler_parameters;
namespace native_schedule = ::threadschedule::detail::native_schedule;

[[nodiscard]] constexpr auto
native_id(thread_id id) noexcept -> native_thread_id
{
  return static_cast<native_thread_id>(::threadschedule::detail::thread_id_access::value(id));
}

} // namespace threadschedule::advanced
