#pragma once

#include "../../shutdown_policy.hpp"
#include "../../task_error.hpp"
#include "../../thread_config.hpp"
#include "../pool/shutdown.hpp"
#include "../thread/control.hpp"
#include "backend.hpp"
#include "scheduled_task.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

namespace threadschedule
{
struct scheduled_pool_config
{
  std::size_t worker_count{ std::thread::hardware_concurrency() };
  bool register_workers{ false };
  thread_config workers{};
  thread_config scheduler{};
  shutdown_policy shutdown{ shutdown_policy::drain };
  error_callback on_task_error{};
};

class scheduled_pool
{
public:
  scheduled_pool() : scheduled_pool(scheduled_pool_config{}) {}

  explicit scheduled_pool(std::size_t worker_count) : scheduled_pool(scheduled_pool_config{ worker_count }) {}

  explicit scheduled_pool(scheduled_pool_config config)
      : config_(std::move(config)),
        impl_(std::make_unique<detail::scheduled_pool_backend>(config_.worker_count, config_.register_workers))
  {
    if (detail::has_thread_configuration(config_.workers))
      {
        auto configured = impl_->configure_threads(detail::to_native(config_.workers));
        if (!configured)
          throw std::system_error(configured.error(), "scheduled_pool worker configuration");
      }

    if (detail::has_thread_configuration(config_.scheduler))
      {
        auto configured = impl_->configure_scheduler_thread(detail::to_native(config_.scheduler));
        if (!configured)
          throw std::system_error(configured.error(), "scheduled_pool scheduler configuration");
      }
  }

  scheduled_pool(scheduled_pool&& other) noexcept
      : config_(std::move(other.config_)), impl_(std::move(other.impl_)),
        stopped_(other.stopped_.load(std::memory_order_acquire))
  {
    other.stopped_.store(true, std::memory_order_release);
  }

  auto
  operator=(scheduled_pool&& other) noexcept -> scheduled_pool&
  {
    if (this != &other)
      {
        if (impl_)
          {
            try
              {
                impl_->shutdown(detail::to_native(config_.shutdown));
              }
            catch (...)
              {
              }
          }
        config_ = std::move(other.config_);
        impl_ = std::move(other.impl_);
        stopped_.store(other.stopped_.load(std::memory_order_acquire), std::memory_order_release);
        other.stopped_.store(true, std::memory_order_release);
      }
    return *this;
  }
  scheduled_pool(scheduled_pool const&) = delete;
  auto operator=(scheduled_pool const&) -> scheduled_pool& = delete;

  ~scheduled_pool()
  {
    if (!impl_)
      return;
    try
      {
        impl_->shutdown(detail::to_native(config_.shutdown));
      }
    catch (...)
      {
      }
  }

  static auto
  create(scheduled_pool_config config = {}) -> result<scheduled_pool>
  {
    try
      {
        return scheduled_pool(std::move(config));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  template <typename Rep, typename Period, typename F>
  auto
  schedule_after(std::chrono::duration<Rep, Period> delay, F&& function) -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    try
      {
        auto handle = impl_->schedule_after(std::chrono::duration_cast<detail::scheduled_pool_backend::duration>(delay),
                                            wrap_task(std::forward<F>(function)));
        if (handle.is_cancelled())
          return unexpected(std::make_error_code(std::errc::operation_canceled));
        return scheduled_task(std::move(handle));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  template <typename F>
  auto
  schedule_at(std::chrono::steady_clock::time_point time, F&& function) -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    try
      {
        auto handle = impl_->schedule_at(time, wrap_task(std::forward<F>(function)));
        if (handle.is_cancelled())
          return unexpected(std::make_error_code(std::errc::operation_canceled));
        return scheduled_task(std::move(handle));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  template <typename Rep, typename Period, typename F>
  auto
  schedule_periodic(std::chrono::duration<Rep, Period> interval, F&& function) -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    auto const native_interval = std::chrono::duration_cast<detail::scheduled_pool_backend::duration>(interval);
    if (native_interval <= detail::scheduled_pool_backend::duration::zero())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    try
      {
        auto handle = impl_->schedule_periodic(native_interval, wrap_task(std::forward<F>(function)));
        if (handle.is_cancelled())
          return unexpected(std::make_error_code(std::errc::operation_canceled));
        return scheduled_task(std::move(handle));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  template <typename InitialRep, typename InitialPeriod, typename IntervalRep, typename IntervalPeriod, typename F>
  auto
  schedule_periodic_after(std::chrono::duration<InitialRep, InitialPeriod> initial_delay,
                          std::chrono::duration<IntervalRep, IntervalPeriod> interval, F&& function)
      -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    auto const native_interval = std::chrono::duration_cast<detail::scheduled_pool_backend::duration>(interval);
    if (native_interval <= detail::scheduled_pool_backend::duration::zero())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    try
      {
        auto handle = impl_->schedule_periodic_after(
            std::chrono::duration_cast<detail::scheduled_pool_backend::duration>(initial_delay), native_interval,
            wrap_task(std::forward<F>(function)));
        if (handle.is_cancelled())
          return unexpected(std::make_error_code(std::errc::operation_canceled));
        return scheduled_task(std::move(handle));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  shutdown(shutdown_policy policy = shutdown_policy::drain) -> result<void>
  {
    if (!impl_)
      return {};
    try
      {
        impl_->shutdown(detail::to_native(policy));
        stopped_.store(true, std::memory_order_release);
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  [[nodiscard]] auto
  scheduled_count() const -> std::size_t
  {
    return impl_ ? impl_->scheduled_count() : 0;
  }

private:
  template <typename F>
  auto
  wrap_task(F&& function)
  {
    using function_type = std::decay_t<F>;
    auto callback = config_.on_task_error;
    return [function = function_type(std::forward<F>(function)), callback = std::move(callback)]() mutable
      {
        try
          {
            std::invoke(function);
          }
        catch (...)
          {
            if (callback)
              {
                try
                  {
                    callback(task_error::capture());
                  }
                catch (...)
                  {
                  }
              }
          }
      };
  }

  scheduled_pool_config config_{};
  std::unique_ptr<detail::scheduled_pool_backend> impl_;
  std::atomic<bool> stopped_{ false };
};

} // namespace threadschedule
