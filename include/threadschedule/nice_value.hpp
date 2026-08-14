#pragma once

#include "result.hpp"

#include <stdexcept>

namespace threadschedule
{

class nice_value
{
public:
  static constexpr int minimum = -20;
  static constexpr int maximum = 19;

  constexpr explicit nice_value(int value) : value_(checked(value)) {}

  [[nodiscard]] static auto
  create(int value) noexcept -> result<nice_value>
  {
    if (value < minimum || value > maximum)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return nice_value(unchecked_tag{}, value);
  }

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
