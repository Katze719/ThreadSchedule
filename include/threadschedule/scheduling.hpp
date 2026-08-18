#pragma once

/**
 * @file scheduling.hpp
 * @brief Portable scheduling intents and priority configuration.
 */

#include <cstdint>

#include "nice_value.hpp"
#include "realtime_priority.hpp"

namespace threadschedule
{

namespace detail
{
struct scheduling_config_access;
}

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

class scheduling_config
{
public:
  [[nodiscard]] constexpr auto
  intent() const noexcept -> scheduling_intent
  {
    return intent_;
  }

  [[nodiscard]] constexpr auto
  priority_value() const noexcept -> int
  {
    return priority_;
  }

private:
  friend struct detail::scheduling_config_access;
  constexpr scheduling_config(scheduling_intent intent, int priority) noexcept : intent_(intent), priority_(priority) {}

  scheduling_intent intent_;
  int priority_;
};

namespace detail
{
struct scheduling_config_access
{
  [[nodiscard]] static constexpr auto
  make(scheduling_intent intent, int priority = 0) noexcept -> scheduling_config
  {
    return scheduling_config(intent, priority);
  }
};
} // namespace detail

namespace schedule
{
[[nodiscard]] constexpr auto
background() noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::background);
}

[[nodiscard]] constexpr auto
normal() noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::normal);
}

[[nodiscard]] constexpr auto
interactive() noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::interactive);
}

[[nodiscard]] constexpr auto
low_latency() noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::low_latency);
}

[[nodiscard]] constexpr auto
realtime_fifo(realtime_priority priority) noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::realtime_fifo, priority.value());
}

[[nodiscard]] constexpr auto
realtime_rr(realtime_priority priority) noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::realtime_round_robin, priority.value());
}

[[nodiscard]] constexpr auto
nice(nice_value value) noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::nice, value.value());
}

[[nodiscard]] constexpr auto
priority(priority_level level) noexcept -> scheduling_config
{
  return nice(nice_value{ static_cast<int>(level) });
}
} // namespace schedule

} // namespace threadschedule
