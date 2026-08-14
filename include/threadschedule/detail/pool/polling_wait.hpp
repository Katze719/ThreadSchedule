#pragma once

/** @file detail/pool/polling_wait.hpp
 *  @brief Timed idle-wait strategy for queue-based pools.
 */

#include <chrono>
#include <condition_variable>

namespace threadschedule::detail
{

template <unsigned IntervalMs = 10>
struct polling_wait
{
  template <typename Lock, typename Pred>
  static auto
  wait(std::condition_variable& cv, Lock& lock, Pred pred) -> bool
  {
    return cv.wait_for(lock, std::chrono::milliseconds(IntervalMs), pred);
  }
};

} // namespace threadschedule::detail
