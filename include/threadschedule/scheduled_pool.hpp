#pragma once

#include "detail/pool/shutdown.hpp"
#include "detail/scheduled/backend.hpp"
#include "detail/thread/control.hpp"
#include "scheduled_task.hpp"
#include "shutdown_policy.hpp"
#include "task_error.hpp"
#include "thread_config.hpp"
#include "worker_count.hpp"
#include "worker_registration.hpp"

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
class scheduled_pool_config
{
public:
  auto
  set_worker_count(worker_count value) noexcept -> scheduled_pool_config&
  {
    worker_count_ = value;
    return *this;
  }
  auto
  set_registration(worker_registration value) noexcept -> scheduled_pool_config&
  {
    registration_ = value;
    return *this;
  }
  auto
  set_worker_config(thread_config value) -> scheduled_pool_config&
  {
    workers_ = std::move(value);
    return *this;
  }
  auto
  set_scheduler_config(thread_config value) -> scheduled_pool_config&
  {
    scheduler_ = std::move(value);
    return *this;
  }
  auto
  set_shutdown_policy(shutdown_policy value) noexcept -> scheduled_pool_config&
  {
    shutdown_ = value;
    return *this;
  }
  auto
  set_error_callback(error_callback value) -> scheduled_pool_config&
  {
    on_task_error_ = std::move(value);
    return *this;
  }

  [[nodiscard]] auto
  get_worker_count() const noexcept -> worker_count
  {
    return worker_count_;
  }
  [[nodiscard]] auto
  get_registration() const noexcept -> worker_registration
  {
    return registration_;
  }
  [[nodiscard]] auto
  get_worker_config() const noexcept -> thread_config const&
  {
    return workers_;
  }
  [[nodiscard]] auto
  get_scheduler_config() const noexcept -> thread_config const&
  {
    return scheduler_;
  }
  [[nodiscard]] auto
  get_shutdown_policy() const noexcept -> shutdown_policy
  {
    return shutdown_;
  }
  [[nodiscard]] auto
  get_error_callback() const noexcept -> error_callback const&
  {
    return on_task_error_;
  }

private:
  worker_count worker_count_{ worker_count::automatic() };
  worker_registration registration_{ worker_registration::disabled };
  thread_config workers_;
  thread_config scheduler_;
  shutdown_policy shutdown_{ shutdown_policy::drain };
  error_callback on_task_error_;
};

class scheduled_pool
{
public:
  scheduled_pool() : scheduled_pool(scheduled_pool_config{}) {}

  explicit scheduled_pool(worker_count count) : scheduled_pool(scheduled_pool_config{}.set_worker_count(count)) {}

  explicit scheduled_pool(scheduled_pool_config config)
      : config_(std::move(config)),
        impl_(std::make_unique<detail::scheduled_pool_backend>(
            config_.get_worker_count().resolve(), config_.get_registration() == worker_registration::global_registry))
  {
    if (detail::has_thread_configuration(config_.get_worker_config()))
      {
        auto configured = impl_->configure_threads(detail::to_native(config_.get_worker_config()));
        if (!configured)
          throw std::system_error(configured.error(), "scheduled_pool worker configuration");
      }

    if (detail::has_thread_configuration(config_.get_scheduler_config()))
      {
        auto configured = impl_->configure_scheduler_thread(detail::to_native(config_.get_scheduler_config()));
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
                impl_->shutdown(detail::to_native(config_.get_shutdown_policy()));
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
        impl_->shutdown(detail::to_native(config_.get_shutdown_policy()));
      }
    catch (...)
      {
      }
  }

  static auto
  create(scheduled_pool_config config = {}) -> result<scheduled_pool>
  {
    return detail::try_result([&config]() -> result<scheduled_pool> { return scheduled_pool(std::move(config)); });
  }

  template <typename Rep, typename Period, typename F>
  auto
  schedule_after(std::chrono::duration<Rep, Period> delay, F&& function) -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<scheduled_task>
          {
            auto handle
                = impl_->schedule_after(std::chrono::duration_cast<detail::scheduled_pool_backend::duration>(delay),
                                        wrap_task(std::forward<F>(function)));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task(std::move(handle));
          });
  }

  template <typename F>
  auto
  schedule_at(std::chrono::steady_clock::time_point time, F&& function) -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<scheduled_task>
          {
            auto handle = impl_->schedule_at(time, wrap_task(std::forward<F>(function)));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task(std::move(handle));
          });
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
    return detail::try_result(
        [&]() -> result<scheduled_task>
          {
            auto handle = impl_->schedule_periodic(native_interval, wrap_task(std::forward<F>(function)));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task(std::move(handle));
          });
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
    return detail::try_result(
        [&]() -> result<scheduled_task>
          {
            auto handle = impl_->schedule_periodic_after(
                std::chrono::duration_cast<detail::scheduled_pool_backend::duration>(initial_delay), native_interval,
                wrap_task(std::forward<F>(function)));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task(std::move(handle));
          });
  }

  auto
  shutdown() -> result<void>
  {
    return shutdown(config_.get_shutdown_policy());
  }

  auto
  shutdown(shutdown_policy policy) -> result<void>
  {
    if (!impl_)
      return {};
    return detail::try_result(
        [&]() -> result<void>
          {
            impl_->shutdown(detail::to_native(policy));
            stopped_.store(true, std::memory_order_release);
            return {};
          });
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
    auto callback = config_.get_error_callback();
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
