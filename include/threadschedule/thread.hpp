#pragma once

#include "detail/callable/bind.hpp"
#include "detail/scope_exit.hpp"
#include "detail/thread/control.hpp"
#include "detail/thread_backend.hpp"
#include "thread_config.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace threadschedule
{
class thread_view;

class thread
{
public:
  thread() = default;
  explicit thread(std::thread&& value) noexcept : impl_(std::move(value)) {}
  template <typename F, typename... Args,
            std::enable_if_t<
                !std::is_same_v<std::decay_t<F>, thread> && !std::is_same_v<std::decay_t<F>, thread_config>, int> = 0>
  explicit thread(F&& function, Args&&... args) : impl_(std::forward<F>(function), std::forward<Args>(args)...)
  {
  }

  template <typename F, typename... Args>
  thread(thread_config const& config, F&& function, Args&&... args)
      : impl_(make_configured_impl(config, std::forward<F>(function), std::forward<Args>(args)...))
  {
  }

  thread(thread&&) noexcept = default;
  auto operator=(thread&&) noexcept -> thread& = default;
  thread(thread const&) = delete;
  auto operator=(thread const&) -> thread& = delete;

  template <typename F, typename... Args>
  static auto
  create(F&& function, Args&&... args)
      -> std::enable_if_t<!std::is_same_v<std::decay_t<F>, thread_config>, result<thread>>
  {
    return detail::try_result([&]() -> result<thread>
                                { return thread(std::forward<F>(function), std::forward<Args>(args)...); });
  }

  template <typename F, typename... Args>
  static auto
  create(thread_config const& config, F&& function, Args&&... args) -> result<thread>
  {
    return detail::try_result([&]() -> result<thread>
                                { return thread(config, std::forward<F>(function), std::forward<Args>(args)...); });
  }

  auto
  join() -> result<void>
  {
    return detail::thread_lifecycle::join(impl_);
  }

  void
  join_or_throw()
  {
    detail::thread_lifecycle::join_or_throw(impl_, "thread::join");
  }

  auto
  detach() -> result<void>
  {
    return detail::thread_lifecycle::detach(impl_);
  }

  void
  detach_or_throw()
  {
    detail::thread_lifecycle::detach_or_throw(impl_, "thread::detach");
  }

  [[nodiscard]] auto
  joinable() const noexcept -> bool
  {
    return impl_.joinable();
  }

  [[nodiscard]] auto
  get_id() const noexcept -> std::thread::id
  {
    return impl_.get_id();
  }

  [[nodiscard]] static auto
  hardware_concurrency() noexcept -> unsigned
  {
    return std::thread::hardware_concurrency();
  }

  auto
  configure(thread_config const& config) -> result<void>
  {
    return detail::portable_thread_control::configure(impl_, config);
  }

  auto
  set_priority(priority_level level) -> result<void>
  {
    return detail::portable_thread_control::set_priority(impl_, level);
  }

  auto
  set_nice(nice_value value) -> result<void>
  {
    return detail::portable_thread_control::set_nice(impl_, value);
  }

  [[nodiscard]] auto
  get_priority() const -> result<priority_level>
  {
    return detail::portable_thread_control::get_priority(impl_.get_nice_value());
  }

  [[nodiscard]] auto
  get_nice() const -> result<nice_value>
  {
    return detail::portable_thread_control::get_nice(impl_.get_nice_value());
  }

  auto
  set_name(std::string const& name) -> result<void>
  {
    return impl_.set_name(name);
  }

  [[nodiscard]] auto
  get_name() const -> result<std::string>
  {
    return impl_.get_name();
  }

  auto
  set_affinity(thread_affinity const& affinity) -> result<void>
  {
    return detail::portable_thread_control::set_affinity(impl_, affinity);
  }

  [[nodiscard]] auto
  get_affinity() const -> result<thread_affinity>
  {
    return detail::portable_thread_control::get_affinity(impl_.get_affinity());
  }

  [[nodiscard]] auto
  release() noexcept -> std::thread
  {
    return impl_.release();
  }

private:
  friend struct detail::native_thread_access;
  friend class thread_view;

  template <typename F, typename... Args>
  static auto
  make_configured_impl(thread_config const& config, F&& function, Args&&... args) -> detail::thread_backend
  {
    auto gate = std::make_shared<detail::thread_start_gate>();
    detail::thread_backend value(
        [gate, callable = detail::bind_args(std::forward<F>(function), std::forward<Args>(args)...)]() mutable
          {
            if (gate->wait())
              callable();
          });

    auto rollback = detail::make_scope_exit(
        [&]() noexcept
          {
            gate->release(false);
            try
              {
                if (value.joinable())
                  value.join();
              }
            catch (...)
              {
              }
          });
    auto configured = value.configure(detail::to_native(config));
    if (!configured)
      {
        throw std::system_error(configured.error(), "thread configuration");
      }
    gate->release(true);
    rollback.release();
    return value;
  }

  detail::thread_backend impl_;
};

} // namespace threadschedule
