#pragma once

#include "result.hpp"

#include <cstdint>
#include <stdexcept>

namespace threadschedule
{

class cpu_id
{
public:
  constexpr explicit cpu_id(int value) : value_(checked(value)) {}

  [[nodiscard]] static auto
  create(int value) noexcept -> result<cpu_id>
  {
    if (value < 0)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return cpu_id(unchecked_tag{}, static_cast<std::uint32_t>(value));
  }

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
