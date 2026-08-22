#pragma once

/**
 * @file thread.hpp
 * @brief Owning std::thread wrapper with portable configuration helpers.
 */

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

/**
 * @brief Owning thread wrapper with result-based lifecycle/configuration API.
 */
class thread
{
public:
  /** @brief Construct an empty, non-joinable thread object. */
  thread() = default;
  /**
   * @brief Take ownership of an existing std::thread.
   *
   * @note On Linux, nice control reports @c operation_not_supported because
   *       an external thread's kernel TID cannot be recovered portably.
   */
  explicit thread(std::thread&& value) noexcept : impl_(std::move(value)) {}
  /**
   * @brief Start a thread from callable and arguments.
   * @param function Callable entry point.
   * @param args Arguments forwarded to @p function.
   */
  template <typename F, typename... Args,
            std::enable_if_t<
                !std::is_same_v<std::decay_t<F>, thread> && !std::is_same_v<std::decay_t<F>, thread_config>, int> = 0>
  explicit thread(F&& function, Args&&... args) : impl_(std::forward<F>(function), std::forward<Args>(args)...)
  {
  }

  /**
   * @brief Start a thread and apply @ref thread_config before user code runs.
   * @param config Portable thread configuration.
   * @param function Callable entry point.
   * @param args Arguments forwarded to @p function.
   */
  template <typename F, typename... Args>
  thread(thread_config const& config, F&& function, Args&&... args)
      : impl_(make_configured_impl(config, std::forward<F>(function), std::forward<Args>(args)...))
  {
  }

  thread(thread&&) noexcept = default;
  auto operator=(thread&&) noexcept -> thread& = default;
  thread(thread const&) = delete;
  auto operator=(thread const&) -> thread& = delete;

  /**
   * @brief Create thread without throwing.
   * @param function Callable entry point.
   * @param args Arguments forwarded to @p function.
   */
  template <typename F, typename... Args>
  static auto
  create(F&& function, Args&&... args)
      -> std::enable_if_t<!std::is_same_v<std::decay_t<F>, thread_config>, result<thread>>
  {
    return detail::try_result([&]() -> result<thread>
                                { return thread(std::forward<F>(function), std::forward<Args>(args)...); });
  }

  /**
   * @brief Create configured thread without throwing.
   * @param config Portable thread configuration.
   * @param function Callable entry point.
   * @param args Arguments forwarded to @p function.
   */
  template <typename F, typename... Args>
  static auto
  create(thread_config const& config, F&& function, Args&&... args) -> result<thread>
  {
    return detail::try_result([&]() -> result<thread>
                                { return thread(config, std::forward<F>(function), std::forward<Args>(args)...); });
  }

  /** @brief Join the thread. */
  auto
  join() -> result<void>
  {
    return detail::thread_lifecycle::join(impl_);
  }

  /** @brief Throwing counterpart to @ref join. */
  void
  join_or_throw()
  {
    detail::thread_lifecycle::join_or_throw(impl_, "thread::join");
  }

  /** @brief Detach the thread. */
  auto
  detach() -> result<void>
  {
    return detail::thread_lifecycle::detach(impl_);
  }

  /** @brief Throwing counterpart to @ref detach. */
  void
  detach_or_throw()
  {
    detail::thread_lifecycle::detach_or_throw(impl_, "thread::detach");
  }

  /** @brief Return whether the thread object owns a running thread. */
  [[nodiscard]] auto
  joinable() const noexcept -> bool
  {
    return impl_.joinable();
  }

  /** @brief Return std::thread id of the owned thread. */
  [[nodiscard]] auto
  get_id() const noexcept -> std::thread::id
  {
    return impl_.get_id();
  }

  /** @brief Forward std::thread::hardware_concurrency(). */
  [[nodiscard]] static auto
  hardware_concurrency() noexcept -> unsigned
  {
    return std::thread::hardware_concurrency();
  }

  /** @brief Apply a full portable thread configuration to the running thread. */
  auto
  configure(thread_config const& config) -> result<void>
  {
    return detail::portable_thread_control::configure(impl_, config);
  }

  /** @brief Set portable priority preset. */
  auto
  set_priority(priority_level level) -> result<void>
  {
    return detail::portable_thread_control::set_priority(impl_, level);
  }

  /** @brief Set explicit nice value. */
  auto
  set_nice(nice_value value) -> result<void>
  {
    return detail::portable_thread_control::set_nice(impl_, value);
  }

  /** @brief Query current portable priority preset. */
  [[nodiscard]] auto
  get_priority() const -> result<priority_level>
  {
    return detail::portable_thread_control::get_priority(impl_.get_nice_value());
  }

  /** @brief Query current nice value. */
  [[nodiscard]] auto
  get_nice() const -> result<nice_value>
  {
    return detail::portable_thread_control::get_nice(impl_.get_nice_value());
  }

  /** @brief Set thread name. */
  auto
  set_name(std::string const& name) -> result<void>
  {
    return impl_.set_name(name);
  }

  /** @brief Query thread name if supported by platform/backend. */
  [[nodiscard]] auto
  get_name() const -> result<std::string>
  {
    return impl_.get_name();
  }

  /** @brief Apply CPU affinity set. */
  auto
  set_affinity(thread_affinity const& affinity) -> result<void>
  {
    return detail::portable_thread_control::set_affinity(impl_, affinity);
  }

  /** @brief Query current CPU affinity. */
  [[nodiscard]] auto
  get_affinity() const -> result<thread_affinity>
  {
    return detail::portable_thread_control::get_affinity(impl_.get_affinity());
  }

  /** @brief Release ownership and return underlying std::thread. */
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
