#pragma once

/**
 * @file realtime_priority.hpp
 * @brief Validated realtime priority wrapper.
 */

#include "result.hpp"

#include <stdexcept>

namespace threadschedule
{

/**
 * @brief Strongly-typed realtime priority in the range $[1, 99]$.
 */
class realtime_priority
{
public:
  /** @brief Lowest accepted realtime priority value. */
  static constexpr int minimum = 1;
  /** @brief Highest accepted realtime priority value. */
  static constexpr int maximum = 99;

  /**
   * @brief Construct and validate a realtime priority.
   * @param value Realtime priority in the range @ref minimum .. @ref maximum.
   * @throws std::invalid_argument if @p value is out of range.
   */
  constexpr explicit realtime_priority(int value) : value_(checked(value)) {}

  /**
   * @brief Construct a realtime priority without exceptions.
   * @param value Realtime priority in the range @ref minimum .. @ref maximum.
   * @return A valid @ref realtime_priority or @c errc::invalid_argument.
   */
  [[nodiscard]] static auto
  create(int value) noexcept -> result<realtime_priority>
  {
    if (value < minimum || value > maximum)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return realtime_priority(unchecked_tag{}, value);
  }

  /** @brief Return the wrapped realtime priority value. */
  [[nodiscard]] constexpr auto
  value() const noexcept -> int
  {
    return value_;
  }

  friend constexpr auto
  operator==(realtime_priority lhs, realtime_priority rhs) noexcept -> bool
  {
    return lhs.value_ == rhs.value_;
  }
  friend constexpr auto
  operator!=(realtime_priority lhs, realtime_priority rhs) noexcept -> bool
  {
    return !(lhs == rhs);
  }

private:
  struct unchecked_tag
  {
  };
  constexpr realtime_priority(unchecked_tag, int value) noexcept : value_(value) {}

  [[nodiscard]] static constexpr auto
  checked(int value) -> int
  {
    if (value < minimum || value > maximum)
      throw std::invalid_argument("realtime_priority must be in the range 1..99");
    return value;
  }

  int value_;
};

} // namespace threadschedule
