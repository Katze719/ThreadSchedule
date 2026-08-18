#pragma once

#include "../../result.hpp"
#include "../../scheduled_task.hpp"
#include "../../shutdown_policy.hpp"
#include "../../thread_config.hpp"
#include "../../worker_count.hpp"
#include "../../worker_registration.hpp"
#include "../pool/shutdown.hpp"
#include "../thread/control.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <system_error>
#include <utility>

namespace threadschedule::detail
{

template <typename Backend>
class scheduled_pool_facade
{
public:
  explicit scheduled_pool_facade(worker_count count, worker_registration registration)
      : impl_(std::make_unique<Backend>(count.resolve(), registration == worker_registration::global_registry))
  {
  }

  scheduled_pool_facade(scheduled_pool_facade const&) = delete;
  auto operator=(scheduled_pool_facade const&) -> scheduled_pool_facade& = delete;
  scheduled_pool_facade(scheduled_pool_facade&&) = delete;
  auto operator=(scheduled_pool_facade&&) -> scheduled_pool_facade& = delete;

  template <typename Rep, typename Period, typename F>
  auto
  schedule_after(std::chrono::duration<Rep, Period> delay, F&& function) -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result(
        [&]() -> result<scheduled_task>
          {
            auto handle = impl_->schedule_after(std::chrono::duration_cast<typename Backend::duration>(delay),
                                                std::forward<F>(function));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task_access::make(std::move(handle));
          });
  }

  template <typename F>
  auto
  schedule_at(std::chrono::steady_clock::time_point time, F&& function) -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result(
        [&]() -> result<scheduled_task>
          {
            auto handle = impl_->schedule_at(time, std::forward<F>(function));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task_access::make(std::move(handle));
          });
  }

  template <typename Rep, typename Period, typename F>
  auto
  schedule_periodic(std::chrono::duration<Rep, Period> interval, F&& function) -> result<scheduled_task>
  {
    auto const native_interval = std::chrono::duration_cast<typename Backend::duration>(interval);
    if (native_interval <= Backend::duration::zero())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result(
        [&]() -> result<scheduled_task>
          {
            auto handle = impl_->schedule_periodic(native_interval, std::forward<F>(function));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task_access::make(std::move(handle));
          });
  }

  template <typename InitialRep, typename InitialPeriod, typename IntervalRep, typename IntervalPeriod, typename F>
  auto
  schedule_periodic_after(std::chrono::duration<InitialRep, InitialPeriod> initial_delay,
                          std::chrono::duration<IntervalRep, IntervalPeriod> interval, F&& function)
      -> result<scheduled_task>
  {
    auto const native_interval = std::chrono::duration_cast<typename Backend::duration>(interval);
    if (native_interval <= Backend::duration::zero())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result(
        [&]() -> result<scheduled_task>
          {
            auto handle
                = impl_->schedule_periodic_after(std::chrono::duration_cast<typename Backend::duration>(initial_delay),
                                                 native_interval, std::forward<F>(function));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task_access::make(std::move(handle));
          });
  }

  auto
  configure_workers(thread_config const& config) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result([&]() -> result<void> { return impl_->configure_threads(to_native(config)); });
  }

  auto
  configure_scheduler(thread_config const& config) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result([&]() -> result<void> { return impl_->configure_scheduler_thread(to_native(config)); });
  }

  auto
  shutdown(shutdown_policy policy = shutdown_policy::drain) -> result<void>
  {
    if (!impl_)
      return {};
    return try_result(
        [&]() -> result<void>
          {
            impl_->shutdown(to_native(policy));
            stopped_.store(true, std::memory_order_release);
            return {};
          });
  }

  [[nodiscard]] auto
  scheduled_count() const -> std::size_t
  {
    return impl_ ? impl_->scheduled_count() : 0;
  }

protected:
  ~scheduled_pool_facade() = default;

private:
  std::unique_ptr<Backend> impl_;
  std::atomic<bool> stopped_{ false };
};

} // namespace threadschedule::detail
