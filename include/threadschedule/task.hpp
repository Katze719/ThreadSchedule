#pragma once

/**
 * @file task.hpp
 * @brief Lazy single-value coroutine (`task<T>`) and blocking bridge (`sync_wait`).
 *
 * A `task<T>` represents a lazy coroutine that produces exactly one value
 * (or throws). It does not begin execution until it is `co_await`ed by
 * another coroutine or passed to `sync_wait()`.
 *
 * Requires C++20 coroutine support.
 */

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

#include <atomic>
#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace threadschedule
{

template <typename T = void>
class task;

namespace detail
{

struct final_awaiter
{
    [[nodiscard]] auto await_ready() const noexcept -> bool
    {
        return false;
    }

    template <typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> h) const noexcept -> std::coroutine_handle<>
    {
        if (auto cont = h.promise().continuation_; cont)
            return cont;
        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }
};

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

  protected:
    std::exception_ptr exception_{};
};

} // namespace detail

// ── task<T> (non-void) ─────────────────────────────────────────────

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

// ── task<void> ──────────────────────────────────────────────────────

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

// ── sync_wait ───────────────────────────────────────────────────────

namespace detail
{

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
 * @brief Block the current thread until a task<T> completes and return its result.
 *
 * This is the primary bridge between coroutine and synchronous code.
 * The task is resumed on the calling thread.
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
 * @brief Block the current thread until a task<void> completes.
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

} // namespace threadschedule

#endif // __cpp_impl_coroutine
