#pragma once

/**
 * @file shutdown_policy.hpp
 * @brief Portable pool shutdown behavior.
 */

#include <cstdint>

namespace threadschedule
{

enum class shutdown_policy : std::uint8_t
{
  drain,
  drop_pending
};

} // namespace threadschedule
