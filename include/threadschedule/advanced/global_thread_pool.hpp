#pragma once

#include "../detail/callable/bind.hpp"
#include "../detail/pool/backend.hpp"
#include "../result.hpp"
#include "../worker_count.hpp"

#include <future>
#include <system_error>
#include <type_traits>
#include <utility>

namespace threadschedule::advanced
{
class global_thread_pool
{
public:
  static void
  init(worker_count count)
  {
    backend::init(count.resolve());
  }
  template <typename F, typename... Args>
  static auto
  submit(F&& function, Args&&... args) -> result<std::future<detail::bind_result_t<F, Args...>>>
  {
    return backend::try_submit(std::forward<F>(function), std::forward<Args>(args)...);
  }
  template <typename F, typename... Args>
  static auto
  submit_or_throw(F&& function, Args&&... args) -> std::future<detail::bind_result_t<F, Args...>>
  {
    auto submitted = submit(std::forward<F>(function), std::forward<Args>(args)...);
    if (!submitted)
      throw std::system_error(submitted.error(), "global_thread_pool::submit");
    return std::move(*submitted);
  }
  template <typename F, typename... Args>
  static auto
  post(F&& function, Args&&... args) -> result<void>
  {
    return backend::try_post(std::forward<F>(function), std::forward<Args>(args)...);
  }
  template <typename F, typename... Args>
  static void
  post_or_throw(F&& function, Args&&... args)
  {
    auto posted = post(std::forward<F>(function), std::forward<Args>(args)...);
    if (!posted)
      throw std::system_error(posted.error(), "global_thread_pool::post");
  }

private:
  using backend = ::threadschedule::detail::global_thread_pool_backend;
};
} // namespace threadschedule::advanced
