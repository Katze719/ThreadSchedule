#pragma once

/**
 * @file task_group.hpp
 * @brief Structured concurrency via @c task_group.
 *
 * A @c task_group ties a set of tasks to a scope: all submitted tasks,
 * including child tasks submitted to the same group by tracked tasks, are
 * guaranteed to complete before @c wait() returns (or the destructor runs).
 * This eliminates dangling-future bugs and makes exception propagation
 * deterministic.
 */

#include "../result.hpp"

#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace threadschedule::detail
{
template <typename Pool, typename = void>
struct task_group_has_current_worker_query : std::false_type
{
};

template <typename Pool>
struct task_group_has_current_worker_query<Pool, std::void_t<decltype(std::declval<Pool const&>().is_current_worker())>>
    : std::true_type
{
};

template <typename Pool>
[[nodiscard]] auto
task_group_is_current_worker(Pool const& pool) noexcept -> bool
{
  if constexpr (task_group_has_current_worker_query<Pool>::value)
    return pool.is_current_worker();
  return false;
}
} // namespace threadschedule::detail

namespace threadschedule::advanced
{

/**
 * @brief Scoped task group that ensures all submitted work completes
 *        before the group is destroyed.
 *
 * @par Usage
 * @code
 * raw_thread_pool pool(worker_count{4});
 * {
 *     task_group group(pool);
 *     group.submit([]{ do_work_a(); });
 *     group.submit([]{ do_work_b(); });
 *     group.wait();  // blocks until both complete
 * }
 * @endcode
 *
 * @par Exception handling
 * If any task throws, the first captured exception is rethrown from
 * @c wait(). All remaining tasks still run to completion.
 *
 * @par Destructor
 * The destructor calls @c wait() if it has not been called already,
 * ensuring that tasks never outlive the group. Note: if the destructor
 * must wait for slow tasks, it will block.
 * Tasks submitted recursively by a task already running in this group are
 * executed inline. This preserves progress when every pool worker is occupied
 * by a parent task waiting for its grouped children.
 *
 * @tparam Pool Thread pool type (must support @c submit(Callable)).
 */
template <typename Pool>
class task_group
{
  struct context_token
  {
  };

public:
  explicit task_group(Pool& pool) : pool_(pool), token_(std::make_shared<context_token>()) {}

  task_group(task_group const&) = delete;
  auto operator=(task_group const&) -> task_group& = delete;

  ~task_group()
  {
    try
      {
        wait();
      }
    catch (...)
      {
      }
  }

  /**
   * @brief Submit a void() callable to the group.
   *
   * The returned future is tracked internally; you do not need to
   * store it yourself.
   */
  template <typename F>
  auto
  submit(F&& f)
  {
    using function_type = std::decay_t<F>;
    auto token = token_;
    auto grouped = [token, function = function_type(std::forward<F>(f))]() mutable
      {
        context_guard guard(current_group_, token.get());
        std::invoke(std::move(function));
      };

    if (current_group_ == token.get() || ::threadschedule::detail::task_group_is_current_worker(pool_))
      return run_inline(std::move(grouped));
    return track(pool_.submit(std::move(grouped)));
  }

private:
  class context_guard
  {
  public:
    context_guard(context_token*& slot, context_token* current) noexcept : slot_(slot), previous_(slot)
    {
      slot_ = current;
    }

    ~context_guard()
    {
      slot_ = previous_;
    }

    context_guard(context_guard const&) = delete;
    auto operator=(context_guard const&) -> context_guard& = delete;

  private:
    context_token*& slot_;
    context_token* previous_;
  };

  template <typename F>
  auto
  run_inline(F&& function) -> result<void>
  {
    std::packaged_task<void()> task(std::forward<F>(function));
    auto future = task.get_future();
    task();
    return track(std::move(future));
  }

  auto
  track(std::future<void> future) -> result<void>
  {
    std::lock_guard<std::mutex> lock(mutex_);
    futures_.push_back(std::move(future));
    return {};
  }

  auto
  track(result<std::future<void>> submitted) -> result<void>
  {
    if (!submitted)
      return unexpected(submitted.error());
    return track(std::move(*submitted));
  }

public:
  /**
   * @brief Block until all submitted tasks complete.
   *
   * Tasks submitted to this group by a task currently being waited on are
   * included in the same wait operation.
   *
   * @throws Rethrows the first exception from any task. All tasks are
   *         still waited on even if one throws.
   */
  void
  wait()
  {
    if (current_group_ == token_.get())
      throw_worker_wait_error();

    std::exception_ptr first_error;
    while (true)
      {
        std::vector<std::future<void>> local;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          if (futures_.empty())
            break;
          local.swap(futures_);
        }

        for (auto& future : local)
          {
            try
              {
                future.get();
              }
            catch (...)
              {
                if (!first_error)
                  first_error = std::current_exception();
              }
          }
      }

    if (first_error)
      std::rethrow_exception(first_error);
  }

  /**
   * @brief Number of pending (not yet waited) tasks.
   */
  [[nodiscard]] auto
  pending() const -> size_t
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return futures_.size();
  }

private:
  [[noreturn]] static void
  throw_worker_wait_error()
  {
    throw std::system_error(std::make_error_code(std::errc::resource_deadlock_would_occur),
                            "task_group::wait from a tracked pool task");
  }

  inline static thread_local context_token* current_group_ = nullptr;
  Pool& pool_;
  std::shared_ptr<context_token> token_;
  mutable std::mutex mutex_;
  std::vector<std::future<void>> futures_;
};

} // namespace threadschedule::advanced
