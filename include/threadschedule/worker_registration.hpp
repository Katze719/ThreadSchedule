#pragma once

/**
 * @file worker_registration.hpp
 * @brief Worker registration behavior for pool-managed threads.
 */

#include <cstdint>

namespace threadschedule
{

/**
 * @brief Controls whether pool workers are registered in a thread registry.
 */
enum class worker_registration : std::uint8_t
{
  /** @brief Do not register workers in the global registry. */
  disabled,
  /** @brief Register workers in the global registry for discovery/control. */
  global_registry
};

} // namespace threadschedule
