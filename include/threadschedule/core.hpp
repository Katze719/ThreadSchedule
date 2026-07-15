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
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
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
  realtime_round_robin
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
      : impl_(std::forward<F>(function), std::forward<Args>(args)...)
  {
    configure_or_throw(config);
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
    impl_.join();
  }

  auto
  detach() -> result<void>
  {
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
  void
  configure_or_throw(thread_config const& config)
  {
    auto configured = impl_.configure(detail::to_native(config));
    if (!configured)
      throw std::system_error(configured.error(), "thread configuration");
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

  template <typename F, typename... Args,
            std::enable_if_t<
                !std::is_same_v<std::decay_t<F>, jthread>
                    && !std::is_same_v<std::decay_t<F>, thread_config>
                    && std::is_constructible_v<std::jthread, F, Args...>,
                int> = 0>
  explicit jthread(F&& function, Args&&... args)
      : impl_(std::forward<F>(function), std::forward<Args>(args)...)
  {
  }

  template <typename F, typename... Args>
  jthread(thread_config const& config, F&& function, Args&&... args)
      : impl_(std::forward<F>(function), std::forward<Args>(args)...)
  {
    configure_or_throw(config);
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
    impl_.join();
  }

  auto
  detach() -> result<void>
  {
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
    return std::move(impl_);
  }

private:
  using native_view_type
      = detail::basic_thread_backend<std::jthread, detail::non_owning_tag>;

  [[nodiscard]] auto
  native_view() const -> native_view_type
  {
    return native_view_type(const_cast<std::jthread&>(impl_));
  }

  void
  configure_or_throw(thread_config const& config)
  {
    auto configured = configure(config);
    if (!configured)
      throw std::system_error(configured.error(), "jthread configuration");
  }

  std::jthread impl_;
};
#endif

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
        native().register_current_thread(std::move(name),
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
    if (!config_.workers.name.empty() || config_.workers.affinity.has_value()
        || config_.workers.scheduling.intent != scheduling_intent::normal)
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
      : impl_(std::make_unique<scheduled_pool_backend>(config.worker_count))
  {
  }

  scheduled_pool(scheduled_pool&& other) noexcept
      : impl_(std::move(other.impl_)),
        stopped_(other.stopped_.load(std::memory_order_acquire))
  {
    other.stopped_.store(true, std::memory_order_release);
  }

  auto
  operator=(scheduled_pool&& other) noexcept -> scheduled_pool&
  {
    if (this != &other)
      {
        impl_ = std::move(other.impl_);
        stopped_.store(other.stopped_.load(std::memory_order_acquire),
                       std::memory_order_release);
        other.stopped_.store(true, std::memory_order_release);
      }
    return *this;
  }
  scheduled_pool(scheduled_pool const&) = delete;
  auto operator=(scheduled_pool const&) -> scheduled_pool& = delete;

  static auto
  create(scheduled_pool_config config = {}) -> result<scheduled_pool>
  {
    try
      {
        return scheduled_pool(config);
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
            std::forward<F>(function));
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
            impl_->schedule_at(time, std::forward<F>(function)));
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
    try
      {
        auto handle = impl_->schedule_periodic(
            std::chrono::duration_cast<scheduled_pool_backend::duration>(
                interval),
            std::forward<F>(function));
        return scheduled_task(std::move(handle));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  shutdown() -> result<void>
  {
    try
      {
        if (!stopped_.exchange(true, std::memory_order_acq_rel))
          impl_->shutdown();
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
    return impl_->scheduled_count();
  }

private:
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
