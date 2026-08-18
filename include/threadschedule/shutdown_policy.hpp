#pragma once

/**
 * @file shutdown_policy.hpp
 * @brief Portable pool shutdown behavior.
 */

#include <cstdint>

namespace threadschedule
{

/**
 * @brief Defines how pools handle pending work during shutdown.
 */
enum class shutdown_policy : std::uint8_t
{
  /** @brief Finish queued work before returning from shutdown. */
  drain,
  /** @brief Cancel/drop queued work that has not started yet. */
  drop_pending
};

} // namespace threadschedule
