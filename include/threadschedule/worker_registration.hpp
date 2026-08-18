#pragma once

#include <cstdint>

namespace threadschedule
{

enum class worker_registration : std::uint8_t
{
  disabled,
  global_registry
};

} // namespace threadschedule
