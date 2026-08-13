#pragma once

#include "backend.hpp"

#include <cstdint>
#include <utility>

namespace threadschedule
{
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
};

} // namespace threadschedule
