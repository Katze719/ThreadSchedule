#pragma once

/**
 * @file worker_count.hpp
 * @brief Worker-thread count configuration for pool types.
 */

#include "result.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <thread>

namespace threadschedule
{

/**
 * @brief Value type for pool worker count.
 *
 * The special automatic value resolves to at least one worker using
 * @c std::thread::hardware_concurrency().
 */
class worker_count
{
public:
  /** @brief Construct automatic worker count. */
  worker_count() noexcept = default;
  /**
   * @brief Construct explicit worker count.
   * @param value Number of workers, must be > 0.
   * @throws std::invalid_argument if @p value is zero.
   */
  explicit worker_count(std::size_t value) : value_(checked(value)) {}

  /**
   * @brief Create automatic worker count.
   * @return A value that resolves at runtime via @ref resolve.
   */
  [[nodiscard]] static constexpr auto
  automatic() noexcept -> worker_count
  {
    return worker_count(automatic_tag{});
  }

  /**
   * @brief Create explicit worker count without exceptions.
   * @param value Number of workers, must be > 0.
   * @return A valid @ref worker_count or @c errc::invalid_argument.
   */
  [[nodiscard]] static auto
  create(std::size_t value) noexcept -> result<worker_count>
  {
    if (value == 0)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return worker_count(unchecked_tag{}, value);
  }

  /** @brief Return whether this instance uses automatic resolution. */
  [[nodiscard]] constexpr auto
  is_automatic() const noexcept -> bool
  {
    return value_ == 0;
  }

  /**
   * @brief Resolve the effective worker count.
   * @return Explicit value if set, otherwise @c max(1, hardware_concurrency).
   */
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
