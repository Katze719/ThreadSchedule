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
  /** @brief Construct an empty affinity set (no CPU pinning requested). */
  thread_affinity() = default;
  /**
   * @brief Construct from a list of CPUs.
   * @param cpus CPU identifiers; duplicates are removed and values are sorted.
   */
  explicit thread_affinity(std::vector<cpu_id> cpus) : cpus_(std::move(cpus))
  {
    normalize();
  }

  /**
   * @brief Construct from an initializer list of CPUs.
   * @param cpus CPU identifiers; duplicates are removed and values are sorted.
   */
  thread_affinity(std::initializer_list<cpu_id> cpus) : cpus_(cpus)
  {
    normalize();
  }

  /**
   * @brief Add a CPU to the affinity set.
   * @param cpu CPU to add.
   */
  void
  add_cpu(cpu_id cpu)
  {
    if (contains(cpu))
      return;
    cpus_.push_back(cpu);
    std::sort(cpus_.begin(), cpus_.end());
  }

  /**
   * @brief Remove a CPU from the affinity set.
   * @param cpu CPU to remove.
   */
  void
  remove_cpu(cpu_id cpu)
  {
    cpus_.erase(std::remove(cpus_.begin(), cpus_.end(), cpu), cpus_.end());
  }

  /** @brief Remove all CPUs from the affinity set. */
  void
  clear() noexcept
  {
    cpus_.clear();
  }

  /**
   * @brief Check whether a CPU is present in the affinity set.
   * @param cpu CPU identifier to test.
   */
  [[nodiscard]] auto
  contains(cpu_id cpu) const noexcept -> bool
  {
    return std::binary_search(cpus_.begin(), cpus_.end(), cpu);
  }

  /** @brief Return whether the affinity set is empty. */
  [[nodiscard]] auto
  empty() const noexcept -> bool
  {
    return cpus_.empty();
  }

  /** @brief Return the normalized sorted list of CPUs in the set. */
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
