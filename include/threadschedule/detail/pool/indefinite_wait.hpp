#pragma once

/** @file detail/pool/indefinite_wait.hpp
 *  @brief Blocking idle-wait strategy for queue-based pools.
 */

#include <condition_variable>

namespace threadschedule::detail
{

struct indefinite_wait
{
  template <typename Lock, typename Pred>
  static auto
  wait(std::condition_variable& cv, Lock& lock, Pred pred) -> bool
  {
    cv.wait(lock, pred);
    return true;
  }
};

} // namespace threadschedule::detail
