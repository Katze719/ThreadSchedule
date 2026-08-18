#pragma once

#include <chrono>
#include <cstddef>

namespace threadschedule
{

struct pool_statistics
{
  std::size_t total_threads{ 0 };
  std::size_t active_threads{ 0 };
  std::size_t pending_tasks{ 0 };
  std::size_t completed_tasks{ 0 };
  std::size_t stolen_tasks{ 0 };
  double tasks_per_second{ 0.0 };
  std::chrono::microseconds average_task_time{ 0 };
};

} // namespace threadschedule
