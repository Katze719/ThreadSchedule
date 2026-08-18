#pragma once

/**
 * @file pool_statistics.hpp
 * @brief Aggregate runtime counters exposed by pool implementations.
 */

#include <chrono>
#include <cstddef>

namespace threadschedule
{

/**
 * @brief Snapshot of pool activity metrics.
 *
 * All values are best-effort observability counters and may change while a
 * workload is running.
 */
struct pool_statistics
{
  /** @brief Number of worker threads owned by the pool. */
  std::size_t total_threads{ 0 };
  /** @brief Number of workers currently executing a task. */
  std::size_t active_threads{ 0 };
  /** @brief Number of queued tasks waiting for execution. */
  std::size_t pending_tasks{ 0 };
  /** @brief Total number of tasks completed since pool start. */
  std::size_t completed_tasks{ 0 };
  /** @brief Total number of tasks stolen by workers (work-stealing pools). */
  std::size_t stolen_tasks{ 0 };
  /** @brief Approximate throughput in tasks per second. */
  double tasks_per_second{ 0.0 };
  /** @brief Average task execution time measured by the backend. */
  std::chrono::microseconds average_task_time{ 0 };
};

} // namespace threadschedule
