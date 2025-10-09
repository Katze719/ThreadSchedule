#pragma once

#include <exception>
#include <functional>
#include <system_error>
#include <type_traits>
#include <utility>
#if (defined(__cplusplus) && __cplusplus >= 202302L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202302L)
#include <expected>
#define THREADSCHEDULE_HAS_STD_EXPECTED 1
#elif defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include <expected>
#define THREADSCHEDULE_HAS_STD_EXPECTED 1
#else
#define THREADSCHEDULE_HAS_STD_EXPECTED 0
#endif

// Exception handling control
// Automatically detects if exceptions are available using __cpp_exceptions
// When exceptions are disabled (e.g., with -fno-exceptions):
// - value() becomes a no-op if called on an error state
// - Use value_or(), operator*, or check has_value() before accessing the value
// - Compatible with exception-free builds for better performance
#ifdef __cpp_exceptions
#define THREADSCHEDULE_EXPECTED_THROW(ex) throw ex
#else
#define THREADSCHEDULE_EXPECTED_THROW(ex) ((void)0)
#endif

namespace threadschedule
{

#if THREADSCHEDULE_HAS_STD_EXPECTED
template <typename E>
using unexpected = std::unexpected<E>;
using unexpect_t = std::unexpect_t;
inline constexpr unexpect_t unexpect{};
template <typename E>
using bad_expected_access = std::bad_expected_access<E>;
template <typename T, typename E = std::error_code>
using expected = std::expected<T, E>;

#else

struct unexpect_t
{
    explicit unexpect_t() = default;
};
inline constexpr unexpect_t unexpect{};

template <typename E>
class bad_expected_access;

template <>
class bad_expected_access<void> : public std::exception
{
  public:
    bad_expected_access() = default;
    [[nodiscard]] auto what() const noexcept -> const char * override
    {
        return "bad expected access";
    }
};

template <typename E>
class bad_expected_access : public bad_expected_access<void>
{
  public:
    explicit bad_expected_access(E e) : error_(std::move(e))
    {
    }
    [[nodiscard]] auto error() const & noexcept -> const E &
    {
        return error_;
    }
    auto error() & noexcept -> E &
    {
        return error_;
    }
    [[nodiscard]] auto error() const && noexcept -> const E &&
    {
        return std::move(error_);
    }
    auto error() && noexcept -> E &&
    {
        return std::move(error_);
    }

  private:
    E error_;
};

template <typename E>
class unexpected
{
  public:
    constexpr explicit unexpected(const E &e) : error_(e)
    {
    }
    constexpr explicit unexpected(E &&e) : error_(std::move(e))
    {
    }
    [[nodiscard]] constexpr auto error() const & noexcept -> const E &
    {
        return error_;
    }
    constexpr auto error() & noexcept -> E &
    {
        return error_;
    }
    constexpr auto error() && noexcept -> E &&
    {
        return std::move(error_);
    }

  private:
    E error_;
};

template <typename T, typename E = std::error_code>
class expected
{
  public:
    using value_type = T;
    using error_type = E;
    using unexpected_type = unexpected<E>;

    // constructors
    constexpr expected() : has_(true)
    {
        if constexpr (std::is_default_constructible_v<T>)
        {
            new (&storage_.value_) T();
        }
    }

    constexpr expected(const expected &other) : has_(other.has_)
    {
        if (has_)
            new (&storage_.value_) T(other.storage_.value_);
        else
            new (&storage_.error_) E(other.storage_.error_);
    }

    constexpr expected(expected &&other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                  std::is_nothrow_move_constructible_v<E>)
        : has_(other.has_)
    {
        if (has_)
            new (&storage_.value_) T(std::move(other.storage_.value_));
        else
            new (&storage_.error_) E(std::move(other.storage_.error_));
    }

    template <typename U = T,
              typename = std::enable_if_t<
                  !std::is_same_v<std::decay_t<U>, expected> && !std::is_same_v<std::decay_t<U>, std::in_place_t> &&
                  !std::is_same_v<std::decay_t<U>, unexpected<E>> && std::is_constructible_v<T, U>>>
#if __cplusplus >= 202002L
    constexpr explicit(!std::is_convertible_v<U, T>) expected(U &&value) : has_(true)
#else
    constexpr expected(U &&value, std::enable_if_t<std::is_convertible_v<U, T>, int>  /*unused*/= 0) : has_(true)
    {
        new (&storage_.value_) T(std::forward<U>(value));
    }

    template <typename U = T,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, expected> &&
                                          !std::is_same_v<std::decay_t<U>, std::in_place_t> &&
                                          !std::is_same_v<std::decay_t<U>, unexpected<E>> &&
                                          std::is_constructible_v<T, U> && !std::is_convertible_v<U, T>>>
    constexpr explicit expected(U &&value) : has_(true)
#endif
    {
        new (&storage_.value_) T(std::forward<U>(value));
    }

    template <typename... Args, typename = std::enable_if_t<std::is_constructible_v<T, Args...>>>
    constexpr explicit expected(std::in_place_t /*unused*/, Args &&...args) : has_(true)
    {
        new (&storage_.value_) T(std::forward<Args>(args)...);
    }

    constexpr expected(const unexpected<E> &ue) : has_(false)
    {
        new (&storage_.error_) E(ue.error());
    }

    constexpr expected(unexpected<E> &&ue) : has_(false)
    {
        new (&storage_.error_) E(std::move(ue.error()));
    }

    template <typename... Args>
    constexpr explicit expected(unexpect_t /*unused*/, Args &&...args) : has_(false)
    {
        new (&storage_.error_) E(std::forward<Args>(args)...);
    }

    // assignment operators
    constexpr auto operator=(const expected &other) -> expected &
    {
        if (this == &other)
            return *this;
        this->~expected();
        new (this) expected(other);
        return *this;
    }

    constexpr auto operator=(expected &&other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                             std::is_nothrow_move_constructible_v<E>) -> expected &
    {
        if (this == &other)
            return *this;
        this->~expected();
        new (this) expected(std::move(other));
        return *this;
    }

    template <typename U = T,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, expected> && std::is_constructible_v<T, U>>>
    constexpr auto operator=(U &&value) -> expected &
    {
        if (has_)
        {
            storage_.value_ = std::forward<U>(value);
        }
        else
        {
            this->~expected();
            new (this) expected(std::forward<U>(value));
        }
        return *this;
    }

    constexpr auto operator=(const unexpected<E> &ue) -> expected &
    {
        if (!has_)
        {
            storage_.error_ = ue.error();
        }
        else
        {
            this->~expected();
            new (this) expected(ue);
        }
        return *this;
    }

    constexpr auto operator=(unexpected<E> &&ue) -> expected &
    {
        if (!has_)
        {
            storage_.error_ = std::move(ue.error());
        }
        else
        {
            this->~expected();
            new (this) expected(std::move(ue));
        }
        return *this;
    }

    ~expected()
    {
        if (has_)
            storage_.value_.~T();
        else
            storage_.error_.~E();
    }

    // observers
    constexpr auto operator->() const noexcept -> const T *
    {
        return &storage_.value_;
    }

    constexpr auto operator->() noexcept -> T *
    {
        return &storage_.value_;
    }

    constexpr auto operator*() const & noexcept -> const T &
    {
        return storage_.value_;
    }

    constexpr auto operator*() & noexcept -> T &
    {
        return storage_.value_;
    }

    constexpr auto operator*() const && noexcept -> const T &&
    {
        return std::move(storage_.value_);
    }

    constexpr auto operator*() && noexcept -> T &&
    {
        return std::move(storage_.value_);
    }

    constexpr explicit operator bool() const noexcept
    {
        return has_;
    }

    [[nodiscard]] constexpr auto has_value() const noexcept -> bool
    {
        return has_;
    }

    [[nodiscard]] constexpr auto value() const & -> const T &
    {
        if (!has_)
            THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(storage_.error_));
        return storage_.value_;
    }

    constexpr auto value() & -> T &
    {
        if (!has_)
            THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(storage_.error_));
        return storage_.value_;
    }

    [[nodiscard]] constexpr auto value() const && -> const T &&
    {
        if (!has_)
            THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(std::move(storage_.error_)));
        return std::move(storage_.value_);
    }

    constexpr auto value() && -> T &&
    {
        if (!has_)
            THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(std::move(storage_.error_)));
        return std::move(storage_.value_);
    }

    [[nodiscard]] constexpr auto error() const & noexcept -> const E &
    {
        return storage_.error_;
    }

    constexpr auto error() & noexcept -> E &
    {
        return storage_.error_;
    }

    [[nodiscard]] constexpr auto error() const && noexcept -> const E &&
    {
        return std::move(storage_.error_);
    }

    constexpr auto error() && noexcept -> E &&
    {
        return std::move(storage_.error_);
    }

    template <typename U>
    constexpr auto value_or(U &&default_value) const & -> T
    {
        return has_ ? storage_.value_ : static_cast<T>(std::forward<U>(default_value));
    }

    template <typename U>
    constexpr auto value_or(U &&default_value) && -> T
    {
        return has_ ? std::move(storage_.value_) : static_cast<T>(std::forward<U>(default_value));
    }

    // emplace
    template <typename... Args>
    constexpr auto emplace(Args &&...args) -> T &
    {
        this->~expected();
        new (this) expected(std::in_place, std::forward<Args>(args)...);
        return storage_.value_;
    }

    // swap
    constexpr void swap(expected &other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                  std::is_nothrow_move_constructible_v<E> &&
                                                  std::is_nothrow_swappable_v<T> && std::is_nothrow_swappable_v<E>)
    {
        if (has_ && other.has_)
        {
            using std::swap;
            swap(storage_.value_, other.storage_.value_);
        }
        else if (!has_ && !other.has_)
        {
            using std::swap;
            swap(storage_.error_, other.storage_.error_);
        }
        else
        {
            expected temp(std::move(other));
            other.~expected();
            new (&other) expected(std::move(*this));
            this->~expected();
            new (this) expected(std::move(temp));
        }
    }

    // monadic operations
    template <typename F>
    constexpr auto and_then(F &&f) &
    {
        using U = std::invoke_result_t<F, T &>;
        if (has_)
            return std::invoke(std::forward<F>(f), storage_.value_);
                    return U(unexpect, storage_.error_);
    }

    template <typename F>
    constexpr auto and_then(F &&f) const &
    {
        using U = std::invoke_result_t<F, const T &>;
        if (has_)
            return std::invoke(std::forward<F>(f), storage_.value_);
                    return U(unexpect, storage_.error_);
    }

    template <typename F>
    constexpr auto and_then(F &&f) &&
    {
        using U = std::invoke_result_t<F, T &&>;
        if (has_)
            return std::invoke(std::forward<F>(f), std::move(storage_.value_));
                    return U(unexpect, std::move(storage_.error_));
    }

    template <typename F>
    constexpr auto and_then(F &&f) const &&
    {
        using U = std::invoke_result_t<F, const T &&>;
        if (has_)
            return std::invoke(std::forward<F>(f), std::move(storage_.value_));
                    return U(unexpect, std::move(storage_.error_));
    }

    template <typename F>
    constexpr auto or_else(F &&f) &
    {
        using U = std::invoke_result_t<F, E &>;
        if (has_)
            return U(storage_.value_);
                    return std::invoke(std::forward<F>(f), storage_.error_);
    }

    template <typename F>
    constexpr auto or_else(F &&f) const &
    {
        using U = std::invoke_result_t<F, const E &>;
        if (has_)
            return U(storage_.value_);
                    return std::invoke(std::forward<F>(f), storage_.error_);
    }

    template <typename F>
    constexpr auto or_else(F &&f) &&
    {
        using U = std::invoke_result_t<F, E &&>;
        if (has_)
            return U(std::move(storage_.value_));
                    return std::invoke(std::forward<F>(f), std::move(storage_.error_));
    }

    template <typename F>
    constexpr auto or_else(F &&f) const &&
    {
        using U = std::invoke_result_t<F, const E &&>;
        if (has_)
            return U(std::move(storage_.value_));
                    return std::invoke(std::forward<F>(f), std::move(storage_.error_));
    }

    template <typename F>
    constexpr auto transform(F &&f) &
    {
        using U = std::remove_cv_t<std::invoke_result_t<F, T &>>;
        if (has_)
            return expected<U, E>(std::in_place, std::invoke(std::forward<F>(f), storage_.value_));
                    return expected<U, E>(unexpect, storage_.error_);
    }

    template <typename F>
    constexpr auto transform(F &&f) const &
    {
        using U = std::remove_cv_t<std::invoke_result_t<F, const T &>>;
        if (has_)
            return expected<U, E>(std::in_place, std::invoke(std::forward<F>(f), storage_.value_));
                    return expected<U, E>(unexpect, storage_.error_);
    }

    template <typename F>
    constexpr auto transform(F &&f) &&
    {
        using U = std::remove_cv_t<std::invoke_result_t<F, T &&>>;
        if (has_)
            return expected<U, E>(std::in_place, std::invoke(std::forward<F>(f), std::move(storage_.value_)));
                    return expected<U, E>(unexpect, std::move(storage_.error_));
    }

    template <typename F>
    constexpr auto transform(F &&f) const &&
    {
        using U = std::remove_cv_t<std::invoke_result_t<F, const T &&>>;
        if (has_)
            return expected<U, E>(std::in_place, std::invoke(std::forward<F>(f), std::move(storage_.value_)));
                    return expected<U, E>(unexpect, std::move(storage_.error_));
    }

    template <typename F>
    constexpr auto transform_error(F &&f) &
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, E &>>;
        if (has_)
            return expected<T, G>(storage_.value_);
                    return expected<T, G>(unexpect, std::invoke(std::forward<F>(f), storage_.error_));
    }

    template <typename F>
    constexpr auto transform_error(F &&f) const &
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, const E &>>;
        if (has_)
            return expected<T, G>(storage_.value_);
                    return expected<T, G>(unexpect, std::invoke(std::forward<F>(f), storage_.error_));
    }

    template <typename F>
    constexpr auto transform_error(F &&f) &&
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, E &&>>;
        if (has_)
            return expected<T, G>(std::move(storage_.value_));
                    return expected<T, G>(unexpect, std::invoke(std::forward<F>(f), std::move(storage_.error_)));
    }

    template <typename F>
    constexpr auto transform_error(F &&f) const &&
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, const E &&>>;
        if (has_)
            return expected<T, G>(std::move(storage_.value_));
                    return expected<T, G>(unexpect, std::invoke(std::forward<F>(f), std::move(storage_.error_)));
    }

    // equality operators
    template <typename T2, typename E2>
    constexpr friend auto operator==(const expected &lhs, const expected<T2, E2> &rhs) -> bool
    {
        if (lhs.has_value() != rhs.has_value())
            return false;
        if (lhs.has_value())
            return *lhs == *rhs;
        return lhs.error() == rhs.error();
    }

    template <typename T2, typename E2>
    constexpr friend auto operator!=(const expected &lhs, const expected<T2, E2> &rhs) -> bool
    {
        return !(lhs == rhs);
    }

    template <typename T2, typename = std::enable_if_t<!std::is_same_v<expected, std::decay_t<T2>>>>
    constexpr friend auto operator==(const expected &lhs, const T2 &rhs) -> bool
    {
        return lhs.has_value() && *lhs == rhs;
    }

    template <typename T2, typename = std::enable_if_t<!std::is_same_v<expected, std::decay_t<T2>>>>
    constexpr friend auto operator==(const T2 &lhs, const expected &rhs) -> bool
    {
        return rhs.has_value() && lhs == *rhs;
    }

    template <typename T2, typename = std::enable_if_t<!std::is_same_v<expected, std::decay_t<T2>>>>
    constexpr friend auto operator!=(const expected &lhs, const T2 &rhs) -> bool
    {
        return !(lhs == rhs);
    }

    template <typename T2, typename = std::enable_if_t<!std::is_same_v<expected, std::decay_t<T2>>>>
    constexpr friend auto operator!=(const T2 &lhs, const expected &rhs) -> bool
    {
        return !(lhs == rhs);
    }

    template <typename E2>
    constexpr friend auto operator==(const expected &lhs, const unexpected<E2> &rhs) -> bool
    {
        return !lhs.has_value() && lhs.error() == rhs.error();
    }

    template <typename E2>
    constexpr friend auto operator==(const unexpected<E2> &lhs, const expected &rhs) -> bool
    {
        return !rhs.has_value() && lhs.error() == rhs.error();
    }

    template <typename E2>
    constexpr friend auto operator!=(const expected &lhs, const unexpected<E2> &rhs) -> bool
    {
        return !(lhs == rhs);
    }

    template <typename E2>
    constexpr friend auto operator!=(const unexpected<E2> &lhs, const expected &rhs) -> bool
    {
        return !(lhs == rhs);
    }

  private:
    bool has_;
    union Storage {
        Storage()
        {
        }
        ~Storage()
        {
        }
        T value_;
        E error_;
    } storage_;
};

template <typename E>
class expected<void, E>
{
  public:
    using value_type = void;
    using error_type = E;
    using unexpected_type = unexpected<E>;

    constexpr expected() : has_(true)
    {
    }

    constexpr expected(const expected &other) = default;
    constexpr expected(expected &&other) = default;

    template <typename... Args>
    constexpr explicit expected(unexpect_t /*unused*/, Args &&...args) : has_(false), error_(std::forward<Args>(args)...)
    {
    }

    constexpr expected(const unexpected<E> &ue) : has_(false), error_(ue.error())
    {
    }

    constexpr expected(unexpected<E> &&ue) : has_(false), error_(std::move(ue.error()))
    {
    }

    constexpr auto operator=(const expected &other) -> expected & = default;
    constexpr auto operator=(expected &&other) -> expected & = default;

    constexpr auto operator=(const unexpected<E> &ue) -> expected &
    {
        has_ = false;
        error_ = ue.error();
        return *this;
    }

    constexpr auto operator=(unexpected<E> &&ue) -> expected &
    {
        has_ = false;
        error_ = std::move(ue.error());
        return *this;
    }

    constexpr explicit operator bool() const noexcept
    {
        return has_;
    }

    [[nodiscard]] constexpr auto has_value() const noexcept -> bool
    {
        return has_;
    }

    constexpr void value() const
    {
        if (!has_)
            THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(error_));
    }

    [[nodiscard]] constexpr auto error() const & noexcept -> const E &
    {
        return error_;
    }

    constexpr auto error() & noexcept -> E &
    {
        return error_;
    }

    [[nodiscard]] constexpr auto error() const && noexcept -> const E &&
    {
        return std::move(error_);
    }

    constexpr auto error() && noexcept -> E &&
    {
        return std::move(error_);
    }

    constexpr void emplace()
    {
        has_ = true;
    }

    constexpr void swap(expected &other) noexcept(std::is_nothrow_move_constructible_v<E> &&
                                                  std::is_nothrow_swappable_v<E>)
    {
        if (has_ && other.has_)
        {
            // both have values, nothing to swap
        }
        else if (!has_ && !other.has_)
        {
            using std::swap;
            swap(error_, other.error_);
        }
        else
        {
            std::swap(has_, other.has_);
            std::swap(error_, other.error_);
        }
    }

    // monadic operations
    template <typename F>
    constexpr auto and_then(F &&f) &
    {
        using U = std::invoke_result_t<F>;
        if (has_)
            return std::invoke(std::forward<F>(f));
                    return U(unexpect, error_);
    }

    template <typename F>
    constexpr auto and_then(F &&f) const &
    {
        using U = std::invoke_result_t<F>;
        if (has_)
            return std::invoke(std::forward<F>(f));
                    return U(unexpect, error_);
    }

    template <typename F>
    constexpr auto and_then(F &&f) &&
    {
        using U = std::invoke_result_t<F>;
        if (has_)
            return std::invoke(std::forward<F>(f));
                    return U(unexpect, std::move(error_));
    }

    template <typename F>
    constexpr auto and_then(F &&f) const &&
    {
        using U = std::invoke_result_t<F>;
        if (has_)
            return std::invoke(std::forward<F>(f));
                    return U(unexpect, std::move(error_));
    }

    template <typename F>
    constexpr auto or_else(F &&f) &
    {
        using U = std::invoke_result_t<F, E &>;
        if (has_)
            return U();
                    return std::invoke(std::forward<F>(f), error_);
    }

    template <typename F>
    constexpr auto or_else(F &&f) const &
    {
        using U = std::invoke_result_t<F, const E &>;
        if (has_)
            return U();
                    return std::invoke(std::forward<F>(f), error_);
    }

    template <typename F>
    constexpr auto or_else(F &&f) &&
    {
        using U = std::invoke_result_t<F, E &&>;
        if (has_)
            return U();
                    return std::invoke(std::forward<F>(f), std::move(error_));
    }

    template <typename F>
    constexpr auto or_else(F &&f) const &&
    {
        using U = std::invoke_result_t<F, const E &&>;
        if (has_)
            return U();
                    return std::invoke(std::forward<F>(f), std::move(error_));
    }

    template <typename F>
    constexpr auto transform(F &&f) &
    {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (has_)
            return expected<U, E>(std::in_place, std::invoke(std::forward<F>(f)));
                    return expected<U, E>(unexpect, error_);
    }

    template <typename F>
    constexpr auto transform(F &&f) const &
    {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (has_)
            return expected<U, E>(std::in_place, std::invoke(std::forward<F>(f)));
                    return expected<U, E>(unexpect, error_);
    }

    template <typename F>
    constexpr auto transform(F &&f) &&
    {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (has_)
            return expected<U, E>(std::in_place, std::invoke(std::forward<F>(f)));
                    return expected<U, E>(unexpect, std::move(error_));
    }

    template <typename F>
    constexpr auto transform(F &&f) const &&
    {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (has_)
            return expected<U, E>(std::in_place, std::invoke(std::forward<F>(f)));
                    return expected<U, E>(unexpect, std::move(error_));
    }

    template <typename F>
    constexpr auto transform_error(F &&f) &
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, E &>>;
        if (has_)
            return expected<void, G>();
                    return expected<void, G>(unexpect, std::invoke(std::forward<F>(f), error_));
    }

    template <typename F>
    constexpr auto transform_error(F &&f) const &
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, const E &>>;
        if (has_)
            return expected<void, G>();
                    return expected<void, G>(unexpect, std::invoke(std::forward<F>(f), error_));
    }

    template <typename F>
    constexpr auto transform_error(F &&f) &&
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, E &&>>;
        if (has_)
            return expected<void, G>();
                    return expected<void, G>(unexpect, std::invoke(std::forward<F>(f), std::move(error_)));
    }

    template <typename F>
    constexpr auto transform_error(F &&f) const &&
    {
        using G = std::remove_cv_t<std::invoke_result_t<F, const E &&>>;
        if (has_)
            return expected<void, G>();
                    return expected<void, G>(unexpect, std::invoke(std::forward<F>(f), std::move(error_)));
    }

    // equality operators
    template <typename E2>
    constexpr friend auto operator==(const expected &lhs, const expected<void, E2> &rhs) -> bool
    {
        if (lhs.has_value() != rhs.has_value())
            return false;
        if (lhs.has_value())
            return true;
        return lhs.error() == rhs.error();
    }

    template <typename E2>
    constexpr friend auto operator!=(const expected &lhs, const expected<void, E2> &rhs) -> bool
    {
        return !(lhs == rhs);
    }

    template <typename E2>
    constexpr friend auto operator==(const expected &lhs, const unexpected<E2> &rhs) -> bool
    {
        return !lhs.has_value() && lhs.error() == rhs.error();
    }

    template <typename E2>
    constexpr friend auto operator==(const unexpected<E2> &lhs, const expected &rhs) -> bool
    {
        return !rhs.has_value() && lhs.error() == rhs.error();
    }

    template <typename E2>
    constexpr friend auto operator!=(const expected &lhs, const unexpected<E2> &rhs) -> bool
    {
        return !(lhs == rhs);
    }

    template <typename E2>
    constexpr friend auto operator!=(const unexpected<E2> &lhs, const expected &rhs) -> bool
    {
        return !(lhs == rhs);
    }

  private:
    bool has_;
    E error_{};
};

#endif // std::expected fallback

// swap for expected
template <typename T, typename E>
constexpr void swap(expected<T, E> &lhs, expected<T, E> &rhs) noexcept(noexcept(lhs.swap(rhs)))
{
    lhs.swap(rhs);
}

} // namespace threadschedule
