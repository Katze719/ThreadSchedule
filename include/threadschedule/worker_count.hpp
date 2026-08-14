#pragma once

#include "result.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <thread>

namespace threadschedule
{

class worker_count
{
public:
  worker_count() noexcept = default;
  explicit worker_count(std::size_t value) : value_(checked(value)) {}

  [[nodiscard]] static constexpr auto
  automatic() noexcept -> worker_count
  {
    return worker_count(automatic_tag{});
  }

  [[nodiscard]] static auto
  create(std::size_t value) noexcept -> result<worker_count>
  {
    if (value == 0)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return worker_count(unchecked_tag{}, value);
  }

  [[nodiscard]] constexpr auto
  is_automatic() const noexcept -> bool
  {
    return value_ == 0;
  }

  [[nodiscard]] auto
  resolve() const noexcept -> std::size_t
  {
    if (!is_automatic())
      return value_;
    return std::max<std::size_t>(1, std::thread::hardware_concurrency());
  }

private:
  struct automatic_tag
  {
  };
  struct unchecked_tag
  {
  };

  constexpr explicit worker_count(automatic_tag) noexcept {}
  constexpr worker_count(unchecked_tag, std::size_t value) noexcept : value_(value) {}

  [[nodiscard]] static auto
  checked(std::size_t value) -> std::size_t
  {
    if (value == 0)
      throw std::invalid_argument("worker_count must be positive");
    return value;
  }

  std::size_t value_{ 0 };
};

} // namespace threadschedule
