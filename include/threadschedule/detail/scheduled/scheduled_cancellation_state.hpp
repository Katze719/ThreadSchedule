#pragma once

/** @file detail/scheduled/scheduled_cancellation_state.hpp
 *  @brief Shared cancellation flags for scheduled task handles.
 */

#include <atomic>

namespace threadschedule::detail
{

struct scheduled_cancellation_state
{
  std::atomic<bool> user_cancelled{ false };
  std::atomic<bool> pool_stopped{ false };
};

} // namespace threadschedule::detail
