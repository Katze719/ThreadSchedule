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

/**
 * @brief Portable scheduling intents that map to platform-specific behavior.
 */
enum class scheduling_intent : std::uint8_t
{
  /** @brief Prefer throughput and lower CPU contention over responsiveness. */
  background,
  /** @brief Default scheduler behavior with balanced responsiveness. */
  normal,
  /** @brief Favor responsiveness for user-facing work. */
  interactive,
  /** @brief Favor low-latency execution where possible. */
  low_latency,
  /** @brief Request POSIX FIFO realtime scheduling. */
  realtime_fifo,
  /** @brief Request POSIX round-robin realtime scheduling. */
  realtime_round_robin,
  /** @brief Apply an explicit nice-level priority value. */
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

/**
 * @brief Immutable scheduling request passed to thread creation/configuration APIs.
 */
class scheduling_config
{
public:
  /**
   * @brief Return the requested scheduling intent.
   */
  [[nodiscard]] constexpr auto
  intent() const noexcept -> scheduling_intent
  {
    return intent_;
  }

  /**
   * @brief Return the numeric priority payload for the selected intent.
   * @return Realtime priority or nice value depending on @ref intent().
   */
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

/** @brief Helpers for building @ref scheduling_config values. */
namespace schedule
{
/** @brief Request background scheduling. */
[[nodiscard]] constexpr auto
background() noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::background);
}

/** @brief Request default scheduling. */
[[nodiscard]] constexpr auto
normal() noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::normal);
}

/** @brief Request interactive scheduling. */
[[nodiscard]] constexpr auto
interactive() noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::interactive);
}

/** @brief Request low-latency scheduling. */
[[nodiscard]] constexpr auto
low_latency() noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::low_latency);
}

/**
 * @brief Request realtime FIFO scheduling.
 * @param priority Valid realtime priority.
 */
[[nodiscard]] constexpr auto
realtime_fifo(realtime_priority priority) noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::realtime_fifo, priority.value());
}

/**
 * @brief Request realtime round-robin scheduling.
 * @param priority Valid realtime priority.
 */
[[nodiscard]] constexpr auto
realtime_rr(realtime_priority priority) noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::realtime_round_robin, priority.value());
}

/**
 * @brief Request scheduling via explicit nice value.
 * @param value Nice value in the supported platform range.
 */
[[nodiscard]] constexpr auto
nice(nice_value value) noexcept -> scheduling_config
{
  return detail::scheduling_config_access::make(scheduling_intent::nice, value.value());
}

/**
 * @brief Request scheduling via portable priority level presets.
 * @param level Portable priority preset mapped to a nice value.
 */
[[nodiscard]] constexpr auto
priority(priority_level level) noexcept -> scheduling_config
{
  return nice(nice_value{ static_cast<int>(level) });
}
} // namespace schedule

} // namespace threadschedule
