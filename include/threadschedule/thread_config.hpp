#pragma once

/**
 * @file thread_config.hpp
 * @brief Portable thread startup and runtime configuration.
 */

#include "scheduling.hpp"
#include "thread_affinity.hpp"

#include <optional>
#include <string>

namespace threadschedule
{

struct thread_config
{
  std::string name{};
  scheduling_config scheduling{ schedule::normal() };
  std::optional<thread_affinity> affinity{};
};

} // namespace threadschedule
