#pragma once

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
class thread_pool_config
{
public:
  auto
  set_worker_count(worker_count value) noexcept -> thread_pool_config&
  {
    worker_count_ = value;
    return *this;
  }
  auto
  set_registration(worker_registration value) noexcept -> thread_pool_config&
  {
    registration_ = value;
    return *this;
  }
  auto
  set_worker_config(thread_config value) -> thread_pool_config&
  {
    workers_ = std::move(value);
    return *this;
  }
  auto
  set_shutdown_policy(shutdown_policy value) noexcept -> thread_pool_config&
  {
    shutdown_ = value;
    return *this;
  }
  auto
  set_error_callback(error_callback value) -> thread_pool_config&
  {
    on_task_error_ = std::move(value);
    return *this;
  }

  [[nodiscard]] auto
  count() const noexcept -> worker_count
  {
    return worker_count_;
  }
  [[nodiscard]] auto
  registration() const noexcept -> worker_registration
  {
    return registration_;
  }
  [[nodiscard]] auto
  worker_config() const noexcept -> thread_config const&
  {
    return workers_;
  }
  [[nodiscard]] auto
  shutdown() const noexcept -> shutdown_policy
  {
    return shutdown_;
  }
  [[nodiscard]] auto
  on_task_error() const noexcept -> error_callback const&
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

class thread_pool
{
public:
  thread_pool() : thread_pool(thread_pool_config{}) {}

  explicit thread_pool(worker_count count) : thread_pool(thread_pool_config{}.set_worker_count(count)) {}

  explicit thread_pool(thread_pool_config config)
      : config_(std::move(config)),
        impl_(std::make_unique<detail::thread_pool_backend>(
            config_.count().resolve(), config_.registration() == worker_registration::global_registry))
  {
    if (detail::has_thread_configuration(config_.worker_config()))
      {
        auto configured = impl_->configure_threads(detail::to_native(config_.worker_config()));
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
        if (impl_)
          {
            try
              {
                impl_->shutdown(detail::to_native(config_.shutdown()));
              }
            catch (...)
              {
              }
          }
        config_ = std::move(other.config_);
        impl_ = std::move(other.impl_);
      }
    return *this;
  }
  thread_pool(thread_pool const&) = delete;
  auto operator=(thread_pool const&) -> thread_pool& = delete;

  static auto
  create(thread_pool_config config = {}) -> result<thread_pool>
  {
    return detail::try_result([&config]() -> result<thread_pool> { return thread_pool(std::move(config)); });
  }

  ~thread_pool()
  {
    if (!impl_)
      return;
    try
      {
        impl_->shutdown(detail::to_native(config_.shutdown()));
      }
    catch (...)
      {
      }
  }

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
            if (!config_.on_task_error())
              return impl_->try_submit(std::forward<F>(function), std::forward<Args>(args)...);

            auto callback = config_.on_task_error();
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

  template <typename F, typename... Args>
  auto
  submit_or_throw(F&& function, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
  {
    auto submitted = submit(std::forward<F>(function), std::forward<Args>(args)...);
    if (!submitted)
      throw std::system_error(submitted.error(), "thread_pool::submit");
    return std::move(*submitted);
  }

  template <typename F, typename... Args>
  auto
  post(F&& function, Args&&... args) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<void>
          {
            if (!config_.on_task_error())
              return impl_->try_post(std::forward<F>(function), std::forward<Args>(args)...);

            auto callback = config_.on_task_error();
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

  template <typename F, typename... Args>
  void
  post_or_throw(F&& function, Args&&... args)
  {
    auto posted = post(std::forward<F>(function), std::forward<Args>(args)...);
    if (!posted)
      throw std::system_error(posted.error(), "thread_pool::post");
  }

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

  void
  wait_or_throw()
  {
    if (!impl_)
      throw std::system_error(std::make_error_code(std::errc::operation_canceled), "thread_pool::wait");
    impl_->wait_for_tasks();
  }

  auto
  configure_workers(thread_config const& config) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result([&]() -> result<void> { return impl_->configure_threads(detail::to_native(config)); });
  }

  auto
  shutdown(shutdown_policy policy = shutdown_policy::drain) -> result<void>
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

  [[nodiscard]] auto
  size() const noexcept -> std::size_t
  {
    return impl_ ? impl_->size() : 0;
  }

private:
  thread_pool_config config_;
  std::unique_ptr<detail::thread_pool_backend> impl_;
};

} // namespace threadschedule
