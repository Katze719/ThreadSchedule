#pragma once

#include "detail/scheduled/backend.hpp"

#include <cstdint>
#include <utility>

namespace threadschedule
{
namespace detail
{
struct scheduled_task_access;
}

class scheduled_task
{
public:
  scheduled_task(scheduled_task const&) = default;
  scheduled_task(scheduled_task&&) noexcept = default;
  auto operator=(scheduled_task const&) -> scheduled_task& = default;
  auto operator=(scheduled_task&&) noexcept -> scheduled_task& = default;

  void
  cancel()
  {
    impl_.cancel();
  }

  [[nodiscard]] auto
  is_cancelled() const noexcept -> bool
  {
    return impl_.is_cancelled();
  }

  [[nodiscard]] auto
  id() const noexcept -> std::uint64_t
  {
    return impl_.id();
  }

private:
  explicit scheduled_task(detail::scheduled_task_backend value) : impl_(std::move(value)) {}

  detail::scheduled_task_backend impl_;
  friend class scheduled_pool;
  friend struct detail::scheduled_task_access;
};

namespace detail
{
struct scheduled_task_access
{
  [[nodiscard]] static auto
  make(scheduled_task_backend value) -> scheduled_task
  {
    return scheduled_task(std::move(value));
  }
};
} // namespace detail

} // namespace threadschedule
