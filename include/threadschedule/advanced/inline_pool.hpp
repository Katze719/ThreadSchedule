#pragma once

#include "../detail/callable/bind.hpp"
#include "../detail/pool/inline_pool_backend.hpp"
#include "../detail/pool/shutdown.hpp"
#include "../detail/pool/worker_context_guard.hpp"
#include "../detail/try_result.hpp"
#include "../result.hpp"
#include "../shutdown_policy.hpp"

#include <cstddef>
#include <future>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace threadschedule::advanced
{
class inline_pool final
{
public:
  inline_pool() = default;

  template <typename F, typename... Args>
  auto
  submit(F&& function, Args&&... args) -> result<std::future<detail::bind_result_t<F, Args...>>>
  {
    return impl_.try_submit(std::forward<F>(function), std::forward<Args>(args)...);
  }

  template <typename F, typename... Args>
  auto
  submit_or_throw(F&& function, Args&&... args) -> std::future<detail::bind_result_t<F, Args...>>
  {
    auto submitted = submit(std::forward<F>(function), std::forward<Args>(args)...);
    if (!submitted)
      throw std::system_error(submitted.error(), "inline_pool::submit");
    return std::move(*submitted);
  }

  template <typename F, typename... Args>
  auto
  post(F&& function, Args&&... args) -> result<void>
  {
    return impl_.try_post(std::forward<F>(function), std::forward<Args>(args)...);
  }

  template <typename F, typename... Args>
  void
  post_or_throw(F&& function, Args&&... args)
  {
    auto posted = post(std::forward<F>(function), std::forward<Args>(args)...);
    if (!posted)
      throw std::system_error(posted.error(), "inline_pool::post");
  }

  template <typename Iterator>
  auto
  submit_batch(Iterator begin, Iterator end) -> result<std::vector<std::future<void>>>
  {
    return impl_.try_submit_batch(begin, end);
  }

  template <typename Iterator>
  auto
  submit_batch_or_throw(Iterator begin, Iterator end) -> std::vector<std::future<void>>
  {
    auto submitted = submit_batch(begin, end);
    if (!submitted)
      throw std::system_error(submitted.error(), "inline_pool::submit_batch");
    return std::move(*submitted);
  }

  template <typename Iterator, typename Function>
  auto
  parallel_for_each(Iterator begin, Iterator end, Function&& function) -> result<void>
  {
    static_assert(detail::is_forward_iterator_v<Iterator>, "parallel_for_each requires at least a forward iterator");
    return detail::try_result(
        [&]() -> result<void>
          {
            impl_.parallel_for_each(begin, end, std::forward<Function>(function));
            return {};
          });
  }

  auto
  wait() -> result<void>
  {
    impl_.wait_for_tasks();
    return {};
  }

  auto
  shutdown(shutdown_policy policy = shutdown_policy::drain) -> result<void>
  {
    impl_.shutdown(detail::to_native(policy));
    return {};
  }

  [[nodiscard]] auto
  size() const noexcept -> std::size_t
  {
    return 0;
  }

  [[nodiscard]] auto
  pending_tasks() const noexcept -> std::size_t
  {
    return 0;
  }

private:
  ::threadschedule::detail::inline_pool_backend impl_;
};
} // namespace threadschedule::advanced
