#pragma once

#include "result.hpp"

#include <stdexcept>

namespace threadschedule
{

class realtime_priority
{
public:
  static constexpr int minimum = 1;
  static constexpr int maximum = 99;

  constexpr explicit realtime_priority(int value) : value_(checked(value)) {}

  [[nodiscard]] static auto
  create(int value) noexcept -> result<realtime_priority>
  {
    if (value < minimum || value > maximum)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return realtime_priority(unchecked_tag{}, value);
  }

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
