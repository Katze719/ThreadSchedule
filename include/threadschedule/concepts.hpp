#pragma once

/**
 * @file concepts.hpp
 * @brief C++20 concepts, type traits, and SFINAE helpers for the threading library.
 *
 * Provides compile-time constraints (`ThreadCallable`, `ThreadIdentifiable`,
 * `Duration`, `PriorityType`, `CPUSetType`) used throughout the library to
 * enforce correct template arguments. When C++20 concepts are unavailable,
 * equivalent `constexpr bool` variables are defined as fallbacks.
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
struct is_duration_impl<T, std::void_t<typename T::rep, typename T::period>> : std::true_type
{
};

// C++20 concepts (with constexpr-bool fallbacks for older compilers)
#if __cpp_concepts >= 201907L

/**
 * @brief Constrains @p F to be invocable with @p Args.
 *
 * Use in template parameter lists to restrict thread-entry functions or
 * callbacks to types that are actually callable with the given arguments.
 */
template <typename F, typename... Args>
concept ThreadCallable = std::is_invocable_v<F, Args...>;

/**
 * @brief Constrains @p T to types that expose a thread identity via
 *        `get_id()` returning something convertible to `std::thread::id`.
 *
 * Satisfied by `std::thread`, `std::jthread`, and `ThreadWrapper`.
 */
template <typename T>
concept ThreadIdentifiable = requires(T t) {
    { t.get_id() } -> std::convertible_to<std::thread::id>;
};

/**
 * @brief Constrains @p T to `std::chrono::duration`-like types (those
 *        exposing `rep` and `period` nested types).
 *
 * Use for timeout / interval parameters in scheduling APIs.
 */
template <typename T>
concept Duration = requires {
    typename T::rep;
    typename T::period;
};

/**
 * @brief Constrains @p T to integral types suitable for representing
 *        thread priorities.
 */
template <typename T>
concept PriorityType = std::is_integral_v<T>;

/**
 * @brief Constrains @p T to container-like types that can represent a set
 *        of CPU indices (must provide `size()`, `begin()`, `end()`).
 */
template <typename T>
concept CPUSetType = requires(T t) {
    { t.size() } -> std::convertible_to<std::size_t>;
    { t.begin() };
    { t.end() };
};

#else

/**
 * @brief Pre-C++20 fallback for ThreadCallable (constexpr bool).
 * @see ThreadCallable concept above.
 */
template <typename F, typename... Args>
constexpr bool ThreadCallable = std::is_invocable_v<F, Args...>;

/** @brief Pre-C++20 fallback for ThreadIdentifiable (constexpr bool). */
template <typename T>
constexpr bool ThreadIdentifiable = std::is_same_v<decltype(std::declval<T>().get_id()), std::thread::id>;

/** @brief Pre-C++20 fallback for Duration (constexpr bool). */
template <typename T>
constexpr bool Duration = is_duration_impl<T>::value;

/** @brief Pre-C++20 fallback for PriorityType (constexpr bool). */
template <typename T>
constexpr bool PriorityType = std::is_integral_v<T>;

/** @brief Pre-C++20 fallback for CPUSetType (constexpr bool). */
template <typename T>
constexpr bool CPUSetType = std::is_same_v<T, std::vector<int>> || std::is_same_v<T, std::set<int>>;

#endif

/**
 * @brief Type trait that identifies thread-like types.
 *
 * The primary template yields `std::false_type`. Explicit specializations
 * are provided for `std::thread` and (when C++20 is available)
 * `std::jthread`. Additional specializations for library types such as
 * `ThreadWrapper` are defined in `profiles.hpp`.
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

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
/** @brief `std::jthread` is a thread-like type (C++20). */
template <>
struct is_thread_like<std::jthread> : std::true_type
{
};
#endif

/** @brief Convenience variable template for is_thread_like. */
template <typename T>
inline constexpr bool is_thread_like_v = is_thread_like<T>::value;

/**
 * @brief Helper type traits for thread operations
 */
template <typename T>
using enable_if_thread_callable_t = std::enable_if_t<std::is_invocable_v<T>, int>;

template <typename T>
using enable_if_duration_t = std::enable_if_t<is_duration_impl<T>::value, int>;

} // namespace threadschedule
