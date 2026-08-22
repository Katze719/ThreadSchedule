#pragma once

#include "../time.hpp"

#include <chrono>

namespace threadschedule::detail
{

[[nodiscard]] inline auto
shutdown_deadline_after(std::chrono::milliseconds timeout) -> std::chrono::steady_clock::time_point
{
  using clock = std::chrono::steady_clock;
  using time_point = clock::time_point;

  auto const now = clock::now();
  if (timeout <= std::chrono::milliseconds::zero())
    return now;

  auto const delay = checked_duration_cast<clock::duration>(timeout);
  if (!delay)
    return time_point::max();

  auto const deadline = checked_deadline_after(now, delay.value());
  return deadline ? deadline.value() : time_point::max();
}

} // namespace threadschedule::detail
