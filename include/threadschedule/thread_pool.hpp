#pragma once

/**
 * @file thread_pool.hpp
 * @brief Fixed-size worker pool for asynchronous task execution.
 */

#include "detail/pool/backend.hpp"
#include "detail/pool/shutdown.hpp"
#include "detail/thread/control.hpp"
#include "shutdown_policy.hpp"
#include "task_error.hpp"
#include "thread_config.hpp"
#include "worker_count.hpp"
#include "worker_registration.hpp"

#include <cstddef>
#include <future>
#include <memory>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

namespace threadschedule
{
/**
 * @brief Builder-style configuration for @ref thread_pool.
 */
class thread_pool_config
{
public:
  /** @brief Set number of worker threads. */
  auto
  set_worker_count(worker_count value) noexcept -> thread_pool_config&
  {
    worker_count_ = value;
    return *this;
  }
  /** @brief Configure registry registration behavior for workers. */
  auto
  set_registration(worker_registration value) noexcept -> thread_pool_config&
  {
    registration_ = value;
    return *this;
  }
  /** @brief Set startup configuration applied to worker threads. */
  auto
  set_worker_config(thread_config value) -> thread_pool_config&
  {
    workers_ = std::move(value);
    return *this;
  }
  /** @brief Set shutdown behavior used by @ref thread_pool::shutdown and destructor. */
  auto
  set_shutdown_policy(shutdown_policy value) noexcept -> thread_pool_config&
  {
    shutdown_ = value;
    return *this;
  }
  /**
   * @brief Set callback invoked when posted/submitted work throws.
   * @param value Error callback receiving captured @ref task_error.
   */
  auto
  set_error_callback(error_callback value) -> thread_pool_config&
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
  /** @brief Return worker thread configuration. */
  [[nodiscard]] auto
  get_worker_config() const noexcept -> thread_config const&
  {
    return workers_;
  }
  /** @brief Return configured shutdown policy. */
  [[nodiscard]] auto
  get_shutdown_policy() const noexcept -> shutdown_policy
  {
    return shutdown_;
  }
  /** @brief Return configured asynchronous task error callback. */
  [[nodiscard]] auto
  get_error_callback() const noexcept -> error_callback const&
  {
    return on_task_error_;
  }

private:
  worker_count worker_count_{ worker_count::automatic() };
  worker_registration registration_{ worker_registration::disabled };
  thread_config workers_;
  shutdown_policy shutdown_{ shutdown_policy::drain };
  error_callback on_task_error_;
};

/**
 * @brief Fixed-size thread pool for queued asynchronous work.
 */
class thread_pool
{
public:
  /** @brief Construct a pool with default configuration. */
  thread_pool() : thread_pool(thread_pool_config{}) {}

  /**
   * @brief Construct a pool with explicit worker count.
   * @param count Number of workers or automatic resolution mode.
   */
  explicit thread_pool(worker_count count) : thread_pool(thread_pool_config{}.set_worker_count(count)) {}

  /**
   * @brief Construct a pool from full configuration.
   * @param config Pool configuration.
   * @throws std::system_error if worker configuration cannot be applied.
   */
  explicit thread_pool(thread_pool_config config)
      : config_(std::move(config)),
        impl_(std::make_unique<detail::thread_pool_backend>(
            config_.get_worker_count().resolve(), config_.get_registration() == worker_registration::global_registry))
  {
    if (detail::has_thread_configuration(config_.get_worker_config()))
      {
        auto configured = impl_->configure_threads(detail::to_native(config_.get_worker_config()));
        if (!configured)
          throw std::system_error(configured.error(), "thread_pool worker configuration");
      }
  }

  thread_pool(thread_pool&&) noexcept = default;
  auto
  operator=(thread_pool&& other) noexcept -> thread_pool&
  {
    if (this != &other)
      {
        dispose_impl();
        config_ = std::move(other.config_);
        impl_ = std::move(other.impl_);
      }
    return *this;
  }
  thread_pool(thread_pool const&) = delete;
  auto operator=(thread_pool const&) -> thread_pool& = delete;

  /**
   * @brief Create a pool without throwing.
   * @param config Pool configuration.
   * @return A ready-to-use pool or a translated error code.
   */
  static auto
  create(thread_pool_config config = {}) -> result<thread_pool>
  {
    return detail::try_result([&config]() -> result<thread_pool> { return thread_pool(std::move(config)); });
  }

  /**
   * @brief Shutdown the pool according to configured policy.
   *
   * Destructor never throws and will attempt best-effort shutdown.
   */
  ~thread_pool()
  {
    dispose_impl();
  }

  /**
   * @brief Submit work and receive a future for its result.
   * @param function Callable to execute on a worker.
   * @param args Arguments forwarded to the callable.
   * @return A future on success, or an error (for example canceled/queue rejection).
   */
  template <typename F, typename... Args>
  auto
  submit(F&& function, Args&&... args) -> result<std::future<std::invoke_result_t<F, Args...>>>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<std::future<std::invoke_result_t<F, Args...>>>
          {
            using return_type = std::invoke_result_t<F, Args...>;
            if (!config_.get_error_callback())
              return impl_->try_submit(std::forward<F>(function), std::forward<Args>(args)...);

            auto callback = config_.get_error_callback();
            auto wrapped = [bound = detail::bind_args(std::forward<F>(function), std::forward<Args>(args)...),
                            callback = std::move(callback)]() mutable -> return_type
              {
                try
                  {
                    if constexpr (std::is_void_v<return_type>)
                      {
                        bound();
                        return;
                      }
                    else
                      {
                        return bound();
                      }
                  }
                catch (...)
                  {
                    auto original = std::current_exception();
                    try
                      {
                        callback(task_error::capture());
                      }
                    catch (...)
                      {
                      }
                    std::rethrow_exception(original);
                  }
              };
            return impl_->try_submit(std::move(wrapped));
          });
  }

  /**
   * @brief Throwing counterpart to @ref submit.
   * @throws std::system_error if submission fails.
   */
  template <typename F, typename... Args>
  auto
  submit_or_throw(F&& function, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
  {
    auto submitted = submit(std::forward<F>(function), std::forward<Args>(args)...);
    if (!submitted)
      throw std::system_error(submitted.error(), "thread_pool::submit");
    return std::move(*submitted);
  }

  /**
   * @brief Post fire-and-forget work to the pool.
   * @param function Callable to execute.
   * @param args Arguments forwarded to the callable.
   * @return Success or error code.
   */
  template <typename F, typename... Args>
  auto
  post(F&& function, Args&&... args) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<void>
          {
            if (!config_.get_error_callback())
              return impl_->try_post(std::forward<F>(function), std::forward<Args>(args)...);

            auto callback = config_.get_error_callback();
            auto wrapped = [bound = detail::bind_args(std::forward<F>(function), std::forward<Args>(args)...),
                            callback = std::move(callback)]() mutable
              {
                try
                  {
                    bound();
                  }
                catch (...)
                  {
                    try
                      {
                        callback(task_error::capture());
                      }
                    catch (...)
                      {
                      }
                  }
              };
            return impl_->try_post(std::move(wrapped));
          });
  }

  /**
   * @brief Throwing counterpart to @ref post.
   * @throws std::system_error if posting fails.
   */
  template <typename F, typename... Args>
  void
  post_or_throw(F&& function, Args&&... args)
  {
    auto posted = post(std::forward<F>(function), std::forward<Args>(args)...);
    if (!posted)
      throw std::system_error(posted.error(), "thread_pool::post");
  }

  /**
   * @brief Wait until all queued/running tasks are finished.
   */
  auto
  wait() -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<void>
          {
            impl_->wait_for_tasks();
            return {};
          });
  }

  /**
   * @brief Throwing counterpart to @ref wait.
   */
  void
  wait_or_throw()
  {
    if (!impl_)
      throw std::system_error(std::make_error_code(std::errc::operation_canceled), "thread_pool::wait");
    impl_->wait_for_tasks();
  }

  /**
   * @brief Apply thread configuration to all workers.
   * @param config Portable configuration to apply.
   */
  auto
  configure_workers(thread_config const& config) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result([&]() -> result<void> { return impl_->configure_threads(detail::to_native(config)); });
  }

  /**
   * @brief Shutdown using configured policy.
   */
  auto
  shutdown() -> result<void>
  {
    return shutdown(config_.get_shutdown_policy());
  }

  /**
   * @brief Shutdown with explicit policy.
   * @param policy Drain/cancel behavior used for pending tasks.
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
            return {};
          });
  }

  /** @brief Return current worker count (0 if moved-from/shutdown backend). */
  [[nodiscard]] auto
  size() const noexcept -> std::size_t
  {
    return impl_ ? impl_->size() : 0;
  }

private:
  void
  dispose_impl() noexcept
  {
    if (!impl_)
      return;

    auto const policy = detail::to_native(config_.get_shutdown_policy());
    if (!impl_->is_current_worker())
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

    // The backend remains in use until the current task returns. Transfer its
    // ownership to a reaper thread, which can safely join every worker.
    auto* backend = impl_.release();
    try
      {
        std::thread reaper(
            [backend, policy]() noexcept
              {
                std::unique_ptr<detail::thread_pool_backend> owner(backend);
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
        // No thread can safely reclaim a backend that is executing this
        // destructor. Keeping it alive is the only non-terminating fallback.
        (void)backend;
      }
  }

  thread_pool_config config_;
  std::unique_ptr<detail::thread_pool_backend> impl_;
};

} // namespace threadschedule
