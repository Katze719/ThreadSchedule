#pragma once

/**
 * @file thread_id.hpp
 * @brief Strongly-typed registry thread identifier.
 */

#include "result.hpp"

#include <cstdint>
#include <functional>
#include <stdexcept>

namespace threadschedule
{
namespace detail
{
struct thread_id_access;
}

class thread_id
{
public:
  /**
   * @brief Construct and validate a thread id.
   * @param value Positive numeric thread id.
   * @throws std::invalid_argument if @p value is not positive.
   */
  constexpr explicit thread_id(std::int64_t value) : value_(checked(value)) {}

  /**
   * @brief Construct a thread id without exceptions.
   * @param value Positive numeric thread id.
   * @return A valid @ref thread_id or @c errc::invalid_argument.
   */
  [[nodiscard]] static auto
  create(std::int64_t value) noexcept -> result<thread_id>
  {
    if (value <= 0)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return thread_id(unchecked_tag{}, static_cast<std::uint64_t>(value));
  }

  friend constexpr auto
  operator==(thread_id lhs, thread_id rhs) noexcept -> bool
  {
    return lhs.value_ == rhs.value_;
  }
  friend constexpr auto
  operator!=(thread_id lhs, thread_id rhs) noexcept -> bool
  {
    return !(lhs == rhs);
  }
  friend constexpr auto
  operator<(thread_id lhs, thread_id rhs) noexcept -> bool
  {
    return lhs.value_ < rhs.value_;
  }

private:
  friend struct detail::thread_id_access;
  struct unchecked_tag
  {
  };

  constexpr thread_id(unchecked_tag, std::uint64_t value) noexcept : value_(value) {}

  [[nodiscard]] static constexpr auto
  checked(std::int64_t value) -> std::uint64_t
  {
    if (value <= 0)
      throw std::invalid_argument("thread_id must be positive");
    return static_cast<std::uint64_t>(value);
  }

  std::uint64_t value_;
};

namespace detail
{
struct thread_id_access
{
  [[nodiscard]] static constexpr auto
  make(std::uint64_t value) -> thread_id
  {
    if (value == 0)
      throw std::invalid_argument("thread_id must be positive");
    return thread_id(thread_id::unchecked_tag{}, value);
  }
  [[nodiscard]] static constexpr auto
  value(thread_id id) noexcept -> std::uint64_t
  {
    return id.value_;
  }
};
} // namespace detail

} // namespace threadschedule

namespace std
{
template <>
struct hash<threadschedule::thread_id>
{
  auto
  operator()(threadschedule::thread_id value) const noexcept -> size_t
  {
    return hash<std::uint64_t>{}(threadschedule::detail::thread_id_access::value(value));
  }
};
} // namespace std
