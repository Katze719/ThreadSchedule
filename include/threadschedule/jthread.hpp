#pragma once

#include "detail/scope_exit.hpp"
#include "detail/thread/control.hpp"
#include "detail/thread_backend.hpp"
#include "thread_config.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace threadschedule
{
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
class jthread
{
public:
  jthread() noexcept = default;
  explicit jthread(std::jthread&& value) noexcept : impl_(std::move(value)) {}
  template <
      typename F, typename... Args,
      std::enable_if_t<!std::is_same_v<std::decay_t<F>, jthread> && !std::is_same_v<std::decay_t<F>, thread_config>
                           && std::is_constructible_v<std::jthread, F, Args...>,
                       int> = 0>
  explicit jthread(F&& function, Args&&... args)
      : impl_(make_impl(native_id_, std::forward<F>(function), std::forward<Args>(args)...))
  {
  }

  template <typename F, typename... Args>
  jthread(thread_config const& config, F&& function, Args&&... args)
      : impl_(make_configured_impl(config, native_id_, std::forward<F>(function), std::forward<Args>(args)...))
  {
  }

  jthread(jthread&&) noexcept = default;
  auto operator=(jthread&&) noexcept -> jthread& = default;
  jthread(jthread const&) = delete;
  auto operator=(jthread const&) -> jthread& = delete;

  template <typename F, typename... Args>
  static auto
  create(F&& function, Args&&... args)
      -> std::enable_if_t<!std::is_same_v<std::decay_t<F>, thread_config>, result<jthread>>
  {
    return detail::try_result([&]() -> result<jthread>
                                { return jthread(std::forward<F>(function), std::forward<Args>(args)...); });
  }

  template <typename F, typename... Args>
  static auto
  create(thread_config const& config, F&& function, Args&&... args) -> result<jthread>
  {
    return detail::try_result([&]() -> result<jthread>
                                { return jthread(config, std::forward<F>(function), std::forward<Args>(args)...); });
  }

  auto
  join() -> result<void>
  {
    return detail::thread_lifecycle::join(impl_);
  }

  void
  join_or_throw()
  {
    detail::thread_lifecycle::join_or_throw(impl_, "jthread::join");
  }

  auto
  detach() -> result<void>
  {
    return detail::thread_lifecycle::detach(impl_);
  }

  void
  detach_or_throw()
  {
    detail::thread_lifecycle::detach_or_throw(impl_, "jthread::detach");
  }

  [[nodiscard]] auto
  joinable() const noexcept -> bool
  {
    return impl_.joinable();
  }

  [[nodiscard]] auto
  get_id() const noexcept -> std::jthread::id
  {
    return impl_.get_id();
  }

  [[nodiscard]] auto
  request_stop() noexcept -> bool
  {
    return impl_.request_stop();
  }

  [[nodiscard]] auto
  stop_requested() const noexcept -> bool
  {
    return impl_.get_stop_token().stop_requested();
  }

  [[nodiscard]] auto
  get_stop_token() const noexcept -> std::stop_token
  {
    return impl_.get_stop_token();
  }

  [[nodiscard]] auto
  get_stop_source() noexcept -> std::stop_source
  {
    return impl_.get_stop_source();
  }

  auto
  configure(thread_config const& config) -> result<void>
  {
    auto view = native_view();
    return detail::portable_thread_control::configure(view, config);
  }

  auto
  set_priority(priority_level level) -> result<void>
  {
    auto view = native_view();
    return detail::portable_thread_control::set_priority(view, level);
  }

  auto
  set_nice(nice_value value) -> result<void>
  {
    auto view = native_view();
    return detail::portable_thread_control::set_nice(view, value);
  }

  [[nodiscard]] auto
  get_priority() const -> result<priority_level>
  {
    auto view = native_view();
    return detail::portable_thread_control::get_priority(view.get_nice_value());
  }

  auto
  set_name(std::string const& name) -> result<void>
  {
    auto view = native_view();
    return view.set_name(name);
  }

  [[nodiscard]] auto
  get_name() const -> result<std::string>
  {
    auto view = native_view();
    return view.get_name();
  }

  auto
  set_affinity(thread_affinity const& affinity) -> result<void>
  {
    auto view = native_view();
    return detail::portable_thread_control::set_affinity(view, affinity);
  }

  [[nodiscard]] auto
  get_affinity() const -> result<thread_affinity>
  {
    auto view = native_view();
    return detail::portable_thread_control::get_affinity(view.get_affinity());
  }

  [[nodiscard]] auto
  release() noexcept -> std::jthread
  {
    auto value = std::move(impl_);
    native_id_ = {};
    return value;
  }

private:
  friend struct detail::native_thread_access;

  using native_view_type = detail::basic_thread_backend<std::jthread, detail::non_owning_tag>;

  [[nodiscard]] auto
  native_view() const -> native_view_type
  {
    return native_view_type(const_cast<std::jthread&>(impl_), native_id_);
  }

  template <typename F, typename... Args>
  static auto
  make_impl(detail::native_thread_id& native_id, F&& function, Args&&... args) -> std::jthread
  {
    using function_type = std::decay_t<F>;
    auto identity = std::make_shared<detail::thread_identity_state>();
    std::jthread value(
        [identity, callable = function_type(std::forward<F>(function)),
         arguments = std::make_tuple(std::forward<Args>(args)...)](std::stop_token token) mutable
          {
            identity->publish(detail::current_native_thread_id());
            detail::invoke_jthread_callable(callable, std::move(arguments), std::move(token));
          });
    native_id = identity->wait();
    return value;
  }

  template <typename F, typename... Args>
  static auto
  make_configured_impl(thread_config const& config, detail::native_thread_id& native_id, F&& function, Args&&... args)
      -> std::jthread
  {
    using function_type = std::decay_t<F>;
    auto gate = std::make_shared<detail::thread_start_gate>();
    auto identity = std::make_shared<detail::thread_identity_state>();
    std::jthread value(
        [gate, identity, callable = function_type(std::forward<F>(function)),
         arguments = std::make_tuple(std::forward<Args>(args)...)](std::stop_token token) mutable
          {
            identity->publish(detail::current_native_thread_id());
            if (!gate->wait())
              return;
            detail::invoke_jthread_callable(callable, std::move(arguments), std::move(token));
          });

    native_id = identity->wait();
    auto rollback = detail::make_scope_exit([&]() noexcept { gate->release(false); });
    native_view_type view(value, native_id);
    auto configured = view.configure(detail::to_native(config));
    if (!configured)
      {
        throw std::system_error(configured.error(), "jthread configuration");
      }
    gate->release(true);
    rollback.release();
    return value;
  }

  detail::native_thread_id native_id_{};
  std::jthread impl_;
};
#endif

} // namespace threadschedule
