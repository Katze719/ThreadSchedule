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

/**
 * @brief Portable thread configuration bundle.
 *
 * Instances can be reused across thread creation and runtime reconfiguration
 * APIs. Any unset field is ignored when applied.
 */
class thread_config
{
public:
  thread_config() = default;

  /**
   * @brief Set thread name.
   * @param name User-visible thread name.
   */
  auto
  set_name(std::string name) -> thread_config&
  {
    name_ = std::move(name);
    return *this;
  }

  /** @brief Clear previously configured thread name. */
  auto
  clear_name() noexcept -> thread_config&
  {
    name_.reset();
    return *this;
  }

  /**
   * @brief Set scheduling configuration.
   * @param scheduling Portable scheduling request.
   */
  auto
  set_scheduling(scheduling_config scheduling) noexcept -> thread_config&
  {
    scheduling_ = scheduling;
    return *this;
  }

  /** @brief Clear previously configured scheduling request. */
  auto
  clear_scheduling() noexcept -> thread_config&
  {
    scheduling_.reset();
    return *this;
  }

  /**
   * @brief Set CPU affinity mask.
   * @param affinity Normalized affinity set.
   */
  auto
  set_affinity(thread_affinity affinity) -> thread_config&
  {
    affinity_ = std::move(affinity);
    return *this;
  }

  /** @brief Clear previously configured CPU affinity. */
  auto
  clear_affinity() noexcept -> thread_config&
  {
    affinity_.reset();
    return *this;
  }

  /** @brief Return configured thread name if present. */
  [[nodiscard]] auto
  get_name() const noexcept -> std::optional<std::string> const&
  {
    return name_;
  }
  /** @brief Return configured scheduling request if present. */
  [[nodiscard]] auto
  get_scheduling() const noexcept -> std::optional<scheduling_config> const&
  {
    return scheduling_;
  }
  /** @brief Return configured affinity set if present. */
  [[nodiscard]] auto
  get_affinity() const noexcept -> std::optional<thread_affinity> const&
  {
    return affinity_;
  }
  /** @brief Return whether no settings are currently configured. */
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
