#pragma once

/**
 * @file cpu_id.hpp
 * @brief Strongly-typed CPU index used by affinity-related APIs.
 */

#include "result.hpp"

#include <cstdint>
#include <stdexcept>

namespace threadschedule
{

/**
 * @brief Validated CPU identifier.
 *
 * This type guarantees a non-negative CPU index and is used by
 * @ref thread_affinity to avoid accidental mixing with unrelated integers.
 */
class cpu_id
{
public:
  /**
   * @brief Construct a CPU id and throw on invalid input.
   * @param value CPU index, must be >= 0.
   * @throws std::invalid_argument if @p value is negative.
   */
  constexpr explicit cpu_id(int value) : value_(checked(value)) {}

  /**
   * @brief Construct a CPU id without exceptions.
   * @param value CPU index, must be >= 0.
   * @return A valid @ref cpu_id or @c errc::invalid_argument.
   */
  [[nodiscard]] static auto
  create(int value) noexcept -> result<cpu_id>
  {
    if (value < 0)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return cpu_id(unchecked_tag{}, static_cast<std::uint32_t>(value));
  }

  /** @brief Return the underlying CPU index. */
  [[nodiscard]] constexpr auto
  value() const noexcept -> std::uint32_t
  {
    return value_;
  }

  friend constexpr auto
  operator==(cpu_id lhs, cpu_id rhs) noexcept -> bool
  {
    return lhs.value_ == rhs.value_;
  }
  friend constexpr auto
  operator!=(cpu_id lhs, cpu_id rhs) noexcept -> bool
  {
    return !(lhs == rhs);
  }
  friend constexpr auto
  operator<(cpu_id lhs, cpu_id rhs) noexcept -> bool
  {
    return lhs.value_ < rhs.value_;
  }

private:
  struct unchecked_tag
  {
  };

  constexpr cpu_id(unchecked_tag, std::uint32_t value) noexcept : value_(value) {}

  [[nodiscard]] static constexpr auto
  checked(int value) -> std::uint32_t
  {
    if (value < 0)
      throw std::invalid_argument("cpu_id must not be negative");
    return static_cast<std::uint32_t>(value);
  }

  std::uint32_t value_;
};

} // namespace threadschedule
