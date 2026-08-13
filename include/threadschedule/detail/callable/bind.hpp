#pragma once

/**
 * @file detail/callable/bind.hpp
 * @brief C++17 callable and argument binding with move-only support.
 */

#include <tuple>
#include <utility>

namespace threadschedule::detail
{

template <typename F, typename... Args>
auto
bind_args(F&& function, Args&&... args)
{
  return [callable = std::forward<F>(function), arguments = std::make_tuple(std::forward<Args>(args)...)]() mutable
    { return std::apply(std::move(callable), std::move(arguments)); };
}

} // namespace threadschedule::detail
