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
  /** @brief Header-only mode: implementation is compiled into each translation unit. */
  header_only,
  /** @brief Runtime mode: implementation is provided by the shared runtime library. */
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
