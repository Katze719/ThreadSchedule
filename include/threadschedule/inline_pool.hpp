#pragma once

/**
 * @file inline_pool.hpp
 * @brief InlinePool: deterministic, single-threaded pool for unit testing.
 *
 * Executes every task synchronously on the calling thread. Has the same
 * submit/post/try_submit/try_post API surface as ThreadPool so it can be
 * used as a drop-in template argument in generic code that is
 * parameterized on pool type.
 */

#include "expected.hpp"
#include "thread_pool.hpp"
#include <functional>
#include <future>
#include <system_error>
#include <type_traits>
#include <vector>

namespace threadschedule
{

/**
 * @brief A pool that executes every task inline on the calling thread.
 *
 * Useful for deterministic unit testing: tasks run synchronously in
 * submission order with no concurrency, making results fully
 * reproducible and debuggable.
 *
 * @par API compatibility
 * InlinePool provides the same submit/try_submit/post/try_post/
 * submit_batch/parallel_for_each surface as @ref ThreadPool. The
 * returned futures are always already fulfilled when submit() returns.
 *
 * @par Limitations
 * - size() always returns 0 (no worker threads).
 * - shutdown() and wait_for_tasks() are no-ops.
 * - There is no concurrency; tasks that block or deadlock will block
 *   the submitting thread.
 */
class InlinePool
{
  public:
    explicit InlinePool(size_t /*num_threads*/ = 0)
    {
    }

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        auto result = try_submit(std::forward<F>(f), std::forward<Args>(args)...);
        if (!result.has_value())
            throw std::runtime_error("InlinePool is shut down");
        return std::move(result.value());
    }

    template <typename F, typename... Args>
    auto try_submit(F&& f, Args&&... args) -> expected<std::future<std::invoke_result_t<F, Args...>>, std::error_code>
    {
        using R = std::invoke_result_t<F, Args...>;
        if (stop_)
            return unexpected(std::make_error_code(std::errc::operation_canceled));

        std::promise<R> p;
        auto future = p.get_future();
        try
        {
            if constexpr (std::is_void_v<R>)
            {
                std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
                p.set_value();
            }
            else
            {
                p.set_value(std::invoke(std::forward<F>(f), std::forward<Args>(args)...));
            }
        }
        catch (...)
        {
            p.set_exception(std::current_exception());
        }
        return future;
    }

    template <typename F, typename... Args>
    void post(F&& f, Args&&... args)
    {
        auto r = try_post(std::forward<F>(f), std::forward<Args>(args)...);
        if (!r.has_value())
            throw std::runtime_error("InlinePool is shut down");
    }

    template <typename F, typename... Args>
    auto try_post(F&& f, Args&&... args) -> expected<void, std::error_code>
    {
        if (stop_)
            return unexpected(std::make_error_code(std::errc::operation_canceled));
        try
        {
            std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
        }
        catch (...)
        {
        }
        return {};
    }

    template <typename Iterator>
    auto try_submit_batch(Iterator begin, Iterator end) -> expected<std::vector<std::future<void>>, std::error_code>
    {
        if (stop_)
            return unexpected(std::make_error_code(std::errc::operation_canceled));

        std::vector<std::future<void>> futures;
        for (auto it = begin; it != end; ++it)
        {
            std::promise<void> p;
            futures.push_back(p.get_future());
            try
            {
                (*it)();
                p.set_value();
            }
            catch (...)
            {
                p.set_exception(std::current_exception());
            }
        }
        return futures;
    }

    template <typename Iterator>
    auto submit_batch(Iterator begin, Iterator end) -> std::vector<std::future<void>>
    {
        auto result = try_submit_batch(begin, end);
        if (!result.has_value())
            throw std::runtime_error("InlinePool is shut down");
        return std::move(result.value());
    }

    template <typename Iterator, typename F>
    void parallel_for_each(Iterator begin, Iterator end, F&& func)
    {
        for (auto it = begin; it != end; ++it)
            func(*it);
    }

    [[nodiscard]] auto size() const noexcept -> size_t { return 0; }
    [[nodiscard]] auto pending_tasks() const noexcept -> size_t { return 0; }

    void wait_for_tasks() {}
    void shutdown(ShutdownPolicy /*policy*/ = ShutdownPolicy::drain) { stop_ = true; }

  private:
    bool stop_{false};
};

} // namespace threadschedule
