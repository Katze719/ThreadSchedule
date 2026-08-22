#pragma once

/**
 * @file detail/callable/bind.hpp
 * @brief C++17 callable and argument binding with move-only support.
 */

#include <tuple>
#include <type_traits>
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

/**
 * @brief Result of invoking the decayed callable and arguments stored by
 *        @ref bind_args.
 *
 * Deriving the type from the binder itself keeps overload resolution aligned
 * with the actual one-shot invocation, including std::reference_wrapper
 * unwrapping performed by std::make_tuple.
 */
template <typename F, typename... Args>
using bind_result_t = std::invoke_result_t<decltype(bind_args(std::declval<F>(), std::declval<Args>()...))&>;

} // namespace threadschedule::detail
