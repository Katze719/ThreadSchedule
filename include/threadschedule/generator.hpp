#pragma once

/**
 * @file generator.hpp
 * @brief Lazy multi-value coroutine (`generator<T>`).
 *
 * A `generator<T>` represents a lazy sequence that produces values
 * on-demand via `co_yield`. It is compatible with range-based for loops.
 *
 * When C++23 `std::generator` is available (`__cpp_lib_generator`), a
 * convenience alias is provided. Otherwise, the library ships its own
 * implementation.
 *
 * Requires C++20 coroutine support.
 */

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

#include <coroutine>
#include <exception>
#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>

#if defined(__has_include)
#    if __has_include(<version>)
#        include <version>
#    endif
#endif

#if defined(__cpp_lib_generator) && __cpp_lib_generator >= 202207L
#    include <generator>
#    define THREADSCHEDULE_HAS_STD_GENERATOR 1
#else
#    define THREADSCHEDULE_HAS_STD_GENERATOR 0
#endif

namespace threadschedule
{

#if THREADSCHEDULE_HAS_STD_GENERATOR

template <typename T>
using generator = std::generator<T>;

#else

/**
 * @brief Lazy, multi-value coroutine that produces a sequence of @p T
 *        values on demand via `co_yield`.
 *
 * @tparam T The element type yielded by the coroutine body.
 *
 * `generator<T>` is the coroutine return type for functions that
 * lazily produce a stream of values. It is compatible with range-based
 * `for` loops thanks to its `begin()` / `end()` interface.
 *
 * **Ownership semantics:**
 * - Move-only; copying is deleted.
 * - The destructor destroys the underlying coroutine frame.
 *
 * **Laziness:**
 * The coroutine body does not execute until `begin()` is called, which
 * resumes the coroutine once to produce the first value. Each subsequent
 * `operator++` on the iterator resumes the coroutine to produce the next
 * value.
 *
 * **Iteration model:**
 * - Input iterator only (single-pass).
 * - `end()` returns `std::default_sentinel_t`; comparison with the
 *   iterator checks whether the coroutine is done.
 * - If the coroutine body throws, the exception is re-thrown on the
 *   next iterator increment (or on `begin()` if the first resumption
 *   throws).
 *
 * When C++23 `std::generator` is available (`__cpp_lib_generator >= 202207L`),
 * this class is replaced by a type alias to `std::generator<T>`.
 *
 * Requires C++20 coroutine support (`__cpp_impl_coroutine >= 201902L`).
 *
 * @par Example
 * @code
 * generator<int> iota(int n) {
 *     for (int i = 0; i < n; ++i)
 *         co_yield i;
 * }
 *
 * for (int v : iota(5))
 *     std::cout << v << '\n';
 * @endcode
 */
template <typename T>
class generator
{
  public:
    struct promise_type
    {
        auto get_return_object() noexcept -> generator
        {
            return generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() noexcept -> std::suspend_always
        {
            return {};
        }

        auto final_suspend() noexcept -> std::suspend_always
        {
            return {};
        }

        auto yield_value(T const& value) -> std::suspend_always
        {
            value_.emplace(value);
            return {};
        }

        auto yield_value(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) -> std::suspend_always
        {
            value_.emplace(std::move(value));
            return {};
        }

        void return_void() noexcept
        {
        }

        void unhandled_exception()
        {
            exception_ = std::current_exception();
        }

        void rethrow_if_exception()
        {
            if (exception_)
                std::rethrow_exception(exception_);
        }

        std::optional<T> value_{};

      private:
        std::exception_ptr exception_{};
    };

    /**
     * @brief Input iterator that lazily drives a generator coroutine.
     *
     * Satisfies `std::input_iterator_tag`. Each call to `operator++`
     * resumes the coroutine to produce the next value. Dereferencing
     * returns a `T&` (reference to the value stored in the promise).
     *
     * Comparison with `std::default_sentinel_t` returns `true` when the
     * coroutine has finished (i.e. `coroutine_handle::done()` is true).
     */
    class iterator
    {
      public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using reference = T&;
        using pointer = T*;

        iterator() noexcept = default;

        explicit iterator(std::coroutine_handle<promise_type> h) noexcept : handle_(h)
        {
        }

        auto operator++() -> iterator&
        {
            handle_.resume();
            if (handle_.done())
            {
                handle_.promise().rethrow_if_exception();
                handle_ = nullptr;
            }
            return *this;
        }

        void operator++(int)
        {
            ++(*this);
        }

        [[nodiscard]] auto operator*() const -> T&
        {
            return *handle_.promise().value_;
        }

        [[nodiscard]] auto operator->() const -> T*
        {
            return std::addressof(*handle_.promise().value_);
        }

        [[nodiscard]] friend auto operator==(iterator const& it, std::default_sentinel_t) noexcept -> bool
        {
            return !it.handle_ || it.handle_.done();
        }

        [[nodiscard]] friend auto operator==(std::default_sentinel_t s, iterator const& it) noexcept -> bool
        {
            return it == s;
        }

        [[nodiscard]] friend auto operator!=(iterator const& it, std::default_sentinel_t s) noexcept -> bool
        {
            return !(it == s);
        }

        [[nodiscard]] friend auto operator!=(std::default_sentinel_t s, iterator const& it) noexcept -> bool
        {
            return !(it == s);
        }

      private:
        std::coroutine_handle<promise_type> handle_{};
    };

    generator() noexcept = default;

    generator(generator&& other) noexcept : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    auto operator=(generator&& other) noexcept -> generator&
    {
        if (this != &other)
        {
            destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~generator()
    {
        destroy();
    }

    generator(generator const&) = delete;
    auto operator=(generator const&) -> generator& = delete;

    [[nodiscard]] auto begin() -> iterator
    {
        if (handle_)
        {
            handle_.resume();
            if (handle_.done())
            {
                handle_.promise().rethrow_if_exception();
                return iterator{};
            }
        }
        return iterator{handle_};
    }

    [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t
    {
        return {};
    }

  private:
    explicit generator(std::coroutine_handle<promise_type> h) noexcept : handle_(h)
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

#endif // THREADSCHEDULE_HAS_STD_GENERATOR

} // namespace threadschedule

#endif // __cpp_impl_coroutine
