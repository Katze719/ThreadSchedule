#pragma once

/**
 * @file core.hpp
 * @brief ThreadSchedule 3.0's C++17 core API and optional C++20 jthread.
 */

#include "detail/thread_backend.hpp"
#include "expected.hpp"
#include "inline_pool.hpp"
#include "scheduled_pool.hpp"
#include "scheduler_policy.hpp"
#include "thread_pool.hpp"
#include "thread_registry.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace threadschedule
{

template <typename T>
using result = expected<T, std::error_code>;

enum class scheduling_intent : std::uint8_t
{
  background,
  normal,
  interactive,
  low_latency,
  realtime_fifo,
  realtime_round_robin,
  nice
};

/** @brief Portable non-realtime priority levels. */
enum class priority_level : std::int8_t
{
  lowest = 19,
  low = 5,
  normal = 0,
  high = -5,
  highest = -20
};

struct scheduling_config
{
  scheduling_intent intent{ scheduling_intent::normal };
  int priority{ 0 };
};

namespace schedule
{
[[nodiscard]] constexpr auto
background() noexcept -> scheduling_config
{
  return { scheduling_intent::background, 0 };
}

[[nodiscard]] constexpr auto
normal() noexcept -> scheduling_config
{
  return { scheduling_intent::normal, 0 };
}

[[nodiscard]] constexpr auto
interactive() noexcept -> scheduling_config
{
  return { scheduling_intent::interactive, 0 };
}

[[nodiscard]] constexpr auto
low_latency() noexcept -> scheduling_config
{
  return { scheduling_intent::low_latency, 0 };
}

[[nodiscard]] constexpr auto
realtime_fifo(int priority = 80) noexcept -> scheduling_config
{
  return { scheduling_intent::realtime_fifo, priority };
}

[[nodiscard]] constexpr auto
realtime_rr(int priority = 80) noexcept -> scheduling_config
{
  return { scheduling_intent::realtime_round_robin, priority };
}

[[nodiscard]] constexpr auto
nice(int value) noexcept -> scheduling_config
{
  return { scheduling_intent::nice, value };
}

[[nodiscard]] constexpr auto
priority(priority_level level) noexcept -> scheduling_config
{
  return nice(static_cast<int>(level));
}
} // namespace schedule

class thread_affinity
{
public:
  thread_affinity() = default;
  explicit thread_affinity(std::vector<int> cpus) : cpus_(std::move(cpus))
  {
    normalize();
  }

  void
  add_cpu(int cpu)
  {
    if (cpu < 0 || contains(cpu))
      return;
    cpus_.push_back(cpu);
    std::sort(cpus_.begin(), cpus_.end());
  }

  void
  remove_cpu(int cpu)
  {
    cpus_.erase(std::remove(cpus_.begin(), cpus_.end(), cpu), cpus_.end());
  }

  void
  clear() noexcept
  {
    cpus_.clear();
  }

  [[nodiscard]] auto
  contains(int cpu) const noexcept -> bool
  {
    return std::binary_search(cpus_.begin(), cpus_.end(), cpu);
  }

  [[nodiscard]] auto
  empty() const noexcept -> bool
  {
    return cpus_.empty();
  }

  [[nodiscard]] auto
  cpus() const noexcept -> std::vector<int> const&
  {
    return cpus_;
  }

private:
  void
  normalize()
  {
    cpus_.erase(std::remove_if(cpus_.begin(), cpus_.end(),
                               [](int cpu) { return cpu < 0; }),
                cpus_.end());
    std::sort(cpus_.begin(), cpus_.end());
    cpus_.erase(std::unique(cpus_.begin(), cpus_.end()), cpus_.end());
  }

  std::vector<int> cpus_;
};

struct thread_config
{
  std::string name{};
  scheduling_config scheduling{ schedule::normal() };
  std::optional<thread_affinity> affinity{};
};

enum class shutdown_policy : std::uint8_t
{
  drain,
  drop_pending
};

struct task_error
{
  std::exception_ptr exception;
  std::string task_description;
  std::thread::id thread_id;
  std::chrono::steady_clock::time_point timestamp;

  [[nodiscard]] static auto
  capture(std::string description = {}) -> task_error
  {
    return { std::current_exception(), std::move(description),
             std::this_thread::get_id(), std::chrono::steady_clock::now() };
  }

  [[nodiscard]] auto
  what() const -> std::string
  {
    try
      {
        if (exception)
          std::rethrow_exception(exception);
      }
    catch (std::exception const& error)
      {
        return error.what();
      }
    catch (...)
      {
        return "Unknown exception";
      }
    return "No exception";
  }

  void
  rethrow() const
  {
    if (exception)
      std::rethrow_exception(exception);
  }
};

using error_callback = std::function<void(task_error const&)>;

namespace detail
{
[[nodiscard]] constexpr auto
to_priority_level(int nice_value) noexcept -> priority_level
{
  if (nice_value <= -10)
    return priority_level::highest;
  if (nice_value < 0)
    return priority_level::high;
  if (nice_value == 0)
    return priority_level::normal;
  if (nice_value < 10)
    return priority_level::low;
  return priority_level::lowest;
}

class thread_start_gate
{
public:
  [[nodiscard]] auto
  wait() -> bool
  {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [this] { return ready_to_start_; });
    return run_;
  }

  void
  release(bool run)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      run_ = run;
      ready_to_start_ = true;
    }
    ready_.notify_all();
  }

private:
  std::mutex mutex_;
  std::condition_variable ready_;
  bool ready_to_start_{ false };
  bool run_{ false };
};

[[nodiscard]] inline auto
current_exception_error_code() noexcept -> std::error_code
{
  try
    {
      throw;
    }
  catch (std::system_error const& error)
    {
      return error.code();
    }
  catch (std::bad_alloc const&)
    {
      return std::make_error_code(std::errc::not_enough_memory);
    }
  catch (...)
    {
      return std::make_error_code(std::errc::state_not_recoverable);
    }
}

[[nodiscard]] constexpr auto
to_native(scheduling_config config) noexcept -> native_scheduling_config
{
  switch (config.intent)
    {
    case scheduling_intent::background:
      return native_schedule::background();
    case scheduling_intent::interactive:
      return native_schedule::interactive();
    case scheduling_intent::low_latency:
      return native_schedule::low_latency();
    case scheduling_intent::realtime_fifo:
      return native_schedule::realtime_fifo(config.priority);
    case scheduling_intent::realtime_round_robin:
      return native_schedule::realtime_rr(config.priority);
    case scheduling_intent::nice:
      return native_schedule::posix_nice(config.priority);
    case scheduling_intent::normal:
    default:
      return native_schedule::normal();
    }
}

[[nodiscard]] inline auto
to_native(thread_affinity const& affinity) -> native_thread_affinity
{
  return native_thread_affinity(affinity.cpus());
}

[[nodiscard]] inline auto
to_native(thread_config const& config) -> native_thread_config
{
  native_thread_config native;
  native.name = config.name;
  native.scheduling = to_native(config.scheduling);
  if (config.affinity)
    native.affinity = to_native(*config.affinity);
  return native;
}

[[nodiscard]] inline auto
has_thread_configuration(thread_config const& config) noexcept -> bool
{
  return !config.name.empty() || config.affinity.has_value()
         || config.scheduling.intent != scheduling_intent::normal
         || config.scheduling.priority != 0;
}

[[nodiscard]] constexpr auto
to_native(shutdown_policy policy) noexcept -> shutdown_policy_backend
{
  return policy == shutdown_policy::drop_pending
             ? shutdown_policy_backend::drop_pending
             : shutdown_policy_backend::drain;
}

[[nodiscard]] inline auto
from_native(native_thread_affinity const& affinity) -> thread_affinity
{
  return thread_affinity(affinity.get_cpus());
}
} // namespace detail

class thread
{
public:
  using native_handle_type = std::thread::native_handle_type;

  thread() = default;
  explicit thread(std::thread&& value) noexcept : impl_(std::move(value)) {}
  thread(std::thread&& value, std::uint64_t native_id) noexcept
      : impl_(std::move(value), static_cast<native_thread_id>(native_id))
  {
  }

  template <
      typename F, typename... Args,
      std::enable_if_t<!std::is_same_v<std::decay_t<F>, thread>
                           && !std::is_same_v<std::decay_t<F>, thread_config>,
                       int> = 0>
  explicit thread(F&& function, Args&&... args)
      : impl_(std::forward<F>(function), std::forward<Args>(args)...)
  {
  }

  template <typename F, typename... Args>
  thread(thread_config const& config, F&& function, Args&&... args)
      : impl_(make_configured_impl(config, std::forward<F>(function),
                                   std::forward<Args>(args)...))
  {
  }

  thread(thread&&) noexcept = default;
  auto operator=(thread&&) noexcept -> thread& = default;
  thread(thread const&) = delete;
  auto operator=(thread const&) -> thread& = delete;

  template <typename F, typename... Args>
  static auto
  create(F&& function, Args&&... args)
      -> std::enable_if_t<!std::is_same_v<std::decay_t<F>, thread_config>,
                          result<thread>>
  {
    try
      {
        return thread(std::forward<F>(function), std::forward<Args>(args)...);
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  template <typename F, typename... Args>
  static auto
  create(thread_config const& config, F&& function, Args&&... args)
      -> result<thread>
  {
    try
      {
        return thread(config, std::forward<F>(function),
                      std::forward<Args>(args)...);
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  join() -> result<void>
  {
    if (!impl_.joinable())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    try
      {
        impl_.join();
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  void
  join_or_throw()
  {
    if (!impl_.joinable())
      throw std::system_error(
          std::make_error_code(std::errc::invalid_argument), "thread::join");
    impl_.join();
  }

  auto
  detach() -> result<void>
  {
    if (!impl_.joinable())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    try
      {
        impl_.detach();
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  void
  detach_or_throw()
  {
    if (!impl_.joinable())
      throw std::system_error(
          std::make_error_code(std::errc::invalid_argument), "thread::detach");
    impl_.detach();
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

  [[nodiscard]] auto
  native_handle() -> native_handle_type
  {
    return impl_.native_handle();
  }

  [[nodiscard]] static auto
  hardware_concurrency() noexcept -> unsigned
  {
    return std::thread::hardware_concurrency();
  }

  auto
  configure(thread_config const& config) -> result<void>
  {
    try
      {
        return impl_.configure(detail::to_native(config));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  set_priority(priority_level level) -> result<void>
  {
    return impl_.configure(
        detail::native_schedule::posix_nice(static_cast<int>(level)));
  }

  auto
  set_nice(int nice_value) -> result<void>
  {
    return impl_.configure(detail::native_schedule::posix_nice(nice_value));
  }

  [[nodiscard]] auto
  get_priority() const -> result<priority_level>
  {
    auto value = impl_.get_nice_value();
    if (!value)
      return unexpected(value.error());
    return detail::to_priority_level(value.value());
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
    try
      {
        return impl_.set_affinity(detail::to_native(affinity));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  [[nodiscard]] auto
  get_affinity() const -> std::optional<thread_affinity>
  {
    auto affinity = impl_.get_affinity();
    if (!affinity)
      return std::nullopt;
    return detail::from_native(*affinity);
  }

  [[nodiscard]] auto
  release() noexcept -> std::thread
  {
    return impl_.release();
  }

private:
  template <typename F, typename... Args>
  static auto
  make_configured_impl(thread_config const& config, F&& function,
                       Args&&... args) -> detail::thread_backend
  {
    auto gate = std::make_shared<detail::thread_start_gate>();
    detail::thread_backend value(
        [gate,
         callable = detail::bind_args(std::forward<F>(function),
                                      std::forward<Args>(args)...)]() mutable
          {
            if (gate->wait())
              callable();
          });

    try
      {
        auto configured = value.configure(detail::to_native(config));
        if (!configured)
          {
            gate->release(false);
            throw std::system_error(configured.error(),
                                    "thread configuration");
          }
        gate->release(true);
      }
    catch (...)
      {
        gate->release(false);
        if (value.joinable())
          value.join();
        throw;
      }
    return value;
  }

  detail::thread_backend impl_;
};

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
class jthread
{
public:
  using native_handle_type = std::jthread::native_handle_type;

  jthread() noexcept = default;
  explicit jthread(std::jthread&& value) noexcept : impl_(std::move(value)) {}
  jthread(std::jthread&& value, std::uint64_t native_id) noexcept
      : native_id_(static_cast<native_thread_id>(native_id)),
        impl_(std::move(value))
  {
  }

  template <typename F, typename... Args,
            std::enable_if_t<
                !std::is_same_v<std::decay_t<F>, jthread>
                    && !std::is_same_v<std::decay_t<F>, thread_config>
                    && std::is_constructible_v<std::jthread, F, Args...>,
                int> = 0>
  explicit jthread(F&& function, Args&&... args)
      : impl_(make_impl(native_id_, std::forward<F>(function),
                        std::forward<Args>(args)...))
  {
  }

  template <typename F, typename... Args>
  jthread(thread_config const& config, F&& function, Args&&... args)
      : impl_(make_configured_impl(config, native_id_,
                                   std::forward<F>(function),
                                   std::forward<Args>(args)...))
  {
  }

  jthread(jthread&&) noexcept = default;
  auto operator=(jthread&&) noexcept -> jthread& = default;
  jthread(jthread const&) = delete;
  auto operator=(jthread const&) -> jthread& = delete;

  template <typename F, typename... Args>
  static auto
  create(F&& function, Args&&... args)
      -> std::enable_if_t<!std::is_same_v<std::decay_t<F>, thread_config>,
                          result<jthread>>
  {
    try
      {
        return jthread(std::forward<F>(function), std::forward<Args>(args)...);
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  template <typename F, typename... Args>
  static auto
  create(thread_config const& config, F&& function, Args&&... args)
      -> result<jthread>
  {
    try
      {
        return jthread(config, std::forward<F>(function),
                       std::forward<Args>(args)...);
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  join() -> result<void>
  {
    if (!impl_.joinable())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    try
      {
        impl_.join();
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  void
  join_or_throw()
  {
    if (!impl_.joinable())
      throw std::system_error(
          std::make_error_code(std::errc::invalid_argument), "jthread::join");
    impl_.join();
  }

  auto
  detach() -> result<void>
  {
    if (!impl_.joinable())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    try
      {
        impl_.detach();
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  void
  detach_or_throw()
  {
    if (!impl_.joinable())
      throw std::system_error(
          std::make_error_code(std::errc::invalid_argument),
          "jthread::detach");
    impl_.detach();
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
  native_handle() -> native_handle_type
  {
    return impl_.native_handle();
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
    try
      {
        auto view = native_view();
        return view.configure(detail::to_native(config));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  set_priority(priority_level level) -> result<void>
  {
    auto view = native_view();
    return view.configure(
        detail::native_schedule::posix_nice(static_cast<int>(level)));
  }

  auto
  set_nice(int nice_value) -> result<void>
  {
    auto view = native_view();
    return view.configure(detail::native_schedule::posix_nice(nice_value));
  }

  [[nodiscard]] auto
  get_priority() const -> result<priority_level>
  {
    auto view = native_view();
    auto value = view.get_nice_value();
    if (!value)
      return unexpected(value.error());
    return detail::to_priority_level(value.value());
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
    try
      {
        auto view = native_view();
        return view.set_affinity(detail::to_native(affinity));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  [[nodiscard]] auto
  get_affinity() const -> std::optional<thread_affinity>
  {
    auto view = native_view();
    auto affinity = view.get_affinity();
    if (!affinity)
      return std::nullopt;
    return detail::from_native(*affinity);
  }

  [[nodiscard]] auto
  release() noexcept -> std::jthread
  {
    auto value = std::move(impl_);
    native_id_ = {};
    return value;
  }

private:
  using native_view_type
      = detail::basic_thread_backend<std::jthread, detail::non_owning_tag>;

  [[nodiscard]] auto
  native_view() const -> native_view_type
  {
    return native_view_type(const_cast<std::jthread&>(impl_), native_id_);
  }

  template <typename F, typename... Args>
  static auto
  make_impl(native_thread_id& native_id, F&& function, Args&&... args)
      -> std::jthread
  {
    using function_type = std::decay_t<F>;
    auto identity = std::make_shared<detail::thread_identity_state>();
    std::jthread value(
        [identity, callable = function_type(std::forward<F>(function)),
         arguments = std::make_tuple(std::forward<Args>(args)...)](
            std::stop_token token) mutable
          {
            identity->publish(detail::current_native_thread_id());
            std::apply(
                [&callable, &token](auto&&... stored)
                  {
                    if constexpr (std::is_invocable_v<function_type,
                                                      std::stop_token,
                                                      decltype(stored)...>)
                      std::invoke(std::move(callable), std::move(token),
                                  std::forward<decltype(stored)>(stored)...);
                    else
                      std::invoke(std::move(callable),
                                  std::forward<decltype(stored)>(stored)...);
                  },
                std::move(arguments));
          });
    native_id = identity->wait();
    return value;
  }

  template <typename F, typename... Args>
  static auto
  make_configured_impl(thread_config const& config,
                       native_thread_id& native_id, F&& function,
                       Args&&... args) -> std::jthread
  {
    using function_type = std::decay_t<F>;
    auto gate = std::make_shared<detail::thread_start_gate>();
    auto identity = std::make_shared<detail::thread_identity_state>();
    std::jthread value(
        [gate, identity, callable = function_type(std::forward<F>(function)),
         arguments = std::make_tuple(std::forward<Args>(args)...)](
            std::stop_token token) mutable
          {
            identity->publish(detail::current_native_thread_id());
            if (!gate->wait())
              return;
            std::apply(
                [&callable, &token](auto&&... stored)
                  {
                    if constexpr (std::is_invocable_v<function_type,
                                                      std::stop_token,
                                                      decltype(stored)...>)
                      std::invoke(std::move(callable), std::move(token),
                                  std::forward<decltype(stored)>(stored)...);
                    else
                      std::invoke(std::move(callable),
                                  std::forward<decltype(stored)>(stored)...);
                  },
                std::move(arguments));
          });

    native_id = identity->wait();
    try
      {
        native_view_type view(value, native_id);
        auto configured = view.configure(detail::to_native(config));
        if (!configured)
          {
            gate->release(false);
            throw std::system_error(configured.error(),
                                    "jthread configuration");
          }
        gate->release(true);
      }
    catch (...)
      {
        gate->release(false);
        throw;
      }
    return value;
  }

  native_thread_id native_id_{};
  std::jthread impl_;
};
#endif

class thread_view
{
public:
  explicit thread_view(std::thread& value) noexcept : impl_(value) {}
  thread_view(std::thread& value, std::uint64_t native_id) noexcept
      : impl_(value, static_cast<native_thread_id>(native_id))
  {
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

  auto
  configure(thread_config const& config) -> result<void>
  {
    try
      {
        return impl_.configure(detail::to_native(config));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  set_priority(priority_level level) -> result<void>
  {
    return impl_.configure(
        detail::native_schedule::posix_nice(static_cast<int>(level)));
  }

  auto
  set_nice(int nice_value) -> result<void>
  {
    return impl_.configure(detail::native_schedule::posix_nice(nice_value));
  }

  [[nodiscard]] auto
  get_priority() const -> result<priority_level>
  {
    auto value = impl_.get_nice_value();
    if (!value)
      return unexpected(value.error());
    return detail::to_priority_level(value.value());
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
    try
      {
        return impl_.set_affinity(detail::to_native(affinity));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  [[nodiscard]] auto
  get_affinity() const -> std::optional<thread_affinity>
  {
    auto affinity = impl_.get_affinity();
    if (!affinity)
      return std::nullopt;
    return detail::from_native(*affinity);
  }

private:
  detail::thread_view_backend impl_;
};

struct registered_thread
{
  std::uint64_t native_id{ 0 };
  std::thread::id id;
  std::string name;
  std::string component;
  bool alive{ false };
};

class thread_registry
{
public:
  thread_registry() : owned_(std::make_unique<thread_registry_backend>()) {}

  thread_registry(thread_registry&&) noexcept = default;
  auto operator=(thread_registry&&) noexcept -> thread_registry& = default;
  thread_registry(thread_registry const&) = delete;
  auto operator=(thread_registry const&) -> thread_registry& = delete;

  static auto
  create() -> result<thread_registry>
  {
    try
      {
        return thread_registry{};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  register_current_thread(std::string name = {}, std::string component = {})
      -> result<void>
  {
    try
      {
        auto control = thread_control_block::create_for_current_thread();
        native().register_current_thread(control, std::move(name),
                                         std::move(component));
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  unregister_current_thread() -> result<void>
  {
    try
      {
        native().unregister_current_thread();
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  [[nodiscard]] auto
  count() const -> std::size_t
  {
    return native().count();
  }

  [[nodiscard]] auto
  empty() const -> bool
  {
    return native().empty();
  }

  [[nodiscard]] auto
  snapshot() const -> result<std::vector<registered_thread>>
  {
    try
      {
        auto entries = native().query().entries();
        std::vector<registered_thread> result_entries;
        result_entries.reserve(entries.size());
        for (auto const& entry : entries)
          {
            result_entries.push_back({ static_cast<std::uint64_t>(entry.tid),
                                       entry.std_id, entry.name,
                                       entry.component, entry.alive });
          }
        return result_entries;
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  configure(std::uint64_t native_id, thread_config const& config)
      -> result<void>
  {
    try
      {
        return native().configure(static_cast<native_thread_id>(native_id),
                                  detail::to_native(config));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  set_priority(std::uint64_t native_id, priority_level level) -> result<void>
  {
    return set_nice(native_id, static_cast<int>(level));
  }

  auto
  set_nice(std::uint64_t native_id, int nice_value) -> result<void>
  {
    try
      {
        return native().configure(
            static_cast<native_thread_id>(native_id),
            detail::native_schedule::posix_nice(nice_value));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  [[nodiscard]] auto
  get_priority(std::uint64_t native_id) const -> result<priority_level>
  {
    try
      {
        auto value = native().get_nice_value(
            static_cast<native_thread_id>(native_id));
        if (!value)
          return unexpected(value.error());
        return detail::to_priority_level(value.value());
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

private:
  struct global_tag
  {
  };

  explicit thread_registry(global_tag /*unused*/) noexcept : global_(true) {}

  [[nodiscard]] auto
  native() -> thread_registry_backend&
  {
    return global_ ? registry() : *owned_;
  }

  [[nodiscard]] auto
  native() const -> thread_registry_backend const&
  {
    return global_ ? registry() : *owned_;
  }

  std::unique_ptr<thread_registry_backend> owned_;
  bool global_{ false };

  friend auto global_registry() -> thread_registry&;
  friend void use_global_registry(thread_registry* value);
};

[[nodiscard]] inline auto
global_registry() -> thread_registry&
{
  static thread_registry value(thread_registry::global_tag{});
  return value;
}

inline void
use_global_registry(thread_registry* value)
{
  set_external_registry(value != nullptr ? &value->native() : nullptr);
}

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

  explicit thread_pool(std::size_t worker_count)
      : thread_pool(thread_pool_config{ worker_count })
  {
  }

  explicit thread_pool(thread_pool_config config)
      : config_(std::move(config)),
        impl_(std::make_unique<thread_pool_backend>(config_.worker_count,
                                                    config_.register_workers))
  {
    if (detail::has_thread_configuration(config_.workers))
      {
        auto configured
            = impl_->configure_threads(detail::to_native(config_.workers));
        if (!configured)
          throw std::system_error(configured.error(),
                                  "thread_pool worker configuration");
      }
  }

  thread_pool(thread_pool&&) noexcept = default;
  auto operator=(thread_pool&&) noexcept -> thread_pool& = default;
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
  submit(F&& function, Args&&... args)
      -> result<std::future<std::invoke_result_t<F, Args...>>>
  {
    try
      {
        using return_type = std::invoke_result_t<F, Args...>;
        if (!config_.on_task_error)
          return impl_->try_submit(std::forward<F>(function),
                                   std::forward<Args>(args)...);

        auto callback = config_.on_task_error;
        auto wrapped
            = [bound = detail::bind_args(std::forward<F>(function),
                                         std::forward<Args>(args)...),
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
  submit_or_throw(F&& function, Args&&... args)
      -> std::future<std::invoke_result_t<F, Args...>>
  {
    auto submitted
        = submit(std::forward<F>(function), std::forward<Args>(args)...);
    if (!submitted)
      throw std::system_error(submitted.error(), "thread_pool::submit");
    return std::move(*submitted);
  }

  template <typename F, typename... Args>
  auto
  post(F&& function, Args&&... args) -> result<void>
  {
    try
      {
        if (!config_.on_task_error)
          return impl_->try_post(std::forward<F>(function),
                                 std::forward<Args>(args)...);

        auto callback = config_.on_task_error;
        auto wrapped = [bound = detail::bind_args(std::forward<F>(function),
                                                  std::forward<Args>(args)...),
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
    impl_->wait_for_tasks();
  }

  auto
  configure_workers(thread_config const& config) -> result<void>
  {
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
    return impl_->size();
  }

private:
  thread_pool_config config_;
  std::unique_ptr<thread_pool_backend> impl_;
};

class scheduled_task
{
public:
  scheduled_task(scheduled_task const&) = default;
  scheduled_task(scheduled_task&&) noexcept = default;
  auto operator=(scheduled_task const&) -> scheduled_task& = default;
  auto operator=(scheduled_task&&) noexcept -> scheduled_task& = default;

  void
  cancel()
  {
    impl_.cancel();
  }

  [[nodiscard]] auto
  is_cancelled() const noexcept -> bool
  {
    return impl_.is_cancelled();
  }

  [[nodiscard]] auto
  id() const noexcept -> std::uint64_t
  {
    return impl_.id();
  }

private:
  explicit scheduled_task(scheduled_task_backend value)
      : impl_(std::move(value))
  {
  }

  scheduled_task_backend impl_;
  friend class scheduled_pool;
};

struct scheduled_pool_config
{
  std::size_t worker_count{ std::thread::hardware_concurrency() };
  bool register_workers{ false };
  thread_config workers{};
  thread_config scheduler{};
  shutdown_policy shutdown{ shutdown_policy::drain };
  error_callback on_task_error{};
};

class scheduled_pool
{
public:
  scheduled_pool() : scheduled_pool(scheduled_pool_config{}) {}

  explicit scheduled_pool(std::size_t worker_count)
      : scheduled_pool(scheduled_pool_config{ worker_count })
  {
  }

  explicit scheduled_pool(scheduled_pool_config config)
      : config_(std::move(config)),
        impl_(std::make_unique<scheduled_pool_backend>(
            config_.worker_count, config_.register_workers))
  {
    if (detail::has_thread_configuration(config_.workers))
      {
        auto configured
            = impl_->configure_threads(detail::to_native(config_.workers));
        if (!configured)
          throw std::system_error(configured.error(),
                                  "scheduled_pool worker configuration");
      }

    if (detail::has_thread_configuration(config_.scheduler))
      {
        auto configured = impl_->configure_scheduler_thread(
            detail::to_native(config_.scheduler));
        if (!configured)
          throw std::system_error(configured.error(),
                                  "scheduled_pool scheduler configuration");
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
        stopped_.store(other.stopped_.load(std::memory_order_acquire),
                       std::memory_order_release);
        other.stopped_.store(true, std::memory_order_release);
      }
    return *this;
  }
  scheduled_pool(scheduled_pool const&) = delete;
  auto operator=(scheduled_pool const&) -> scheduled_pool& = delete;

  ~scheduled_pool()
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

  static auto
  create(scheduled_pool_config config = {}) -> result<scheduled_pool>
  {
    try
      {
        return scheduled_pool(std::move(config));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  template <typename Rep, typename Period, typename F>
  auto
  schedule_after(std::chrono::duration<Rep, Period> delay, F&& function)
      -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    try
      {
        auto handle = impl_->schedule_after(
            std::chrono::duration_cast<scheduled_pool_backend::duration>(
                delay),
            wrap_task(std::forward<F>(function)));
        return scheduled_task(std::move(handle));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  template <typename F>
  auto
  schedule_at(std::chrono::steady_clock::time_point time, F&& function)
      -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    try
      {
        return scheduled_task(
            impl_->schedule_at(time, wrap_task(std::forward<F>(function))));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  template <typename Rep, typename Period, typename F>
  auto
  schedule_periodic(std::chrono::duration<Rep, Period> interval, F&& function)
      -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    auto const native_interval
        = std::chrono::duration_cast<scheduled_pool_backend::duration>(
            interval);
    if (native_interval <= scheduled_pool_backend::duration::zero())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    try
      {
        auto handle = impl_->schedule_periodic(
            native_interval, wrap_task(std::forward<F>(function)));
        return scheduled_task(std::move(handle));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  template <typename InitialRep, typename InitialPeriod, typename IntervalRep,
            typename IntervalPeriod, typename F>
  auto
  schedule_periodic_after(
      std::chrono::duration<InitialRep, InitialPeriod> initial_delay,
      std::chrono::duration<IntervalRep, IntervalPeriod> interval,
      F&& function) -> result<scheduled_task>
  {
    if (stopped_.load(std::memory_order_acquire))
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    auto const native_interval
        = std::chrono::duration_cast<scheduled_pool_backend::duration>(
            interval);
    if (native_interval <= scheduled_pool_backend::duration::zero())
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    try
      {
        auto handle = impl_->schedule_periodic_after(
            std::chrono::duration_cast<scheduled_pool_backend::duration>(
                initial_delay),
            native_interval, wrap_task(std::forward<F>(function)));
        return scheduled_task(std::move(handle));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  shutdown(shutdown_policy policy = shutdown_policy::drain) -> result<void>
  {
    try
      {
        if (!stopped_.exchange(true, std::memory_order_acq_rel))
          impl_->shutdown(detail::to_native(policy));
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  [[nodiscard]] auto
  scheduled_count() const -> std::size_t
  {
    return impl_ ? impl_->scheduled_count() : 0;
  }

private:
  template <typename F>
  auto
  wrap_task(F&& function)
  {
    using function_type = std::decay_t<F>;
    auto callback = config_.on_task_error;
    return [function = function_type(std::forward<F>(function)),
            callback = std::move(callback)]() mutable
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
  std::unique_ptr<scheduled_pool_backend> impl_;
  std::atomic<bool> stopped_{ false };
};

namespace advanced
{
using work_stealing_pool = work_stealing_pool_backend;
using polling_pool = polling_pool_backend;
using lightweight_pool = lightweight_pool_backend;
using inline_pool = inline_pool_backend;
using global_thread_pool = global_thread_pool_backend;
using global_work_stealing_pool = global_work_stealing_pool_backend;
using native_thread_priority = ::threadschedule::native_thread_priority;
using native_scheduling_policy = ::threadschedule::native_scheduling_policy;
using native_scheduling_config = ::threadschedule::native_scheduling_config;
using native_thread_config = ::threadschedule::native_thread_config;
using scheduler_parameters = ::threadschedule::scheduler_parameters;
using composite_thread_registry = composite_thread_registry_backend;
namespace native_schedule = detail::native_schedule;
} // namespace advanced

} // namespace threadschedule
