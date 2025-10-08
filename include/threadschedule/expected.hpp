#pragma once

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

namespace threadschedule
{

#if THREADSCHEDULE_HAS_STD_EXPECTED
template <typename E>
using unexpected = std::unexpected<E>;
using unexpect_t = std::unexpect_t;
inline constexpr unexpect_t unexpect{};
template <typename T, typename E = std::error_code>
using expected = std::expected<T, E>;

#else

struct unexpect_t
{
};
inline constexpr unexpect_t unexpect{};

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
    constexpr const E &error() const & noexcept
    {
        return error_;
    }
    constexpr E &error() & noexcept
    {
        return error_;
    }
    constexpr E &&error() && noexcept
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
    // constructors
    expected() : has_(true)
    {
        if constexpr (std::is_default_constructible_v<T>)
        {
            new (&storage_.value_) T();
        }
    }
    expected(const T &value) : has_(true)
    {
        new (&storage_.value_) T(value);
    }
    expected(T &&value) : has_(true)
    {
        new (&storage_.value_) T(std::move(value));
    }
    expected(unexpected<E> ue) : has_(false)
    {
        new (&storage_.error_) E(std::move(ue.error()));
    }
    expected(unexpect_t, const E &error) : has_(false)
    {
        new (&storage_.error_) E(error);
    }

    expected(const expected &other) : has_(other.has_)
    {
        if (has_)
            new (&storage_.value_) T(other.storage_.value_);
        else
            new (&storage_.error_) E(other.storage_.error_);
    }
    expected(expected &&other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                        std::is_nothrow_move_constructible_v<E>)
        : has_(other.has_)
    {
        if (has_)
            new (&storage_.value_) T(std::move(other.storage_.value_));
        else
            new (&storage_.error_) E(std::move(other.storage_.error_));
    }

    expected &operator=(const expected &other)
    {
        if (this == &other)
            return *this;
        this->~expected();
        new (this) expected(other);
        return *this;
    }
    expected &operator=(expected &&other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                   std::is_nothrow_move_constructible_v<E>)
    {
        if (this == &other)
            return *this;
        this->~expected();
        new (this) expected(std::move(other));
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
    bool has_value() const noexcept
    {
        return has_;
    }
    explicit operator bool() const noexcept
    {
        return has_;
    }
    T &value() &
    {
        return storage_.value_;
    }
    const T &value() const &
    {
        return storage_.value_;
    }
    T &&value() &&
    {
        return std::move(storage_.value_);
    }

    E &error() &
    {
        return storage_.error_;
    }
    const E &error() const &
    {
        return storage_.error_;
    }
    E &&error() &&
    {
        return std::move(storage_.error_);
    }

    template <typename U>
    T value_or(U &&default_value) const &
    {
        return has_ ? storage_.value_ : static_cast<T>(std::forward<U>(default_value));
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
    expected() : has_(true)
    {
    }
    expected(unexpected<E> ue) : has_(false), error_(std::move(ue.error()))
    {
    }
    expected(unexpect_t, const E &error) : has_(false), error_(error)
    {
    }

    bool has_value() const noexcept
    {
        return has_;
    }
    explicit operator bool() const noexcept
    {
        return has_;
    }
    E &error() &
    {
        return error_;
    }
    const E &error() const &
    {
        return error_;
    }
    E &&error() &&
    {
        return std::move(error_);
    }

  private:
    bool has_;
    E error_{};
};

#endif // std::expected fallback

} // namespace threadschedule
