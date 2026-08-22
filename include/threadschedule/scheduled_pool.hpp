#pragma once

/**
 * @file scheduled_pool.hpp
 * @brief Time-based task scheduler with worker execution pool.
 */

#include "detail/pool/shutdown.hpp"
#include "detail/scheduled/backend.hpp"
#include "detail/thread/control.hpp"
#include "detail/time.hpp"
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
/**
 * @brief Builder-style configuration for @ref scheduled_pool.
 */
class scheduled_pool_config
{
public:
  /** @brief Set number of worker threads executing scheduled callbacks. */
  auto
  set_worker_count(worker_count value) noexcept -> scheduled_pool_config&
  {
    worker_count_ = value;
    return *this;
  }
  /** @brief Configure registry registration behavior for scheduler workers. */
  auto
  set_registration(worker_registration value) noexcept -> scheduled_pool_config&
  {
    registration_ = value;
    return *this;
  }
  /** @brief Set configuration applied to worker threads. */
  auto
  set_worker_config(thread_config value) -> scheduled_pool_config&
  {
    workers_ = std::move(value);
    return *this;
  }
  /** @brief Set configuration applied to the scheduler coordination thread. */
  auto
  set_scheduler_config(thread_config value) -> scheduled_pool_config&
  {
    scheduler_ = std::move(value);
    return *this;
  }
  /** @brief Set shutdown behavior for pending scheduled tasks. */
  auto
  set_shutdown_policy(shutdown_policy value) noexcept -> scheduled_pool_config&
  {
    shutdown_ = value;
    return *this;
  }
  /** @brief Set callback invoked when scheduled callbacks throw. */
  auto
  set_error_callback(error_callback value) -> scheduled_pool_config&
  {
    on_task_error_ = std::move(value);
    return *this;
  }

  /** @brief Return configured worker count. */
  [[nodiscard]] auto
  get_worker_count() const noexcept -> worker_count
  {
    return worker_count_;
  }
  /** @brief Return configured worker registration mode. */
  [[nodiscard]] auto
  get_registration() const noexcept -> worker_registration
  {
    return registration_;
  }
  /** @brief Return worker-thread configuration. */
  [[nodiscard]] auto
  get_worker_config() const noexcept -> thread_config const&
  {
    return workers_;
  }
  /** @brief Return scheduler-thread configuration. */
  [[nodiscard]] auto
  get_scheduler_config() const noexcept -> thread_config const&
  {
    return scheduler_;
  }
  /** @brief Return configured shutdown policy. */
  [[nodiscard]] auto
  get_shutdown_policy() const noexcept -> shutdown_policy
  {
    return shutdown_;
  }
  /** @brief Return configured asynchronous error callback. */
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

/**
 * @brief Scheduler that executes delayed and periodic tasks on worker threads.
 */
class scheduled_pool
{
public:
  /** @brief Construct with default scheduler configuration. */
  scheduled_pool() : scheduled_pool(scheduled_pool_config{}) {}

  /**
   * @brief Construct with explicit worker count.
   * @param count Number of worker threads or automatic resolution mode.
   */
  explicit scheduled_pool(worker_count count) : scheduled_pool(scheduled_pool_config{}.set_worker_count(count)) {}

  /**
   * @brief Construct from full configuration.
   * @param config Scheduler configuration.
   * @throws std::system_error if worker/scheduler thread configuration fails.
   */
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
        dispose_impl();
        config_ = std::move(other.config_);
        impl_ = std::move(other.impl_);
        stopped_.store(other.stopped_.load(std::memory_order_acquire), std::memory_order_release);
        other.stopped_.store(true, std::memory_order_release);
      }
    return *this;
  }
  scheduled_pool(scheduled_pool const&) = delete;
  auto operator=(scheduled_pool const&) -> scheduled_pool& = delete;

  /**
   * @brief Shutdown scheduler and workers according to configured policy.
   */
  ~scheduled_pool()
  {
    dispose_impl();
  }

  /**
   * @brief Create a scheduled pool without exceptions.
   * @param config Scheduler configuration.
   * @return A ready-to-use pool or an error code.
   */
  static auto
  create(scheduled_pool_config config = {}) -> result<scheduled_pool>
  {
    return detail::try_result([&config]() -> result<scheduled_pool> { return scheduled_pool(std::move(config)); });
  }

  /**
   * @brief Schedule a one-shot task to run after a delay.
   * @param delay Relative delay before first execution.
   * @param function Callable to execute.
   * @return Task handle or error (for example canceled).
   */
  template <typename Rep, typename Period, typename F>
  auto
  schedule_after(std::chrono::duration<Rep, Period> delay, F&& function) -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<scheduled_task>
          {
            auto native_delay = detail::checked_duration_cast<detail::scheduled_pool_backend::duration>(delay);
            if (!native_delay)
              return unexpected(native_delay.error());
            auto handle = impl_->schedule_after(native_delay.value(), wrap_task(std::forward<F>(function)));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task(std::move(handle));
          });
  }

  /**
   * @brief Schedule a one-shot task at an absolute time point.
   * @param time Absolute @c steady_clock timestamp.
   * @param function Callable to execute.
   */
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

  /**
   * @brief Schedule a periodic task.
   * @param interval Execution interval; must be > 0.
   * @param function Callable to execute each tick.
   * @return Task handle or @c errc::invalid_argument for non-positive interval.
   */
  template <typename Rep, typename Period, typename F>
  auto
  schedule_periodic(std::chrono::duration<Rep, Period> interval, F&& function) -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    auto const native_interval = detail::checked_duration_cast<detail::scheduled_pool_backend::duration>(interval);
    if (!native_interval)
      return unexpected(native_interval.error());
    if (native_interval.value() <= detail::scheduled_pool_backend::duration::zero())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return detail::try_result(
        [&]() -> result<scheduled_task>
          {
            auto handle = impl_->schedule_periodic(native_interval.value(), wrap_task(std::forward<F>(function)));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task(std::move(handle));
          });
  }

  /**
   * @brief Schedule a periodic task with initial delay.
   * @param initial_delay Delay before first execution.
   * @param interval Period between executions; must be > 0.
   * @param function Callable to execute.
   */
  template <typename InitialRep, typename InitialPeriod, typename IntervalRep, typename IntervalPeriod, typename F>
  auto
  schedule_periodic_after(std::chrono::duration<InitialRep, InitialPeriod> initial_delay,
                          std::chrono::duration<IntervalRep, IntervalPeriod> interval, F&& function)
      -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    auto const native_interval = detail::checked_duration_cast<detail::scheduled_pool_backend::duration>(interval);
    if (!native_interval)
      return unexpected(native_interval.error());
    if (native_interval.value() <= detail::scheduled_pool_backend::duration::zero())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    auto const native_delay = detail::checked_duration_cast<detail::scheduled_pool_backend::duration>(initial_delay);
    if (!native_delay)
      return unexpected(native_delay.error());
    return detail::try_result(
        [&]() -> result<scheduled_task>
          {
            auto handle = impl_->schedule_periodic_after(native_delay.value(), native_interval.value(),
                                                         wrap_task(std::forward<F>(function)));
            if (handle.is_cancelled())
              return unexpected(std::make_error_code(std::errc::operation_canceled));
            return scheduled_task(std::move(handle));
          });
  }

  /** @brief Shutdown using configured shutdown policy. */
  auto
  shutdown() -> result<void>
  {
    return shutdown(config_.get_shutdown_policy());
  }

  /**
   * @brief Shutdown with explicit policy.
   * @param policy Drain/cancel behavior for pending tasks.
   */
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

  /** @brief Return count of tasks currently tracked by the scheduler backend. */
  [[nodiscard]] auto
  scheduled_count() const -> std::size_t
  {
    return impl_ ? impl_->scheduled_count() : 0;
  }

private:
  void
  dispose_impl() noexcept
  {
    if (!impl_)
      return;

    auto const policy = detail::to_native(config_.get_shutdown_policy());
    if (!impl_->is_current_context())
      {
        try
          {
            impl_->shutdown(policy);
          }
        catch (...)
          {
          }
        impl_.reset();
        return;
      }

    auto* backend = impl_.release();
    try
      {
        std::thread reaper(
            [backend, policy]() noexcept
              {
                std::unique_ptr<detail::scheduled_pool_backend> owner(backend);
                try
                  {
                    owner->shutdown(policy);
                  }
                catch (...)
                  {
                  }
              });
        reaper.detach();
      }
    catch (...)
      {
        (void)backend;
      }
  }

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
