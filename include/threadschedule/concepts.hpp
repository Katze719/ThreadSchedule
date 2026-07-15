#pragma once

/**
 * @file concepts.hpp
 * @brief C++17 type traits and SFINAE helpers for the threading library.
 *
 * Provides standard-independent constexpr traits used throughout the library
 * to enforce correct template arguments.
 *
 * Also defines the `is_thread_like<T>` trait hierarchy for generic thread
 * handle dispatch.
 */

#include <chrono>
#include <functional>
#include <set>
#include <thread>
#include <type_traits>
#include <vector>

namespace threadschedule
{

/**
 * @brief SFINAE trait that detects `std::chrono::duration` types.
 *
 * Yields `std::true_type` when @p T exposes nested `rep` and `period`
 * type aliases (the signature of any `std::chrono::duration` instantiation).
 * The primary template is `std::false_type`; the partial specialization
 * using `std::void_t` matches duration-like types.
 *
 * @tparam T The type to test.
 */
template <typename T, typename = void>
struct is_duration_impl : std::false_type
{
};

/** @copydoc is_duration_impl */
template <typename T>
struct is_duration_impl<T, std::void_t<typename T::rep, typename T::period>>
    : std::true_type
{
};

/** @brief True when @p F can be invoked with @p Args. */
template <typename F, typename... Args>
constexpr bool thread_callable = std::is_invocable_v<F, Args...>;

/** @brief Pre-C++20 fallback for thread_identifiable (constexpr bool). */
template <typename T>
constexpr bool thread_identifiable
    = std::is_same_v<decltype(std::declval<T>().get_id()), std::thread::id>;

/** @brief Pre-C++20 fallback for duration (constexpr bool). */
template <typename T>
constexpr bool duration = is_duration_impl<T>::value;

/** @brief Pre-C++20 fallback for priority_type (constexpr bool). */
template <typename T>
constexpr bool priority_type = std::is_integral_v<T>;

/** @brief Pre-C++20 fallback for cpu_set_type (constexpr bool). */
template <typename T>
constexpr bool cpu_set_type
    = std::is_same_v<T, std::vector<int>> || std::is_same_v<T, std::set<int>>;

/**
 * @brief Type trait that identifies thread-like types.
 *
 * The primary template yields `std::false_type`. Explicit specializations
 * are provided for `std::thread`. Additional specializations for library types
 * such as `detail::thread_backend` are defined in `profiles.hpp`.
 *
 * Used by `apply_profile()` and other generic scheduling functions to
 * accept any thread-like handle uniformly.
 *
 * @tparam T The type to test.
 *
 * @par Helper variable
 * `is_thread_like_v<T>` is a convenience `inline constexpr bool`.
 */
template <typename T>
struct is_thread_like : std::false_type
{
};

/** @brief `std::thread` is a thread-like type. */
template <>
struct is_thread_like<std::thread> : std::true_type
{
};

/** @brief Convenience variable template for is_thread_like. */
template <typename T>
inline constexpr bool is_thread_like_v = is_thread_like<T>::value;

/**
 * @brief Helper type traits for thread operations
 */
template <typename T>
using enable_if_thread_callable_t
    = std::enable_if_t<std::is_invocable_v<T>, int>;

template <typename T>
using enable_if_duration_t = std::enable_if_t<is_duration_impl<T>::value, int>;

} // namespace threadschedule
