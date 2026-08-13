#pragma once

/**
 * @file scheduling.hpp
 * @brief Portable scheduling intents and priority configuration.
 */

#include <cstdint>

namespace threadschedule
{

enum class scheduling_intent : std::uint8_t
{
  background,
  normal,
  interactive,
  low_latency,
  realtime_fifo,
  realtime_round_robin,
  nice
};

/** @brief Portable non-realtime priority levels. */
enum class priority_level : std::int8_t
{
  lowest = 19,
  low = 5,
  normal = 0,
  high = -5,
  highest = -20
};

struct scheduling_config
{
  scheduling_intent intent{ scheduling_intent::normal };
  int priority{ 0 };
};

namespace schedule
{
[[nodiscard]] constexpr auto
background() noexcept -> scheduling_config
{
  return { scheduling_intent::background, 0 };
}

[[nodiscard]] constexpr auto
normal() noexcept -> scheduling_config
{
  return { scheduling_intent::normal, 0 };
}

[[nodiscard]] constexpr auto
interactive() noexcept -> scheduling_config
{
  return { scheduling_intent::interactive, 0 };
}

[[nodiscard]] constexpr auto
low_latency() noexcept -> scheduling_config
{
  return { scheduling_intent::low_latency, 0 };
}

[[nodiscard]] constexpr auto
realtime_fifo(int priority = 80) noexcept -> scheduling_config
{
  return { scheduling_intent::realtime_fifo, priority };
}

[[nodiscard]] constexpr auto
realtime_rr(int priority = 80) noexcept -> scheduling_config
{
  return { scheduling_intent::realtime_round_robin, priority };
}

[[nodiscard]] constexpr auto
nice(int value) noexcept -> scheduling_config
{
  return { scheduling_intent::nice, value };
}

[[nodiscard]] constexpr auto
priority(priority_level level) noexcept -> scheduling_config
{
  return nice(static_cast<int>(level));
}
} // namespace schedule

} // namespace threadschedule
