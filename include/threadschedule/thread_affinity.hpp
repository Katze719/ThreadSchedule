#pragma once

/**
 * @file thread_affinity.hpp
 * @brief Portable normalized CPU-affinity value type.
 */

#include <algorithm>
#include <utility>
#include <vector>

namespace threadschedule
{

class thread_affinity
{
public:
  thread_affinity() = default;
  explicit thread_affinity(std::vector<int> cpus) : cpus_(std::move(cpus))
  {
    normalize();
  }

  void
  add_cpu(int cpu)
  {
    if (cpu < 0 || contains(cpu))
      return;
    cpus_.push_back(cpu);
    std::sort(cpus_.begin(), cpus_.end());
  }

  void
  remove_cpu(int cpu)
  {
    cpus_.erase(std::remove(cpus_.begin(), cpus_.end(), cpu), cpus_.end());
  }

  void
  clear() noexcept
  {
    cpus_.clear();
  }

  [[nodiscard]] auto
  contains(int cpu) const noexcept -> bool
  {
    return std::binary_search(cpus_.begin(), cpus_.end(), cpu);
  }

  [[nodiscard]] auto
  empty() const noexcept -> bool
  {
    return cpus_.empty();
  }

  [[nodiscard]] auto
  cpus() const noexcept -> std::vector<int> const&
  {
    return cpus_;
  }

private:
  void
  normalize()
  {
    cpus_.erase(std::remove_if(cpus_.begin(), cpus_.end(), [](int cpu) { return cpu < 0; }), cpus_.end());
    std::sort(cpus_.begin(), cpus_.end());
    cpus_.erase(std::unique(cpus_.begin(), cpus_.end()), cpus_.end());
  }

  std::vector<int> cpus_;
};

} // namespace threadschedule
