#pragma once

#include <chrono>
#include <functional>
#include <set>
#include <thread>
#include <type_traits>
#include <vector>

namespace threadschedule
{

// Custom duration trait for compatibility across all C++ versions
template <typename T, typename = void>
struct is_duration_impl : std::false_type
{
};

template <typename T>
struct is_duration_impl<T, std::void_t<typename T::rep, typename T::period>> : std::true_type
{
};

// C++23 concepts (with fallbacks for older compilers)
#if __cpp_concepts >= 201907L

/**
 * @brief Concept for callable objects that can be executed by threads
 */
template <typename F, typename... Args>
concept ThreadCallable = std::is_invocable_v<F, Args...>;

/**
 * @brief Concept for types that can be used as thread identifiers
 */
template <typename T>
concept ThreadIdentifiable = requires(T t) {
    { t.get_id() } -> std::convertible_to<std::thread::id>;
};

/**
 * @brief Concept for duration types used in thread operations
 */
template <typename T>
concept Duration = requires {
    typename T::rep;
    typename T::period;
};

/**
 * @brief Concept for types that can represent thread priorities
 */
template <typename T>
concept PriorityType = std::is_integral_v<T>;

/**
 * @brief Concept for CPU set types
 */
template <typename T>
concept CPUSetType = requires(T t) {
    { t.size() } -> std::convertible_to<std::size_t>;
    { t.begin() };
    { t.end() };
};

#else

// Fallback using SFINAE for older compilers
template <typename F, typename... Args>
constexpr bool ThreadCallable = std::is_invocable_v<F, Args...>;

template <typename T>
constexpr bool ThreadIdentifiable = std::is_same_v<decltype(std::declval<T>().get_id()), std::thread::id>;

template <typename T>
constexpr bool Duration = is_duration_impl<T>::value;

template <typename T>
constexpr bool PriorityType = std::is_integral_v<T>;

// For CPU set types, we'll use a simple trait
template <typename T>
constexpr bool CPUSetType = std::is_same_v<T, std::vector<int>> || std::is_same_v<T, std::set<int>>;

#endif

/**
 * @brief Type trait for thread-like objects
 */
template <typename T>
struct is_thread_like : std::false_type
{
};

template <>
struct is_thread_like<std::thread> : std::true_type
{
};

// Only include jthread if C++20 is available
#if __cplusplus >= 202002L
template <>
struct is_thread_like<std::jthread> : std::true_type
{
};
#endif

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
