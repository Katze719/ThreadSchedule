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
/**
 * @brief Apply full portable configuration to the calling thread.
 */
inline auto
configure(thread_config const& config) -> result<void>
{
  auto current = detail::thread_info();
  return detail::portable_thread_control::configure(current, config);
}

/** @brief Set portable priority preset for the calling thread. */
inline auto
set_priority(priority_level level) -> result<void>
{
  auto current = detail::thread_info();
  return detail::portable_thread_control::set_priority(current, level);
}

/** @brief Set explicit nice value for the calling thread. */
inline auto
set_nice(nice_value value) -> result<void>
{
  auto current = detail::thread_info();
  return detail::portable_thread_control::set_nice(current, value);
}

/** @brief Query current portable priority preset of the calling thread. */
[[nodiscard]] inline auto
get_priority() -> result<priority_level>
{
  auto const id = detail::current_native_thread_id();
  return detail::portable_thread_control::get_priority(detail::read_effective_nice(id, id));
}

/** @brief Query current nice value of the calling thread. */
[[nodiscard]] inline auto
get_nice() -> result<nice_value>
{
  auto const id = detail::current_native_thread_id();
  return detail::portable_thread_control::get_nice(detail::read_effective_nice(id, id));
}

/** @brief Set the calling thread name. */
inline auto
set_name(std::string const& name) -> result<void>
{
  return detail::thread_info().set_name(name);
}

/** @brief Query the calling thread name. */
[[nodiscard]] inline auto
get_name() -> result<std::string>
{
  return detail::thread_info().get_name();
}

/** @brief Set CPU affinity for the calling thread. */
inline auto
set_affinity(thread_affinity const& affinity) -> result<void>
{
  auto current = detail::thread_info();
  return detail::portable_thread_control::set_affinity(current, affinity);
}

/** @brief Query CPU affinity of the calling thread. */
[[nodiscard]] inline auto
get_affinity() -> result<thread_affinity>
{
  return detail::portable_thread_control::get_affinity(detail::thread_info().get_affinity());
}
} // namespace this_thread

} // namespace threadschedule
