#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <thread>

namespace threadschedule::detail
{

[[nodiscard]] inline auto
default_worker_count() noexcept -> std::size_t
{
  return (std::max)(std::size_t{ 1 }, static_cast<std::size_t>(std::thread::hardware_concurrency()));
}

[[nodiscard]] inline auto
checked_worker_count(std::size_t count) -> std::size_t
{
  if (count == 0)
    throw std::invalid_argument("worker count must be positive");
  return count;
}

} // namespace threadschedule::detail
