#pragma once

/**
 * @file futures.hpp
 * @brief Combinators for @c std::future: @c when_all, @c when_any,
 *        @c when_all_settled.
 *
 * These utilities simplify waiting on multiple futures produced by thread
 * pool submissions.
 */

#include "expected.hpp"

#include <chrono>
#include <exception>
#include <future>
#include <utility>
#include <vector>

namespace threadschedule
{

/**
 * @brief Block until all futures complete, returning results in submission order.
 *
 * If any future throws, the first exception is captured and re-thrown after
 * all remaining futures have been waited on (to avoid leaving them dangling).
 *
 * @tparam T The value type of each future.
 * @param futures A vector of futures to wait on. Moved-from on return.
 * @return A vector of values in the same order as the input futures.
 */
template <typename T>
auto when_all(std::vector<std::future<T>>& futures) -> std::vector<T>
{
    std::vector<T> results;
    results.reserve(futures.size());
    std::exception_ptr first_error;

    for (auto& f : futures)
    {
        try
        {
            results.push_back(f.get());
        }
        catch (...)
        {
            if (!first_error)
                first_error = std::current_exception();
            results.emplace_back();
        }
    }

    if (first_error)
        std::rethrow_exception(first_error);

    return results;
}

/**
 * @brief Block until all void futures complete.
 *
 * Re-throws the first exception after all futures have been waited on.
 */
inline void when_all(std::vector<std::future<void>>& futures)
{
    std::exception_ptr first_error;

    for (auto& f : futures)
    {
        try
        {
            f.get();
        }
        catch (...)
        {
            if (!first_error)
                first_error = std::current_exception();
        }
    }

    if (first_error)
        std::rethrow_exception(first_error);
}

/**
 * @brief Block until all futures complete, returning an @c expected per slot.
 *
 * Never throws. Each slot is either the result value or the captured
 * @c std::exception_ptr.
 *
 * @tparam T The value type of each future.
 */
template <typename T>
auto when_all_settled(std::vector<std::future<T>>& futures)
    -> std::vector<expected<T, std::exception_ptr>>
{
    std::vector<expected<T, std::exception_ptr>> results;
    results.reserve(futures.size());

    for (auto& f : futures)
    {
        try
        {
            results.push_back(f.get());
        }
        catch (...)
        {
            results.push_back(unexpected(std::current_exception()));
        }
    }

    return results;
}

/**
 * @brief Block until all void futures complete, returning an @c expected per slot.
 */
inline auto when_all_settled(std::vector<std::future<void>>& futures)
    -> std::vector<expected<void, std::exception_ptr>>
{
    std::vector<expected<void, std::exception_ptr>> results;
    results.reserve(futures.size());

    for (auto& f : futures)
    {
        try
        {
            f.get();
            results.emplace_back();
        }
        catch (...)
        {
            results.push_back(unexpected(std::current_exception()));
        }
    }

    return results;
}

/**
 * @brief Block until the first future becomes ready.
 *
 * Polls all futures round-robin with a 1 ms timeout until one is ready,
 * then returns its index and value.
 *
 * @note The remaining futures are left in their current state -- the caller
 *       is responsible for managing their lifetime.
 *
 * @tparam T The value type of each future.
 * @return A pair of (index of the first ready future, its value).
 */
template <typename T>
auto when_any(std::vector<std::future<T>>& futures) -> std::pair<size_t, T>
{
    while (true)
    {
        for (size_t i = 0; i < futures.size(); ++i)
        {
            if (futures[i].wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
                return {i, futures[i].get()};
        }
    }
}

/**
 * @brief Block until the first void future becomes ready.
 *
 * @return The index of the first ready future.
 */
inline auto when_any(std::vector<std::future<void>>& futures) -> size_t
{
    while (true)
    {
        for (size_t i = 0; i < futures.size(); ++i)
        {
            if (futures[i].wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
            {
                futures[i].get();
                return i;
            }
        }
    }
}

} // namespace threadschedule
