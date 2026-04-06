#pragma once

/**
 * @file task.hpp
 * @brief Coroutine @c task, @c sync_wait, and pool helpers @c schedule_on / @c run_on.
 *
 * @c task is lazy until @c co_await or @c sync_wait. For how work moves onto a
 * thread pool and what nested @c schedule_on does, see struct @c schedule_on and
 * function template @c run_on below (C++20 only).
 *
 * Requires C++20 coroutine support.
 */

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

#include <atomic>
#include <coroutine>
#include <exception>
#include <functional>
#include <future>
#include <optional>
#include <type_traits>
#include <utility>

namespace threadschedule
{

template <typename T = void>
class task;

/**
 * @brief Type-erased executor interface for pool-aware coroutines.
 *
 * Implementations schedule a coroutine handle for execution on a specific
 * executor (e.g. a thread pool).
 */
struct executor_base
{
    virtual void execute(std::coroutine_handle<>) = 0;
    virtual ~executor_base() = default;
};

/**
 * @brief Executor that dispatches coroutine resumption to a thread pool.
 *
 * @tparam Pool A thread pool type providing @c submit(Callable).
 */
template <typename Pool>
struct pool_executor : executor_base
{
    Pool& pool;
    explicit pool_executor(Pool& p) : pool(p) {}

    void execute(std::coroutine_handle<> h) override
    {
        pool.submit([h]() mutable { h.resume(); });
    }
};

namespace detail
{

/**
 * @brief Awaiter that resumes the parent coroutine (continuation) when a task completes.
 *
 * @internal This is an implementation detail of the task coroutine machinery.
 *
 * When a task's coroutine body finishes, `final_awaiter` is returned from
 * `final_suspend()`. If an executor is set on the promise, the continuation
 * is dispatched through it (e.g. resumed on a pool thread). Otherwise,
 * symmetric transfer is used for zero-overhead inline resumption.
 */
struct final_awaiter
{
    [[nodiscard]] auto await_ready() const noexcept -> bool
    {
        return false;
    }

    template <typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> h) const noexcept -> std::coroutine_handle<>
    {
        auto cont = h.promise().continuation_;
        if (h.promise().executor_ && cont)
        {
            h.promise().executor_->execute(cont);
            return std::noop_coroutine();
        }
        if (cont)
            return cont;
        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }
};

/**
 * @brief Shared promise logic for task<T> and task<void>.
 *
 * @internal This is an implementation detail; users should interact with
 * task<T> rather than its promise directly.
 *
 * @tparam T The value type produced by the task (may be `void`).
 *
 * Key behaviours:
 * - **Lazy start:** `initial_suspend()` returns `std::suspend_always`, so
 *   the coroutine does not begin until explicitly resumed (via `co_await`
 *   or `sync_wait()`).
 * - **Exception forwarding:** `unhandled_exception()` captures the active
 *   exception into an `std::exception_ptr`; the awaiter re-throws it when
 *   the caller retrieves the result.
 * - **Continuation:** `continuation_` is set by the task's awaiter just
 *   before resuming the task. `final_awaiter` uses it to return control
 *   to the parent coroutine.
 * - **Executor:** If @c executor_ is set on the promise, the continuation is
 *   dispatched through that executor instead of using symmetric transfer.
 */
template <typename T>
class task_promise_base
{
  public:
    task_promise_base() = default;

    auto initial_suspend() noexcept -> std::suspend_always
    {
        return {};
    }

    auto final_suspend() noexcept -> final_awaiter
    {
        return {};
    }

    void unhandled_exception() noexcept
    {
        exception_ = std::current_exception();
    }

    void rethrow_if_exception()
    {
        if (exception_)
            std::rethrow_exception(exception_);
    }

    std::coroutine_handle<> continuation_{};
    executor_base* executor_{nullptr};

  protected:
    std::exception_ptr exception_{};
};

} // namespace detail

// --- task<T> (non-void) ---

/**
 * @brief Lazy, single-value coroutine that produces a @p T on completion.
 *
 * @tparam T The type of the value produced by the coroutine body.
 *
 * A `task<T>` is the primary coroutine return type for asynchronous
 * operations that yield exactly one result (or throw). It models a
 * **lazy** coroutine: execution does not begin until the task is
 * `co_await`ed by another coroutine or passed to `sync_wait()`.
 *
 * **Ownership semantics:**
 * - Move-only; copying is deleted.
 * - The destructor destroys the underlying coroutine frame, so the task
 *   must outlive any in-progress `co_await` that references it.
 *
 * **Result retrieval:**
 * `co_await`ing a `task<T>` returns `T`. If the coroutine body threw an
 * exception, the exception is re-thrown at the `co_await` point (via
 * `promise_type::result()`).
 *
 * Requires C++20 coroutine support (`__cpp_impl_coroutine >= 201902L`).
 *
 * @par Example
 * @code
 * task<int> compute() { co_return 42; }
 *
 * task<void> caller() {
 *     int v = co_await compute(); // resumes compute, gets 42
 * }
 * @endcode
 */
template <typename T>
class task
{
  public:
    struct promise_type : detail::task_promise_base<T>
    {
        auto get_return_object() noexcept -> task
        {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            result_.emplace(std::move(value));
        }

        auto result() -> T
        {
            this->rethrow_if_exception();
            return std::move(*result_);
        }

      private:
        std::optional<T> result_{};
    };

    task() noexcept = default;

    task(task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    auto operator=(task&& other) noexcept -> task&
    {
        if (this != &other)
        {
            destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~task()
    {
        destroy();
    }

    task(task const&) = delete;
    auto operator=(task const&) -> task& = delete;

    [[nodiscard]] auto handle() const noexcept -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

    struct awaiter
    {
        std::coroutine_handle<promise_type> handle_;

        [[nodiscard]] auto await_ready() const noexcept -> bool
        {
            return false;
        }

        auto await_suspend(std::coroutine_handle<> continuation) noexcept -> std::coroutine_handle<>
        {
            handle_.promise().continuation_ = continuation;
            return handle_;
        }

        auto await_resume() -> T
        {
            return handle_.promise().result();
        }
    };

    auto operator co_await() const& noexcept -> awaiter
    {
        return awaiter{handle_};
    }

    auto operator co_await() const&& noexcept -> awaiter
    {
        return awaiter{handle_};
    }

  private:
    explicit task(std::coroutine_handle<promise_type> h) noexcept : handle_(h)
    {
    }

    void destroy()
    {
        if (handle_)
        {
            handle_.destroy();
            handle_ = nullptr;
        }
    }

    std::coroutine_handle<promise_type> handle_{};
};

// --- task<void> ---

/**
 * @brief Lazy, single-value coroutine specialization for operations that
 *        produce no result.
 *
 * This is the `void` specialization of task. It behaves identically to
 * `task<T>` except that `co_await`ing it yields no value and the promise
 * uses `return_void()` instead of `return_value()`.
 *
 * **Ownership semantics:**
 * - Move-only; copying is deleted.
 * - The destructor destroys the underlying coroutine frame, so the task
 *   must outlive any in-progress `co_await` that references it.
 *
 * If the coroutine body throws, the exception is re-thrown at the
 * `co_await` point.
 *
 * Requires C++20 coroutine support (`__cpp_impl_coroutine >= 201902L`).
 *
 * @par Example
 * @code
 * task<void> do_work() { co_return; }
 *
 * task<void> caller() {
 *     co_await do_work(); // resumes do_work, returns void
 * }
 * @endcode
 */
template <>
class task<void>
{
  public:
    struct promise_type : detail::task_promise_base<void>
    {
        auto get_return_object() noexcept -> task<void>
        {
            return task<void>{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void return_void() noexcept
        {
        }

        void result()
        {
            this->rethrow_if_exception();
        }
    };

    task() noexcept = default;

    task(task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    auto operator=(task&& other) noexcept -> task<void>&
    {
        if (this != &other)
        {
            destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~task()
    {
        destroy();
    }

    task(task const&) = delete;
    auto operator=(task const&) -> task<void>& = delete;

    [[nodiscard]] auto handle() const noexcept -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

    struct awaiter
    {
        std::coroutine_handle<promise_type> handle_;

        [[nodiscard]] auto await_ready() const noexcept -> bool
        {
            return false;
        }

        auto await_suspend(std::coroutine_handle<> continuation) noexcept -> std::coroutine_handle<>
        {
            handle_.promise().continuation_ = continuation;
            return handle_;
        }

        void await_resume()
        {
            handle_.promise().result();
        }
    };

    auto operator co_await() const& noexcept -> awaiter
    {
        return awaiter{handle_};
    }

    auto operator co_await() const&& noexcept -> awaiter
    {
        return awaiter{handle_};
    }

  private:
    explicit task(std::coroutine_handle<promise_type> h) noexcept : handle_(h)
    {
    }

    void destroy()
    {
        if (handle_)
        {
            handle_.destroy();
            handle_ = nullptr;
        }
    }

    std::coroutine_handle<promise_type> handle_{};
};

// --- sync_wait ---

namespace detail
{

/**
 * @brief Bridge coroutine used internally by `sync_wait()` to block until
 *        a task completes.
 *
 * @internal This is an implementation detail; use `sync_wait()` instead.
 *
 * `sync_wait_task` wraps a `task<T>` inside a coroutine whose
 * `final_suspend` signals completion via an `std::atomic<bool>` and
 * `notify_one()`.
 *
 * Typical usage (inside `sync_wait`):
 * -# Construct a `sync_wait_task` from a lambda that `co_await`s the
 *    user's task.
 * -# Call `start()` to resume the coroutine (runs on the calling thread).
 * -# Call `wait()` to block until `final_suspend` fires `notify_one`.
 * -# Call `rethrow()` to propagate any unhandled exception from the
 *    coroutine body.
 *
 * The class is move-only and non-copyable.
 */
class sync_wait_task
{
  public:
    struct promise_type
    {
        auto initial_suspend() noexcept -> std::suspend_always
        {
            return {};
        }

        auto final_suspend() noexcept
        {
            struct notifier
            {
                [[nodiscard]] auto await_ready() const noexcept -> bool
                {
                    return false;
                }

                void await_suspend(std::coroutine_handle<promise_type> h) const noexcept
                {
                    h.promise().finished_.store(true, std::memory_order_release);
                    h.promise().finished_.notify_one();
                }

                void await_resume() const noexcept
                {
                }
            };
            return notifier{};
        }

        auto get_return_object() -> sync_wait_task
        {
            return sync_wait_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void return_void() noexcept
        {
        }

        void unhandled_exception() noexcept
        {
            exception_ = std::current_exception();
        }

        std::atomic<bool> finished_{false};
        std::exception_ptr exception_{};
    };

    explicit sync_wait_task(std::coroutine_handle<promise_type> h) : handle_(h)
    {
    }

    sync_wait_task(sync_wait_task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    ~sync_wait_task()
    {
        if (handle_)
            handle_.destroy();
    }

    sync_wait_task(sync_wait_task const&) = delete;
    auto operator=(sync_wait_task const&) -> sync_wait_task& = delete;
    auto operator=(sync_wait_task&&) -> sync_wait_task& = delete;

    void start()
    {
        handle_.resume();
    }

    void wait()
    {
        handle_.promise().finished_.wait(false, std::memory_order_acquire);
    }

    void rethrow()
    {
        if (handle_.promise().exception_)
            std::rethrow_exception(handle_.promise().exception_);
    }

  private:
    std::coroutine_handle<promise_type> handle_;
};

} // namespace detail

/**
 * @brief Block the calling thread until a `task<T>` completes and return
 *        its result.
 *
 * This is the primary bridge between coroutine code and synchronous code.
 * The task is resumed **on the calling thread** -- no thread pool or
 * executor is involved.
 *
 * If the task's coroutine body throws an exception, `sync_wait`
 * re-throws it to the caller.
 *
 * @tparam T The value type produced by the task.
 * @param  t The task to run. Consumed by move.
 * @return   The value produced by the task's `co_return`.
 * @throws   Any exception thrown inside the task body.
 *
 * @par Example
 * @code
 * task<int> compute() { co_return 42; }
 * int main() { return sync_wait(compute()); }
 * @endcode
 */
template <typename T>
auto sync_wait(task<T> t) -> T
{
    std::optional<T> result;
    std::exception_ptr ex;

    auto wrapper = [&result, &ex](task<T> inner) -> detail::sync_wait_task {
        try
        {
            result.emplace(co_await inner);
        }
        catch (...)
        {
            ex = std::current_exception();
        }
    };

    auto sw = wrapper(std::move(t));
    sw.start();
    sw.wait();
    sw.rethrow();

    if (ex)
        std::rethrow_exception(ex);

    return std::move(*result);
}

/**
 * @brief Block the calling thread until a `task<void>` completes.
 *
 * Overload for void tasks. Behaves identically to the `task<T>` overload
 * but returns nothing.
 *
 * The task is resumed **on the calling thread** -- no thread pool or
 * executor is involved. If the task body throws, the exception is
 * re-thrown to the caller.
 *
 * @param t The void task to run. Consumed by move.
 * @throws  Any exception thrown inside the task body.
 */
inline void sync_wait(task<void> t)
{
    std::exception_ptr ex;

    auto wrapper = [&ex](task<void> inner) -> detail::sync_wait_task {
        try
        {
            co_await inner;
        }
        catch (...)
        {
            ex = std::current_exception();
        }
    };

    auto sw = wrapper(std::move(t));
    sw.start();
    sw.wait();
    sw.rethrow();

    if (ex)
        std::rethrow_exception(ex);
}

// ---------------------------------------------------------------------------
// schedule_on awaitable
// ---------------------------------------------------------------------------

/**
 * @brief Awaitable that continues the current coroutine on a thread pool worker.
 *
 * @tparam Pool Pool type providing @c submit(Callable) (for example
 *         HighPerformancePool, ThreadPool, FastThreadPool).
 *
 * @par Mechanism
 * The coroutine stays a single @c task frame. Nothing is split into separate
 * compiled "halves". @c co_await schedule_on{pool} does the following:
 * -# The coroutine suspends at the @c co_await.
 * -# @c await_suspend enqueues a job on @p pool that calls @c resume() on this
 *    coroutine handle.
 * -# A worker runs that job; @c resume() continues the coroutine on that
 *    thread, starting with the line after the @c co_await. Everything after
 *    that point runs on that worker until another explicit transfer.
 *
 * Code before the first @c co_await schedule_on{pool} runs on whatever thread
 * was already running the coroutine (for example @c main under @c sync_wait, or
 * a pool thread if you were already there). Code after runs on whichever worker
 * picked up the @c submit.
 *
 * @par Nested @c schedule_on
 * Each @c co_await schedule_on queues another @c submit(resume). With two
 * different pools, you first continue on a worker of the first pool, then on a
 * worker of the second. Nesting @c schedule_on on the same pool still uses
 * @c pool.submit each time: you may run on another worker of that pool. There
 * is no guarantee it is the same OS thread as before.
 *
 * @par Versus @c co_await on another @c task
 * Awaiting another @c task usually does not post to a pool; when the child
 * finishes, the parent is typically resumed on the same thread (symmetric
 * transfer / direct resume). Only @c co_await schedule_on (or similar)
 * explicitly pushes the continuation onto the pool queue.
 *
 * @par Example
 * @code
 * task<void> work(HighPerformancePool& pool) {
 *     co_await schedule_on{pool};
 *     // runs on a pool worker from here until the next transfer
 * }
 * @endcode
 */
template <typename Pool>
struct schedule_on
{
    Pool& pool;

    [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> h) const
    {
        pool.submit([h]() mutable { h.resume(); });
    }

    void await_resume() const noexcept {}
};

// ---------------------------------------------------------------------------
// run_on convenience
// ---------------------------------------------------------------------------

/**
 * @brief Run a callable that returns @c task<T> on a pool worker; return
 *        @c std::future for the result.
 *
 * @tparam Pool Pool type providing @c submit(Callable).
 * @tparam F Callable with signature returning @c task<T> (for some @c T).
 *
 * @par Behaviour
 * The callable is invoked on a worker thread. That worker calls @c sync_wait
 * on the @c task returned by the callable, so the coroutine body runs there.
 * Nested @c co_await on other @c task objects typically keeps resuming on that
 * same worker unless you @c co_await schedule_on to hand off again. The
 * @c std::future is fulfilled when @c sync_wait completes inside the worker.
 *
 * @par Example
 * @code
 * auto future = run_on(pool, []() -> task<int> { co_return 42; });
 * int v = future.get();
 * @endcode
 */
template <typename Pool, typename F>
auto run_on(Pool& pool, F&& coro_fn)
    -> std::future<decltype(sync_wait(std::declval<std::invoke_result_t<F>>()))>
{
    return pool.submit([fn = std::forward<F>(coro_fn)]() mutable {
        return sync_wait(fn());
    });
}

} // namespace threadschedule

#endif // __cpp_impl_coroutine
