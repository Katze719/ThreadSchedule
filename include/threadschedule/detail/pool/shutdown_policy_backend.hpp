#pragma once

/** @file detail/pool/shutdown_policy_backend.hpp
 *  @brief Internal queued-task shutdown behavior.
 */

#include <cstdint>

namespace threadschedule::detail
{

enum class shutdown_policy_backend : std::uint8_t
{
  drain,
  drop_pending
};

} // namespace threadschedule::detail
