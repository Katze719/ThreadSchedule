#pragma once

#include "../expected.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <system_error>
#include <type_traits>

namespace threadschedule::detail
{

template <typename ToDuration, typename Rep, typename Period>
[[nodiscard]] inline auto
checked_duration_cast(std::chrono::duration<Rep, Period> value) -> expected<ToDuration, std::error_code>
{
  using source_duration = std::chrono::duration<Rep, Period>;
  using target_rep = typename ToDuration::rep;

  if constexpr (std::is_same_v<source_duration, ToDuration>)
    return value;

  using floating_duration = std::chrono::duration<long double, typename ToDuration::period>;

  auto const count = floating_duration(value).count();
  if (!std::isfinite(count))
    return unexpected(std::make_error_code(std::errc::invalid_argument));

  if constexpr (std::is_integral_v<target_rep>)
    {
      auto const minimum = static_cast<long double>((std::numeric_limits<target_rep>::lowest)());
      auto const upper_bound = static_cast<long double>((std::numeric_limits<target_rep>::max)()) + 1.0L;
      if (count < minimum || count >= upper_bound)
        return unexpected(std::make_error_code(std::errc::value_too_large));
    }
  else
    {
      auto const minimum = static_cast<long double>((std::numeric_limits<target_rep>::lowest)());
      auto const maximum = static_cast<long double>((std::numeric_limits<target_rep>::max)());
      if (count < minimum || count > maximum)
        return unexpected(std::make_error_code(std::errc::value_too_large));
    }

  return ToDuration(static_cast<target_rep>(count));
}

template <typename Clock, typename Duration>
[[nodiscard]] inline auto
checked_deadline_after(std::chrono::time_point<Clock, Duration> now, Duration delay)
    -> expected<std::chrono::time_point<Clock, Duration>, std::error_code>
{
  if (delay <= Duration::zero())
    return now;

  auto const current = now.time_since_epoch().count();
  auto const delta = delay.count();
  auto const maximum = (std::numeric_limits<typename Duration::rep>::max)();
  if (current > maximum - delta)
    return unexpected(std::make_error_code(std::errc::value_too_large));
  return std::chrono::time_point<Clock, Duration>(Duration(current + delta));
}

} // namespace threadschedule::detail
