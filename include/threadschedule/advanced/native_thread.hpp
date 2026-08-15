#pragma once

/**
 * @file advanced/native_thread.hpp
 * @brief Native thread identity and handle escape hatches.
 */

#include "../jthread.hpp"
#include "../thread.hpp"
#include "../thread_id.hpp"

#ifndef _WIN32
#  include <sys/types.h>
#endif

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

#ifdef _WIN32
using native_thread_id = unsigned long;
#else
using native_thread_id = pid_t;
#endif

[[nodiscard]] constexpr auto
native_id(thread_id id) noexcept -> native_thread_id
{
  return static_cast<native_thread_id>(::threadschedule::detail::thread_id_access::value(id));
}

} // namespace threadschedule::advanced
