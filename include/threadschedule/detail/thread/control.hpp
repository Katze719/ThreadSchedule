#pragma once

#include "../../result.hpp"
#include "../../scheduling.hpp"
#include "../../thread_affinity.hpp"
#include "../../thread_config.hpp"
#include "../scheduling/native.hpp"

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace threadschedule
{

namespace detail
{
struct native_thread_access;
}

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
      {
        auto native = native_schedule::realtime_fifo(config.priority);
        native.valid = config.priority >= 1 && config.priority <= 99;
        return native;
      }
    case scheduling_intent::realtime_round_robin:
      {
        auto native = native_schedule::realtime_rr(config.priority);
        native.valid = config.priority >= 1 && config.priority <= 99;
        return native;
      }
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
  native_thread_affinity native(affinity.cpus());
  if (native.get_cpus() != affinity.cpus())
    throw std::system_error(std::make_error_code(std::errc::invalid_argument), "thread affinity is not representable");
  return native;
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
  return !config.name.empty() || config.affinity.has_value() || config.scheduling.intent != scheduling_intent::normal
         || config.scheduling.priority != 0;
}

[[nodiscard]] inline auto
from_native(native_thread_affinity const& affinity) -> thread_affinity
{
  return thread_affinity(affinity.get_cpus());
}

template <typename Function>
[[nodiscard]] auto
try_result(Function&& function) -> decltype(std::forward<Function>(function)())
{
  try
    {
      return std::forward<Function>(function)();
    }
  catch (...)
    {
      return unexpected(current_exception_error_code());
    }
}

namespace thread_lifecycle
{
template <typename ThreadLike>
auto
join(ThreadLike& value) -> result<void>
{
  if (!value.joinable())
    return unexpected(std::make_error_code(std::errc::invalid_argument));
  return try_result(
      [&value]() -> result<void>
        {
          value.join();
          return {};
        });
}

template <typename ThreadLike>
void
join_or_throw(ThreadLike& value, char const* operation)
{
  if (!value.joinable())
    throw std::system_error(std::make_error_code(std::errc::invalid_argument), operation);
  value.join();
}

template <typename ThreadLike>
auto
detach(ThreadLike& value) -> result<void>
{
  if (!value.joinable())
    return unexpected(std::make_error_code(std::errc::invalid_argument));
  return try_result(
      [&value]() -> result<void>
        {
          value.detach();
          return {};
        });
}

template <typename ThreadLike>
void
detach_or_throw(ThreadLike& value, char const* operation)
{
  if (!value.joinable())
    throw std::system_error(std::make_error_code(std::errc::invalid_argument), operation);
  value.detach();
}
} // namespace thread_lifecycle

namespace portable_thread_control
{
template <typename Control>
auto
configure(Control& control, thread_config const& config) -> result<void>
{
  return try_result([&control, &config]() -> result<void> { return control.configure(to_native(config)); });
}

template <typename Control>
auto
set_priority(Control& control, priority_level level) -> result<void>
{
  return control.configure(native_schedule::posix_nice(static_cast<int>(level)));
}

template <typename Control>
auto
set_nice(Control& control, int nice_value) -> result<void>
{
  return control.configure(native_schedule::posix_nice(nice_value));
}

[[nodiscard]] inline auto
get_priority(result<int> value) -> result<priority_level>
{
  if (!value)
    return unexpected(value.error());
  return to_priority_level(value.value());
}

template <typename Control>
auto
set_affinity(Control& control, thread_affinity const& affinity) -> result<void>
{
  return try_result([&control, &affinity]() -> result<void> { return control.set_affinity(to_native(affinity)); });
}

[[nodiscard]] inline auto
get_affinity(result<native_thread_affinity> affinity) -> result<thread_affinity>
{
  if (!affinity)
    return unexpected(affinity.error());
  return from_native(affinity.value());
}
} // namespace portable_thread_control

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
template <typename Function, typename Tuple>
void
invoke_jthread_callable(Function& callable, Tuple&& arguments, std::stop_token token)
{
  using function_type = std::remove_reference_t<Function>;
  std::apply(
      [&callable, &token](auto&&... stored)
        {
          if constexpr (std::is_invocable_v<function_type, std::stop_token, decltype(stored)...>)
            std::invoke(std::move(callable), std::move(token), std::forward<decltype(stored)>(stored)...);
          else
            std::invoke(std::move(callable), std::forward<decltype(stored)>(stored)...);
        },
      std::forward<Tuple>(arguments));
}
#endif
} // namespace detail

} // namespace threadschedule
