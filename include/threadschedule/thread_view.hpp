#pragma once

#include "detail/thread/control.hpp"
#include "detail/thread_backend.hpp"
#include "thread_config.hpp"

#include <cstdint>
#include <string>
#include <thread>

namespace threadschedule
{
class thread_view
{
public:
  explicit thread_view(std::thread& value) noexcept : impl_(value) {}
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

private:
  detail::thread_view_backend impl_;
};

} // namespace threadschedule
