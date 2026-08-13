#pragma once

/**
 * @file detail/pool/wait_policy.hpp
 * @brief Idle wait strategies shared by queue-based pools.
 *
 * Internal implementation fragment included by backend.hpp inside
 * threadschedule::detail.
 */

// ---------------------------------------------------------------------------
// Wait policies for thread_pool_backend_base
// ---------------------------------------------------------------------------

/**
 * @brief Wait policy that blocks indefinitely until work is available.
 *
 * Workers consume zero CPU while idle but wake instantly when a task is
 * enqueued. Used by the @c thread_pool_backend type alias.
 */
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

/**
 * @brief Wait policy that polls with a configurable timeout.
 *
 * Workers periodically re-check the queue even without notification, trading
 * a small amount of CPU for lower wake-up latency under bursty workloads.
 * Used by the @c polling_pool_backend type alias (default 10 ms).
 *
 * @tparam IntervalMs Polling interval in milliseconds.
 */
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
