#pragma once

/**
 * @file thread_affinity.hpp
 * @brief Portable normalized CPU-affinity value type.
 */

#include "cpu_id.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>
#include <vector>

namespace threadschedule
{

class thread_affinity
{
public:
  thread_affinity() = default;
  explicit thread_affinity(std::vector<cpu_id> cpus) : cpus_(std::move(cpus))
  {
    normalize();
  }

  thread_affinity(std::initializer_list<cpu_id> cpus) : cpus_(cpus)
  {
    normalize();
  }

  void
  add_cpu(cpu_id cpu)
  {
    if (contains(cpu))
      return;
    cpus_.push_back(cpu);
    std::sort(cpus_.begin(), cpus_.end());
  }

  void
  remove_cpu(cpu_id cpu)
  {
    cpus_.erase(std::remove(cpus_.begin(), cpus_.end(), cpu), cpus_.end());
  }

  void
  clear() noexcept
  {
    cpus_.clear();
  }

  [[nodiscard]] auto
  contains(cpu_id cpu) const noexcept -> bool
  {
    return std::binary_search(cpus_.begin(), cpus_.end(), cpu);
  }

  [[nodiscard]] auto
  empty() const noexcept -> bool
  {
    return cpus_.empty();
  }

  [[nodiscard]] auto
  cpus() const noexcept -> std::vector<cpu_id> const&
  {
    return cpus_;
  }

private:
  void
  normalize()
  {
    std::sort(cpus_.begin(), cpus_.end());
    cpus_.erase(std::unique(cpus_.begin(), cpus_.end()), cpus_.end());
  }

  std::vector<cpu_id> cpus_;
};

} // namespace threadschedule
