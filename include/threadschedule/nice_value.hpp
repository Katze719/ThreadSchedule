#pragma once

/**
 * @file nice_value.hpp
 * @brief Validated POSIX nice priority wrapper.
 */

#include "result.hpp"

#include <stdexcept>

namespace threadschedule
{

/**
 * @brief Strongly-typed nice value in the POSIX range $[-20, 19]$.
 */
class nice_value
{
public:
  /** @brief Lowest accepted nice value (highest priority). */
  static constexpr int minimum = -20;
  /** @brief Highest accepted nice value (lowest priority). */
  static constexpr int maximum = 19;

  /**
   * @brief Construct and validate a nice value.
   * @param value Nice value in the range @ref minimum .. @ref maximum.
   * @throws std::invalid_argument if @p value is out of range.
   */
  constexpr explicit nice_value(int value) : value_(checked(value)) {}

  /**
   * @brief Construct a nice value without exceptions.
   * @param value Nice value in the range @ref minimum .. @ref maximum.
   * @return A valid @ref nice_value or @c errc::invalid_argument.
   */
  [[nodiscard]] static auto
  create(int value) noexcept -> result<nice_value>
  {
    if (value < minimum || value > maximum)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return nice_value(unchecked_tag{}, value);
  }

  /** @brief Return the wrapped nice value. */
  [[nodiscard]] constexpr auto
  value() const noexcept -> int
  {
    return value_;
  }

  friend constexpr auto
  operator==(nice_value lhs, nice_value rhs) noexcept -> bool
  {
    return lhs.value_ == rhs.value_;
  }
  friend constexpr auto
  operator!=(nice_value lhs, nice_value rhs) noexcept -> bool
  {
    return !(lhs == rhs);
  }

private:
  struct unchecked_tag
  {
  };
  constexpr nice_value(unchecked_tag, int value) noexcept : value_(value) {}

  [[nodiscard]] static constexpr auto
  checked(int value) -> int
  {
    if (value < minimum || value > maximum)
      throw std::invalid_argument("nice_value must be in the range -20..19");
    return value;
  }

  int value_;
};

} // namespace threadschedule
