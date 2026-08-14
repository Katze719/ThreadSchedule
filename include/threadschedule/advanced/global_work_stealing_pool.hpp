#pragma once

#include "../detail/pool/backend.hpp"

namespace threadschedule::advanced
{
class global_work_stealing_pool
{
public:
  static void
  init(std::size_t count)
  {
    backend::init(count);
  }
  template <typename F, typename... Args>
  static auto
  submit(F&& function, Args&&... args)
  {
    return backend::submit(std::forward<F>(function), std::forward<Args>(args)...);
  }
  template <typename F, typename... Args>
  static auto
  try_submit(F&& function, Args&&... args)
  {
    return backend::try_submit(std::forward<F>(function), std::forward<Args>(args)...);
  }
  template <typename F, typename... Args>
  static void
  post(F&& function, Args&&... args)
  {
    backend::post(std::forward<F>(function), std::forward<Args>(args)...);
  }
  template <typename F, typename... Args>
  static auto
  try_post(F&& function, Args&&... args)
  {
    return backend::try_post(std::forward<F>(function), std::forward<Args>(args)...);
  }

private:
  using backend = ::threadschedule::detail::global_work_stealing_pool_backend;
};
} // namespace threadschedule::advanced
