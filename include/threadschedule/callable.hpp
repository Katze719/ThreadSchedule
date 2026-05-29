#pragma once

/**
 * @file callable.hpp
 * @brief Feature-gated callable storage aliases for modern C++ builds.
 */

#include <functional>
#include <type_traits>
#include <utility>

namespace threadschedule
{
namespace detail
{

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
template <typename Signature>
using move_callable = std::move_only_function<Signature>;
#else
template <typename Signature>
using move_callable = std::function<Signature>;
#endif

#if defined(__cpp_lib_copyable_function) && __cpp_lib_copyable_function >= 202306L
template <typename Signature>
using copyable_callable = std::copyable_function<Signature>;
#else
template <typename Signature>
using copyable_callable = std::function<Signature>;
#endif

template <typename Signature, typename Callable>
auto make_move_callable(Callable&& callable) -> move_callable<Signature>
{
    return move_callable<Signature>(std::forward<Callable>(callable));
}

template <typename Signature, typename Callable>
auto make_copyable_callable(Callable&& callable) -> copyable_callable<Signature>
{
    return copyable_callable<Signature>(std::forward<Callable>(callable));
}

} // namespace detail
} // namespace threadschedule
