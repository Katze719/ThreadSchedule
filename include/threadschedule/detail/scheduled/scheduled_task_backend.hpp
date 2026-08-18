#pragma once

/** @file detail/scheduled/scheduled_task_backend.hpp
 *  @brief Internal cancellable scheduled-task handle.
 */

#include "scheduled_cancellation_state.hpp"

#include <cstdint>
#include <memory>

namespace threadschedule::detail
{

/**
 * @brief Copyable handle for a cancellable scheduled task.
 *
 * Copyable (its cancellation state is shared). Both cancel() and
 * is_cancelled() are thread-safe.
 *
 * Cancellation is cooperative: the scheduler checks the flag before
 * dispatching the task to the worker pool, but a task that is already
 * executing will **not** be interrupted.
 */
class scheduled_task_backend
{
public:
  explicit scheduled_task_backend(uint64_t id)
      : id_(id), cancellation_(std::make_shared<detail::scheduled_cancellation_state>())
  {
  }

  scheduled_task_backend(scheduled_task_backend const&) = default;
  auto operator=(scheduled_task_backend const&) -> scheduled_task_backend& = default;

  scheduled_task_backend(scheduled_task_backend&& other) noexcept
      : id_(other.id_), cancellation_(std::move(other.cancellation_))
  {
    other.id_ = 0;
  }

  auto
  operator=(scheduled_task_backend&& other) noexcept -> scheduled_task_backend&
  {
    if (this != &other)
      {
        id_ = other.id_;
        cancellation_ = std::move(other.cancellation_);
        other.id_ = 0;
      }
    return *this;
  }

  void
  cancel()
  {
    if (cancellation_)
      cancellation_->user_cancelled.store(true, std::memory_order_release);
  }

  [[nodiscard]] auto
  is_cancelled() const noexcept -> bool
  {
    return !cancellation_ || cancellation_->user_cancelled.load(std::memory_order_acquire)
           || cancellation_->pool_stopped.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto
  id() const noexcept -> uint64_t
  {
    return id_;
  }

private:
  uint64_t id_{ 0 };
  std::shared_ptr<detail::scheduled_cancellation_state> cancellation_;

  template <typename>
  friend class scheduled_pool_backend_base;
  [[nodiscard]] auto
  get_cancellation() const -> std::shared_ptr<detail::scheduled_cancellation_state>
  {
    return cancellation_;
  }
};

} // namespace threadschedule::detail
