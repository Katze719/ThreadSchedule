#pragma once

#include "detail/thread/control.hpp"
#include "detail/thread_backend.hpp"
#include "jthread.hpp"
#include "thread.hpp"
#include "thread_config.hpp"

#include <string>
#include <thread>
#include <utility>
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
#  include <variant>
#endif

namespace threadschedule
{

class thread_view
{
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
  using std_thread_view = detail::thread_view_backend;
  using jthread_view = detail::basic_thread_backend<std::jthread, detail::non_owning_tag>;

  template <typename Function>
  decltype(auto)
  with_impl(Function&& function)
  {
    return std::visit(std::forward<Function>(function), impl_);
  }

  template <typename Function>
  decltype(auto)
  with_impl(Function&& function) const
  {
    return std::visit(std::forward<Function>(function), impl_);
  }

  std::variant<std_thread_view, jthread_view> impl_;
#else
  template <typename Function>
  decltype(auto)
  with_impl(Function&& function)
  {
    return std::forward<Function>(function)(impl_);
  }

  template <typename Function>
  decltype(auto)
  with_impl(Function&& function) const
  {
    return std::forward<Function>(function)(impl_);
  }

  detail::thread_view_backend impl_;
#endif

public:
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
  explicit thread_view(std::thread& value) noexcept : impl_(std::in_place_type<std_thread_view>, value) {}
  explicit thread_view(thread& value) noexcept
      : impl_(std::in_place_type<std_thread_view>, value.impl_.get(), value.impl_.native_id())
  {
  }
  explicit thread_view(std::jthread& value) noexcept : impl_(std::in_place_type<jthread_view>, value) {}
  explicit thread_view(jthread& value) noexcept : impl_(std::in_place_type<jthread_view>, value.impl_, value.native_id_)
  {
  }
#else
  explicit thread_view(std::thread& value) noexcept : impl_(value) {}
  explicit thread_view(thread& value) noexcept : impl_(value.impl_.get(), value.impl_.native_id()) {}
#endif

  [[nodiscard]] auto
  joinable() const noexcept -> bool
  {
    return with_impl([](auto const& value) { return value.joinable(); });
  }

  [[nodiscard]] auto
  get_id() const noexcept -> std::thread::id
  {
    return with_impl([](auto const& value) { return value.get_id(); });
  }

  auto
  configure(thread_config const& config) -> result<void>
  {
    return with_impl([&](auto& value) { return detail::portable_thread_control::configure(value, config); });
  }

  auto
  set_priority(priority_level level) -> result<void>
  {
    return with_impl([&](auto& value) { return detail::portable_thread_control::set_priority(value, level); });
  }

  auto
  set_nice(nice_value value) -> result<void>
  {
    return with_impl([&](auto& impl) { return detail::portable_thread_control::set_nice(impl, value); });
  }

  [[nodiscard]] auto
  get_priority() const -> result<priority_level>
  {
    return with_impl([](auto const& value)
                       { return detail::portable_thread_control::get_priority(value.get_nice_value()); });
  }

  [[nodiscard]] auto
  get_nice() const -> result<nice_value>
  {
    return with_impl([](auto const& value)
                       { return detail::portable_thread_control::get_nice(value.get_nice_value()); });
  }

  auto
  set_name(std::string const& name) -> result<void>
  {
    return with_impl([&](auto& value) { return value.set_name(name); });
  }

  [[nodiscard]] auto
  get_name() const -> result<std::string>
  {
    return with_impl([](auto const& value) { return value.get_name(); });
  }

  auto
  set_affinity(thread_affinity const& affinity) -> result<void>
  {
    return with_impl([&](auto& value) { return detail::portable_thread_control::set_affinity(value, affinity); });
  }

  [[nodiscard]] auto
  get_affinity() const -> result<thread_affinity>
  {
    return with_impl([](auto const& value)
                       { return detail::portable_thread_control::get_affinity(value.get_affinity()); });
  }
};

} // namespace threadschedule
