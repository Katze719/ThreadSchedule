#pragma once

#include "../../shutdown_policy.hpp"
#include "../../task_error.hpp"
#include "../../thread_config.hpp"
#include "../thread/control.hpp"
#include "backend.hpp"
#include "shutdown.hpp"

#include <cstddef>
#include <future>
#include <memory>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

namespace threadschedule
{
struct thread_pool_config
{
  std::size_t worker_count{ std::thread::hardware_concurrency() };
  bool register_workers{ false };
  thread_config workers{};
  shutdown_policy shutdown{ shutdown_policy::drain };
  error_callback on_task_error{};
};

class thread_pool
{
public:
  thread_pool() : thread_pool(thread_pool_config{}) {}

  explicit thread_pool(std::size_t worker_count) : thread_pool(thread_pool_config{ worker_count }) {}

  explicit thread_pool(thread_pool_config config)
      : config_(std::move(config)),
        impl_(std::make_unique<detail::thread_pool_backend>(config_.worker_count, config_.register_workers))
  {
    if (detail::has_thread_configuration(config_.workers))
      {
        auto configured = impl_->configure_threads(detail::to_native(config_.workers));
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
                impl_->shutdown(detail::to_native(config_.shutdown));
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
    try
      {
        return thread_pool(std::move(config));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  ~thread_pool()
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

  template <typename F, typename... Args>
  auto
  submit(F&& function, Args&&... args) -> result<std::future<std::invoke_result_t<F, Args...>>>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    try
      {
        using return_type = std::invoke_result_t<F, Args...>;
        if (!config_.on_task_error)
          return impl_->try_submit(std::forward<F>(function), std::forward<Args>(args)...);

        auto callback = config_.on_task_error;
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
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
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
    try
      {
        if (!config_.on_task_error)
          return impl_->try_post(std::forward<F>(function), std::forward<Args>(args)...);

        auto callback = config_.on_task_error;
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
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
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
    try
      {
        impl_->wait_for_tasks();
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
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
    try
      {
        return impl_->configure_threads(detail::to_native(config));
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
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
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
