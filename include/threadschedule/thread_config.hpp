#pragma once

/**
 * @file thread_config.hpp
 * @brief Portable thread startup and runtime configuration.
 */

#include "scheduling.hpp"
#include "thread_affinity.hpp"

#include <optional>
#include <string>
#include <utility>

namespace threadschedule
{

class thread_config
{
public:
  thread_config() = default;

  auto
  set_name(std::string name) -> thread_config&
  {
    name_ = std::move(name);
    return *this;
  }

  auto
  clear_name() noexcept -> thread_config&
  {
    name_.reset();
    return *this;
  }

  auto
  set_scheduling(scheduling_config scheduling) noexcept -> thread_config&
  {
    scheduling_ = scheduling;
    return *this;
  }

  auto
  clear_scheduling() noexcept -> thread_config&
  {
    scheduling_.reset();
    return *this;
  }

  auto
  set_affinity(thread_affinity affinity) -> thread_config&
  {
    affinity_ = std::move(affinity);
    return *this;
  }

  auto
  clear_affinity() noexcept -> thread_config&
  {
    affinity_.reset();
    return *this;
  }

  [[nodiscard]] auto
  name() const noexcept -> std::optional<std::string> const&
  {
    return name_;
  }
  [[nodiscard]] auto
  scheduling() const noexcept -> std::optional<scheduling_config> const&
  {
    return scheduling_;
  }
  [[nodiscard]] auto
  affinity() const noexcept -> std::optional<thread_affinity> const&
  {
    return affinity_;
  }
  [[nodiscard]] auto
  empty() const noexcept -> bool
  {
    return !name_ && !scheduling_ && !affinity_;
  }

private:
  std::optional<std::string> name_;
  std::optional<scheduling_config> scheduling_;
  std::optional<thread_affinity> affinity_;
};

} // namespace threadschedule
