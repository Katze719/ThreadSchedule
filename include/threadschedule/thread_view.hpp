#pragma once

/**
 * @file thread_view.hpp
 * @brief Non-owning adapter for thread control/query operations.
 */

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

/**
 * @brief Non-owning view over a running thread or jthread.
 *
 * This class never joins/detaches and only forwards control/query operations.
 */
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
  /** @brief Create a view over an existing std::thread. */
  explicit thread_view(std::thread& value) noexcept : impl_(std::in_place_type<std_thread_view>, value) {}
  /** @brief Create a view over a threadschedule::thread. */
  explicit thread_view(thread& value) noexcept
      : impl_(std::in_place_type<std_thread_view>, value.impl_.get(), value.impl_.native_id())
  {
  }
  /** @brief Create a view over an existing std::jthread. */
  explicit thread_view(std::jthread& value) noexcept : impl_(std::in_place_type<jthread_view>, value) {}
  /** @brief Create a view over a threadschedule::jthread. */
  explicit thread_view(jthread& value) noexcept : impl_(std::in_place_type<jthread_view>, value.impl_, value.native_id_)
  {
  }
#else
  /** @brief Create a view over an existing std::thread. */
  explicit thread_view(std::thread& value) noexcept : impl_(value) {}
  /** @brief Create a view over a threadschedule::thread. */
  explicit thread_view(thread& value) noexcept : impl_(value.impl_.get(), value.impl_.native_id()) {}
#endif

  /** @brief Return whether viewed thread is joinable. */
  [[nodiscard]] auto
  joinable() const noexcept -> bool
  {
    return with_impl([](auto const& value) { return value.joinable(); });
  }

  /** @brief Return std::thread id of viewed thread. */
  [[nodiscard]] auto
  get_id() const noexcept -> std::thread::id
  {
    return with_impl([](auto const& value) { return value.get_id(); });
  }

  /** @brief Apply full portable configuration to viewed thread. */
  auto
  configure(thread_config const& config) -> result<void>
  {
    return with_impl([&](auto& value) { return detail::portable_thread_control::configure(value, config); });
  }

  /** @brief Set portable priority preset on viewed thread. */
  auto
  set_priority(priority_level level) -> result<void>
  {
    return with_impl([&](auto& value) { return detail::portable_thread_control::set_priority(value, level); });
  }

  /** @brief Set explicit nice value on viewed thread. */
  auto
  set_nice(nice_value value) -> result<void>
  {
    return with_impl([&](auto& impl) { return detail::portable_thread_control::set_nice(impl, value); });
  }

  /** @brief Query portable priority preset of viewed thread. */
  [[nodiscard]] auto
  get_priority() const -> result<priority_level>
  {
    return with_impl([](auto const& value)
                       { return detail::portable_thread_control::get_priority(value.get_nice_value()); });
  }

  /** @brief Query nice value of viewed thread. */
  [[nodiscard]] auto
  get_nice() const -> result<nice_value>
  {
    return with_impl([](auto const& value)
                       { return detail::portable_thread_control::get_nice(value.get_nice_value()); });
  }

  /** @brief Set viewed thread name. */
  auto
  set_name(std::string const& name) -> result<void>
  {
    return with_impl([&](auto& value) { return value.set_name(name); });
  }

  /** @brief Query viewed thread name. */
  [[nodiscard]] auto
  get_name() const -> result<std::string>
  {
    return with_impl([](auto const& value) { return value.get_name(); });
  }

  /** @brief Set viewed thread CPU affinity. */
  auto
  set_affinity(thread_affinity const& affinity) -> result<void>
  {
    return with_impl([&](auto& value) { return detail::portable_thread_control::set_affinity(value, affinity); });
  }

  /** @brief Query viewed thread CPU affinity. */
  [[nodiscard]] auto
  get_affinity() const -> result<thread_affinity>
  {
    return with_impl([](auto const& value)
                       { return detail::portable_thread_control::get_affinity(value.get_affinity()); });
  }
};

} // namespace threadschedule
