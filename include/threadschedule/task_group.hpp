#pragma once

/**
 * @file task_group.hpp
 * @brief Structured concurrency via @c task_group.
 *
 * A @c task_group ties a set of tasks to a scope: all submitted tasks
 * are guaranteed to complete before @c wait() returns (or the destructor
 * runs). This eliminates dangling-future bugs and makes exception
 * propagation deterministic.
 */

#include <exception>
#include <future>
#include <mutex>
#include <vector>

namespace threadschedule
{

/**
 * @brief Scoped task group that ensures all submitted work completes
 *        before the group is destroyed.
 *
 * @par Usage
 * @code
 * ThreadPool pool(4);
 * {
 *     task_group group(pool);
 *     group.submit([]{ do_work_a(); });
 *     group.submit([]{ do_work_b(); });
 *     group.wait();  // blocks until both complete
 * }
 * @endcode
 *
 * @par Exception handling
 * If any task throws, the first captured exception is rethrown from
 * @c wait(). All remaining tasks still run to completion.
 *
 * @par Destructor
 * The destructor calls @c wait() if it has not been called already,
 * ensuring that tasks never outlive the group. Note: if the destructor
 * must wait for slow tasks, it will block.
 *
 * @tparam Pool Thread pool type (must support @c submit(Callable)).
 */
template <typename Pool>
class task_group
{
  public:
    explicit task_group(Pool& pool) : pool_(pool) {}

    task_group(task_group const&) = delete;
    auto operator=(task_group const&) -> task_group& = delete;

    ~task_group()
    {
        try
        {
            wait();
        }
        catch (...)
        {
        }
    }

    /**
     * @brief Submit a void() callable to the group.
     *
     * The returned future is tracked internally; you do not need to
     * store it yourself.
     */
    template <typename F>
    void submit(F&& f)
    {
        auto future = pool_.submit(std::forward<F>(f));
        std::lock_guard<std::mutex> lock(mutex_);
        futures_.push_back(std::move(future));
    }

    /**
     * @brief Block until all submitted tasks complete.
     *
     * @throws Rethrows the first exception from any task. All tasks are
     *         still waited on even if one throws.
     */
    void wait()
    {
        std::vector<std::future<void>> local;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            local.swap(futures_);
        }

        std::exception_ptr first_error;
        for (auto& f : local)
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
     * @brief Number of pending (not yet waited) tasks.
     */
    [[nodiscard]] auto pending() const -> size_t
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return futures_.size();
    }

  private:
    Pool& pool_;
    mutable std::mutex mutex_;
    std::vector<std::future<void>> futures_;
};

} // namespace threadschedule
