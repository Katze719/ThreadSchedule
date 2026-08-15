#pragma once

#include "detail/thread/control.hpp"
#include "detail/thread_backend.hpp"
#include "thread_config.hpp"

#include <string>

namespace threadschedule
{
/** @brief Portable configuration operations for the calling thread. */
namespace this_thread
{
inline auto
configure(thread_config const& config) -> result<void>
{
  auto current = detail::thread_info();
  return detail::portable_thread_control::configure(current, config);
}

inline auto
set_priority(priority_level level) -> result<void>
{
  auto current = detail::thread_info();
  return detail::portable_thread_control::set_priority(current, level);
}

inline auto
set_nice(nice_value value) -> result<void>
{
  auto current = detail::thread_info();
  return detail::portable_thread_control::set_nice(current, value);
}

[[nodiscard]] inline auto
get_priority() -> result<priority_level>
{
  return detail::portable_thread_control::get_priority(detail::read_nice_value(detail::current_native_thread_id()));
}

[[nodiscard]] inline auto
get_nice() -> result<nice_value>
{
  return detail::portable_thread_control::get_nice(detail::read_nice_value(detail::current_native_thread_id()));
}

inline auto
set_name(std::string const& name) -> result<void>
{
  return detail::thread_info().set_name(name);
}

[[nodiscard]] inline auto
get_name() -> result<std::string>
{
  return detail::thread_info().get_name();
}

inline auto
set_affinity(thread_affinity const& affinity) -> result<void>
{
  auto current = detail::thread_info();
  return detail::portable_thread_control::set_affinity(current, affinity);
}

[[nodiscard]] inline auto
get_affinity() -> result<thread_affinity>
{
  return detail::portable_thread_control::get_affinity(detail::thread_info().get_affinity());
}
} // namespace this_thread

} // namespace threadschedule
