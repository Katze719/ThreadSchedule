#pragma once

#include "../../pool_statistics.hpp"
#include "../../result.hpp"
#include "../../shutdown_policy.hpp"
#include "../../thread_config.hpp"
#include "../../worker_count.hpp"
#include "../../worker_registration.hpp"
#include "../thread/control.hpp"
#include "shutdown.hpp"
#include "work_stealing_pool_backend.hpp"

#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace threadschedule::detail
{

template <typename Backend>
[[nodiscard]] auto
make_pool_backend(worker_count count, worker_registration registration) -> std::unique_ptr<Backend>
{
  if constexpr (std::is_same_v<Backend, work_stealing_pool_backend>)
    return std::make_unique<Backend>(count.resolve(),
                                     work_stealing_deque<typename Backend::queued_task>::default_capacity,
                                     registration == worker_registration::global_registry);
  else
    return std::make_unique<Backend>(count.resolve(), registration == worker_registration::global_registry);
}

template <typename T, typename = void>
struct has_stolen_tasks : std::false_type
{
};

template <typename T>
struct has_stolen_tasks<T, std::void_t<decltype(std::declval<T const&>().stolen_tasks)>> : std::true_type
{
};

template <typename Backend>
void
dispose_pool_backend(std::unique_ptr<Backend>& backend) noexcept
{
  if (!backend)
    return;

  if (!backend->is_current_worker())
    {
      try
        {
          backend->shutdown(shutdown_policy_backend::drain);
        }
      catch (...)
        {
        }
      backend.reset();
      return;
    }

  // The backend is still executing the callback that destroys its facade.
  // Transfer ownership until a different thread can safely join every worker.
  auto* pending = backend.release();
  try
    {
      std::thread reaper(
          [pending]() noexcept
            {
              std::unique_ptr<Backend> owner(pending);
              try
                {
                  owner->shutdown(shutdown_policy_backend::drain);
                }
              catch (...)
                {
                }
            });
      reaper.detach();
    }
  catch (...)
    {
      // Destroying the backend on this worker would terminate the process.
      // Keeping it alive is the only non-terminating fallback.
      (void)pending;
    }
}

template <typename Backend>
class submitting_pool_facade
{
public:
  explicit submitting_pool_facade(worker_count count, worker_registration registration)
      : impl_(make_pool_backend<Backend>(count, registration))
  {
  }

  submitting_pool_facade(submitting_pool_facade const&) = delete;
  auto operator=(submitting_pool_facade const&) -> submitting_pool_facade& = delete;
  submitting_pool_facade(submitting_pool_facade&& other) noexcept : impl_(std::move(other.impl_)) {}
  auto
  operator=(submitting_pool_facade&& other) noexcept -> submitting_pool_facade&
  {
    if (this != &other)
      {
        dispose_pool_backend(impl_);
        impl_ = std::move(other.impl_);
      }
    return *this;
  }

  template <typename F, typename... Args>
  auto
  submit(F&& function, Args&&... args) -> result<std::future<bind_result_t<F, Args...>>>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result([&]() -> result<std::future<bind_result_t<F, Args...>>>
                        { return impl_->try_submit(std::forward<F>(function), std::forward<Args>(args)...); });
  }

  template <typename F, typename... Args>
  auto
  submit_or_throw(F&& function, Args&&... args) -> std::future<bind_result_t<F, Args...>>
  {
    auto submitted = submit(std::forward<F>(function), std::forward<Args>(args)...);
    if (!submitted)
      throw std::system_error(submitted.error(), "advanced pool submission");
    return std::move(*submitted);
  }

  template <typename F, typename... Args>
  auto
  post(F&& function, Args&&... args) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result([&]() -> result<void>
                        { return impl_->try_post(std::forward<F>(function), std::forward<Args>(args)...); });
  }

  template <typename F, typename... Args>
  void
  post_or_throw(F&& function, Args&&... args)
  {
    auto posted = post(std::forward<F>(function), std::forward<Args>(args)...);
    if (!posted)
      throw std::system_error(posted.error(), "advanced pool post");
  }

  template <typename Iterator>
  auto
  submit_batch(Iterator begin, Iterator end) -> result<std::vector<std::future<void>>>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result([&]() -> result<std::vector<std::future<void>>> { return impl_->try_submit_batch(begin, end); });
  }

  template <typename Iterator>
  auto
  submit_batch_or_throw(Iterator begin, Iterator end) -> std::vector<std::future<void>>
  {
    auto submitted = submit_batch(begin, end);
    if (!submitted)
      throw std::system_error(submitted.error(), "advanced pool batch submission");
    return std::move(*submitted);
  }

  template <typename Iterator, typename Function>
  auto
  parallel_for_each(Iterator begin, Iterator end, Function&& function) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result(
        [&]() -> result<void>
          {
            impl_->parallel_for_each(begin, end, std::forward<Function>(function));
            return {};
          });
  }

  auto
  wait() -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result(
        [&]() -> result<void>
          {
            impl_->wait_for_tasks();
            return {};
          });
  }

  auto
  configure_workers(thread_config const& config) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result([&]() -> result<void> { return impl_->configure_threads(to_native(config)); });
  }

  auto
  distribute_workers() -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result([&]() -> result<void> { return impl_->distribute_across_cpus(); });
  }

  auto
  shutdown(shutdown_policy policy = shutdown_policy::drain) -> result<void>
  {
    if (!impl_)
      return {};
    return try_result(
        [&]() -> result<void>
          {
            impl_->shutdown(to_native(policy));
            return {};
          });
  }

  auto
  shutdown_for(std::chrono::milliseconds timeout) -> result<bool>
  {
    if (!impl_)
      return true;
    return try_result([&]() -> result<bool> { return impl_->shutdown_for(timeout); });
  }

  [[nodiscard]] auto
  size() const noexcept -> std::size_t
  {
    return impl_ ? impl_->size() : 0;
  }

  [[nodiscard]] auto
  pending_tasks() const -> std::size_t
  {
    return impl_ ? impl_->pending_tasks() : 0;
  }

  [[nodiscard]] auto
  is_current_worker() const noexcept -> bool
  {
    return impl_ && impl_->is_current_worker();
  }

  [[nodiscard]] auto
  get_statistics() const -> pool_statistics
  {
    if (!impl_)
      return {};
    auto const source = impl_->get_statistics();
    pool_statistics result_value{
      source.total_threads,    source.active_threads, source.pending_tasks, source.completed_tasks, 0,
      source.tasks_per_second, source.avg_task_time
    };
    if constexpr (has_stolen_tasks<decltype(source)>::value)
      result_value.stolen_tasks = source.stolen_tasks;
    return result_value;
  }

protected:
  ~submitting_pool_facade()
  {
    dispose_pool_backend(impl_);
  }

private:
  std::unique_ptr<Backend> impl_;
};

template <typename Backend>
class lightweight_pool_facade
{
public:
  explicit lightweight_pool_facade(worker_count count, worker_registration registration)
      : impl_(make_pool_backend<Backend>(count, registration))
  {
  }

  lightweight_pool_facade(lightweight_pool_facade const&) = delete;
  auto operator=(lightweight_pool_facade const&) -> lightweight_pool_facade& = delete;
  lightweight_pool_facade(lightweight_pool_facade&& other) noexcept : impl_(std::move(other.impl_)) {}
  auto
  operator=(lightweight_pool_facade&& other) noexcept -> lightweight_pool_facade&
  {
    if (this != &other)
      {
        dispose_pool_backend(impl_);
        impl_ = std::move(other.impl_);
      }
    return *this;
  }

  template <typename F, typename... Args>
  auto
  post(F&& function, Args&&... args) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result([&]() -> result<void>
                        { return impl_->try_post(std::forward<F>(function), std::forward<Args>(args)...); });
  }

  template <typename F, typename... Args>
  void
  post_or_throw(F&& function, Args&&... args)
  {
    auto posted = post(std::forward<F>(function), std::forward<Args>(args)...);
    if (!posted)
      throw std::system_error(posted.error(), "advanced lightweight pool post");
  }

  auto
  configure_workers(thread_config const& config) -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result([&]() -> result<void> { return impl_->configure_threads(to_native(config)); });
  }

  auto
  distribute_workers() -> result<void>
  {
    if (!impl_)
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return try_result([&]() -> result<void> { return impl_->distribute_across_cpus(); });
  }

  auto
  shutdown(shutdown_policy policy = shutdown_policy::drain) -> result<void>
  {
    if (!impl_)
      return {};
    return try_result(
        [&]() -> result<void>
          {
            impl_->shutdown(to_native(policy));
            return {};
          });
  }

  auto
  shutdown_for(std::chrono::milliseconds timeout) -> result<bool>
  {
    if (!impl_)
      return true;
    return try_result([&]() -> result<bool> { return impl_->shutdown_for(timeout); });
  }

  [[nodiscard]] auto
  size() const noexcept -> std::size_t
  {
    return impl_ ? impl_->size() : 0;
  }

  [[nodiscard]] auto
  is_current_worker() const noexcept -> bool
  {
    return impl_ && impl_->is_current_worker();
  }

protected:
  ~lightweight_pool_facade()
  {
    dispose_pool_backend(impl_);
  }

private:
  std::unique_ptr<Backend> impl_;
};

} // namespace threadschedule::detail
