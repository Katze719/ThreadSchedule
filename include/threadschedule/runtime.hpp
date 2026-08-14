#pragma once

/**
 * @file runtime.hpp
 * @brief Shared-runtime mode inspection.
 */

#include "export.hpp"

#include <cstdint>

namespace threadschedule
{

/** @brief Describes whether ThreadSchedule uses inline storage or its shared runtime. */
enum class build_mode : std::uint8_t
{
  header_only,
  runtime
};

#if defined(THREADSCHEDULE_RUNTIME)
inline constexpr bool is_runtime_build = true;
THREADSCHEDULE_API auto current_build_mode() -> build_mode;
#else
inline constexpr bool is_runtime_build = false;

[[nodiscard]] inline constexpr auto
current_build_mode() noexcept -> build_mode
{
  return build_mode::header_only;
}
#endif

[[nodiscard]] inline constexpr auto
build_mode_string() noexcept -> char const*
{
  return is_runtime_build ? "runtime" : "header-only";
}

} // namespace threadschedule
