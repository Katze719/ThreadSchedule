#pragma once

/**
 * @file expected.hpp
 * @brief Stable library-owned expected implementation for ThreadSchedule APIs.
 */

#include <exception>
#include <functional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__has_include)
#  if __has_include(<version>)
#    include <version>
#  endif
#endif

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#  include <expected>
#  define THREADSCHEDULE_HAS_STD_EXPECTED 1
#elif (defined(__cplusplus) && __cplusplus >= 202302L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202302L)
#  include <expected>
#  define THREADSCHEDULE_HAS_STD_EXPECTED 1
#else
#  define THREADSCHEDULE_HAS_STD_EXPECTED 0
#endif

#ifdef __cpp_exceptions
#  define THREADSCHEDULE_EXPECTED_THROW(ex) throw ex
#else
#  define THREADSCHEDULE_EXPECTED_THROW(ex) ::std::terminate()
#endif

namespace threadschedule
{

struct unexpect_t
{
  explicit unexpect_t() = default;
};
inline constexpr unexpect_t unexpect{};

template <typename E>
class bad_expected_access;

/// @cond INTERNAL
template <>
class bad_expected_access<void> : public std::exception
{
public:
  [[nodiscard]] auto
  what() const noexcept -> char const* override
  {
    return "bad expected access";
  }
};

template <typename E>
class bad_expected_access : public bad_expected_access<void>
{
public:
  explicit bad_expected_access(E error) : error_(std::move(error)) {}
  [[nodiscard]] auto
  error() const& noexcept -> E const&
  {
    return error_;
  }
  auto
  error() & noexcept -> E&
  {
    return error_;
  }
  [[nodiscard]] auto
  error() const&& noexcept -> E const&&
  {
    return std::move(error_);
  }
  auto
  error() && noexcept -> E&&
  {
    return std::move(error_);
  }

private:
  E error_;
};
/// @endcond

template <typename E>
class unexpected
{
public:
  constexpr explicit unexpected(E const& error) : error_(error) {}
  constexpr explicit unexpected(E&& error) : error_(std::move(error)) {}
  [[nodiscard]] constexpr auto
  error() const& noexcept -> E const&
  {
    return error_;
  }
  constexpr auto
  error() & noexcept -> E&
  {
    return error_;
  }
  [[nodiscard]] constexpr auto
  error() const&& noexcept -> E const&&
  {
    return std::move(error_);
  }
  constexpr auto
  error() && noexcept -> E&&
  {
    return std::move(error_);
  }

private:
  E error_;
};

template <typename T, typename E = std::error_code>
class expected;

namespace detail
{
/// @cond INTERNAL

struct expected_value_tag
{
};
struct expected_error_tag
{
};

template <typename T>
struct expected_value_holder
{
  template <typename... Args>
  explicit constexpr expected_value_holder(std::in_place_t, Args&&... args) : value(std::forward<Args>(args)...)
  {
  }
  T value;
};

template <typename E>
struct expected_error_holder
{
  template <typename... Args>
  explicit constexpr expected_error_holder(std::in_place_t, Args&&... args) : error(std::forward<Args>(args)...)
  {
  }
  E error;
};

template <typename T, typename E>
class expected_storage_common
{
public:
  using value_holder = expected_value_holder<T>;
  using error_holder = expected_error_holder<E>;

  template <typename... Args>
  explicit constexpr expected_storage_common(expected_value_tag, Args&&... args)
      : data_(std::in_place_index<0>, std::in_place, std::forward<Args>(args)...)
  {
  }
  template <typename... Args>
  explicit constexpr expected_storage_common(expected_error_tag, Args&&... args)
      : data_(std::in_place_index<1>, std::in_place, std::forward<Args>(args)...)
  {
  }

  expected_storage_common(expected_storage_common const&) = default;
  expected_storage_common(expected_storage_common&&) = default;
  auto operator=(expected_storage_common const&) -> expected_storage_common& = default;
  auto operator=(expected_storage_common&&) -> expected_storage_common& = default;
  ~expected_storage_common() = default;

  [[nodiscard]] constexpr auto
  has_value() const noexcept -> bool
  {
    return data_.index() == 0;
  }
  constexpr auto
  value() & noexcept -> T&
  {
    return std::get<0>(data_).value;
  }
  [[nodiscard]] constexpr auto
  value() const& noexcept -> T const&
  {
    return std::get<0>(data_).value;
  }
  constexpr auto
  value() && noexcept -> T&&
  {
    return std::move(std::get<0>(data_).value);
  }
  [[nodiscard]] constexpr auto
  value() const&& noexcept -> T const&&
  {
    return std::move(std::get<0>(data_).value);
  }
  constexpr auto
  error() & noexcept -> E&
  {
    return std::get<1>(data_).error;
  }
  [[nodiscard]] constexpr auto
  error() const& noexcept -> E const&
  {
    return std::get<1>(data_).error;
  }
  constexpr auto
  error() && noexcept -> E&&
  {
    return std::move(std::get<1>(data_).error);
  }
  [[nodiscard]] constexpr auto
  error() const&& noexcept -> E const&&
  {
    return std::move(std::get<1>(data_).error);
  }

  template <typename U>
  void
  assign_value(U&& source)
  {
    if (has_value())
      value() = std::forward<U>(source);
    else
      replace_with_value(std::forward<U>(source));
  }

  template <typename G>
  void
  assign_error(G&& source)
  {
    if (has_value())
      replace_with_error(std::forward<G>(source));
    else
      error() = std::forward<G>(source);
  }

  template <typename... Args>
  auto
  emplace_value(Args&&... args) noexcept -> T&
  {
    data_.template emplace<0>(std::in_place, std::forward<Args>(args)...);
    return value();
  }

  void
  copy_assign(expected_storage_common const& other)
  {
    if (has_value() && other.has_value())
      value() = other.value();
    else if (!has_value() && !other.has_value())
      error() = other.error();
    else if (other.has_value())
      replace_with_value(other.value());
    else
      replace_with_error(other.error());
  }

  void
  move_assign(expected_storage_common&& other)
  {
    if (has_value() && other.has_value())
      value() = std::move(other).value();
    else if (!has_value() && !other.has_value())
      error() = std::move(other).error();
    else if (other.has_value())
      replace_with_value(std::move(other).value());
    else
      replace_with_error(std::move(other).error());
  }

  void
  swap_storage(expected_storage_common& other) noexcept(std::is_nothrow_move_constructible_v<T>
                                                        && std::is_nothrow_move_constructible_v<E>
                                                        && std::is_nothrow_swappable_v<T>
                                                        && std::is_nothrow_swappable_v<E>)
  {
    if (has_value() && other.has_value())
      {
        using std::swap;
        swap(value(), other.value());
      }
    else if (!has_value() && !other.has_value())
      {
        using std::swap;
        swap(error(), other.error());
      }
    else if (has_value())
      swap_value_error(*this, other);
    else
      swap_value_error(other, *this);
  }

private:
  template <typename U>
  void
  replace_with_value(U&& source)
  {
    if constexpr (std::is_nothrow_constructible_v<T, U>)
      data_.template emplace<0>(std::in_place, std::forward<U>(source));
    else if constexpr (std::is_nothrow_move_constructible_v<T>)
      {
        T replacement(std::forward<U>(source));
        data_.template emplace<0>(std::in_place, std::move(replacement));
      }
    else
      {
        static_assert(std::is_nothrow_move_constructible_v<E>);
        E backup(std::move(error()));
#ifdef __cpp_exceptions
        try
          {
            data_.template emplace<0>(std::in_place, std::forward<U>(source));
          }
        catch (...)
          {
            data_.template emplace<1>(std::in_place, std::move(backup));
            throw;
          }
#else
        (void)backup;
        data_.template emplace<0>(std::in_place, std::forward<U>(source));
#endif
      }
  }

  template <typename G>
  void
  replace_with_error(G&& source)
  {
    if constexpr (std::is_nothrow_constructible_v<E, G>)
      data_.template emplace<1>(std::in_place, std::forward<G>(source));
    else if constexpr (std::is_nothrow_move_constructible_v<E>)
      {
        E replacement(std::forward<G>(source));
        data_.template emplace<1>(std::in_place, std::move(replacement));
      }
    else
      {
        static_assert(std::is_nothrow_move_constructible_v<T>);
        T backup(std::move(value()));
#ifdef __cpp_exceptions
        try
          {
            data_.template emplace<1>(std::in_place, std::forward<G>(source));
          }
        catch (...)
          {
            data_.template emplace<0>(std::in_place, std::move(backup));
            throw;
          }
#else
        (void)backup;
        data_.template emplace<1>(std::in_place, std::forward<G>(source));
#endif
      }
  }

  static void
  swap_value_error(expected_storage_common& with_value, expected_storage_common& with_error)
  {
    if constexpr (std::is_nothrow_move_constructible_v<T>)
      {
        T backup(std::move(with_value.value()));
#ifdef __cpp_exceptions
        try
          {
            with_value.data_.template emplace<1>(std::in_place, std::move(with_error.error()));
          }
        catch (...)
          {
            with_value.data_.template emplace<0>(std::in_place, std::move(backup));
            throw;
          }
#else
        with_value.data_.template emplace<1>(std::in_place, std::move(with_error.error()));
#endif
        with_error.data_.template emplace<0>(std::in_place, std::move(backup));
      }
    else
      {
        static_assert(std::is_nothrow_move_constructible_v<E>);
        E backup(std::move(with_error.error()));
#ifdef __cpp_exceptions
        try
          {
            with_error.data_.template emplace<0>(std::in_place, std::move(with_value.value()));
          }
        catch (...)
          {
            with_error.data_.template emplace<1>(std::in_place, std::move(backup));
            throw;
          }
#else
        with_error.data_.template emplace<0>(std::in_place, std::move(with_value.value()));
#endif
        with_value.data_.template emplace<1>(std::in_place, std::move(backup));
      }
  }

  std::variant<value_holder, error_holder> data_;
};

template <typename T, typename E>
inline constexpr bool expected_copy_assignable_v
    = std::is_copy_constructible_v<T> && std::is_copy_constructible_v<E> && std::is_copy_assignable_v<T>
      && std::is_copy_assignable_v<E>
      && (std::is_nothrow_move_constructible_v<T> || std::is_nothrow_move_constructible_v<E>);
template <typename T, typename E>
inline constexpr bool expected_move_assignable_v
    = std::is_move_constructible_v<T> && std::is_move_constructible_v<E> && std::is_move_assignable_v<T>
      && std::is_move_assignable_v<E>
      && (std::is_nothrow_move_constructible_v<T> || std::is_nothrow_move_constructible_v<E>);

template <typename T, typename E, bool Copy = expected_copy_assignable_v<T, E>,
          bool Move = expected_move_assignable_v<T, E>>
class expected_storage;

template <typename T, typename E>
class expected_storage<T, E, true, true> : public expected_storage_common<T, E>
{
  using base = expected_storage_common<T, E>;

public:
  using base::base;
  expected_storage(expected_storage const&) = default;
  expected_storage(expected_storage&&) = default;
  auto
  operator=(expected_storage const& other) -> expected_storage&
  {
    if (this != &other)
      this->copy_assign(other);
    return *this;
  }
  auto
  operator=(expected_storage&& other) noexcept(std::is_nothrow_move_constructible_v<T>
                                               && std::is_nothrow_move_constructible_v<E>
                                               && std::is_nothrow_move_assignable_v<T>
                                               && std::is_nothrow_move_assignable_v<E>) -> expected_storage&
  {
    if (this != &other)
      this->move_assign(std::move(other));
    return *this;
  }
};

template <typename T, typename E>
class expected_storage<T, E, true, false> : public expected_storage_common<T, E>
{
  using base = expected_storage_common<T, E>;

public:
  using base::base;
  expected_storage(expected_storage const&) = default;
  expected_storage(expected_storage&&) = default;
  auto
  operator=(expected_storage const& other) -> expected_storage&
  {
    if (this != &other)
      this->copy_assign(other);
    return *this;
  }
  auto operator=(expected_storage&&) -> expected_storage& = delete;
};

template <typename T, typename E>
class expected_storage<T, E, false, true> : public expected_storage_common<T, E>
{
  using base = expected_storage_common<T, E>;

public:
  using base::base;
  expected_storage(expected_storage const&) = default;
  expected_storage(expected_storage&&) = default;
  auto operator=(expected_storage const&) -> expected_storage& = delete;
  auto
  operator=(expected_storage&& other) noexcept(std::is_nothrow_move_constructible_v<T>
                                               && std::is_nothrow_move_constructible_v<E>
                                               && std::is_nothrow_move_assignable_v<T>
                                               && std::is_nothrow_move_assignable_v<E>) -> expected_storage&
  {
    if (this != &other)
      this->move_assign(std::move(other));
    return *this;
  }
};

template <typename T, typename E>
class expected_storage<T, E, false, false> : public expected_storage_common<T, E>
{
  using base = expected_storage_common<T, E>;

public:
  using base::base;
  expected_storage(expected_storage const&) = default;
  expected_storage(expected_storage&&) = default;
  auto operator=(expected_storage const&) -> expected_storage& = delete;
  auto operator=(expected_storage&&) -> expected_storage& = delete;
};

/// @endcond
} // namespace detail

template <typename T, typename E>
class expected
{
public:
  using value_type = T;
  using error_type = E;
  using unexpected_type = unexpected<E>;

  template <typename U = T, std::enable_if_t<std::is_default_constructible_v<U>, int> = 0>
  constexpr expected() : storage_(detail::expected_value_tag{})
  {
  }
  template <typename U = T, std::enable_if_t<!std::is_default_constructible_v<U>, int> = 0>
  expected() = delete;

  expected(expected const&) = default;
  expected(expected&&) = default;
  auto operator=(expected const&) -> expected& = default;
  auto operator=(expected&&) -> expected& = default;
  ~expected() = default;

  template <typename U = T, std::enable_if_t<!std::is_same_v<std::decay_t<U>, expected>
                                                 && !std::is_same_v<std::decay_t<U>, std::in_place_t>
                                                 && !std::is_same_v<std::decay_t<U>, unexpected<E>>
                                                 && std::is_constructible_v<T, U> && std::is_convertible_v<U, T>,
                                             int> = 0>
  constexpr expected(U&& value) : storage_(detail::expected_value_tag{}, std::forward<U>(value))
  {
  }
  template <typename U = T, std::enable_if_t<!std::is_same_v<std::decay_t<U>, expected>
                                                 && !std::is_same_v<std::decay_t<U>, std::in_place_t>
                                                 && !std::is_same_v<std::decay_t<U>, unexpected<E>>
                                                 && std::is_constructible_v<T, U> && !std::is_convertible_v<U, T>,
                                             int> = 0>
  constexpr explicit expected(U&& value) : storage_(detail::expected_value_tag{}, std::forward<U>(value))
  {
  }
  template <typename... Args, std::enable_if_t<std::is_constructible_v<T, Args...>, int> = 0>
  constexpr explicit expected(std::in_place_t, Args&&... args)
      : storage_(detail::expected_value_tag{}, std::forward<Args>(args)...)
  {
  }
  constexpr expected(unexpected<E> const& error) : storage_(detail::expected_error_tag{}, error.error()) {}
  constexpr expected(unexpected<E>&& error) : storage_(detail::expected_error_tag{}, std::move(error).error()) {}
  template <typename... Args, std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
  constexpr explicit expected(unexpect_t, Args&&... args)
      : storage_(detail::expected_error_tag{}, std::forward<Args>(args)...)
  {
  }

  template <typename U = T,
            std::enable_if_t<!std::is_same_v<std::decay_t<U>, expected> && std::is_constructible_v<T, U>
                                 && std::is_assignable_v<T&, U>
                                 && (std::is_nothrow_constructible_v<T, U> || std::is_nothrow_move_constructible_v<T>
                                     || std::is_nothrow_move_constructible_v<E>),
                             int> = 0>
  auto
  operator=(U&& value) -> expected&
  {
    storage_.assign_value(std::forward<U>(value));
    return *this;
  }
  template <typename G, std::enable_if_t<std::is_constructible_v<E, G const&> && std::is_assignable_v<E&, G const&>
                                             && (std::is_nothrow_constructible_v<E, G const&>
                                                 || std::is_nothrow_move_constructible_v<E>
                                                 || std::is_nothrow_move_constructible_v<T>),
                                         int> = 0>
  auto
  operator=(unexpected<G> const& error) -> expected&
  {
    storage_.assign_error(error.error());
    return *this;
  }
  template <typename G,
            std::enable_if_t<std::is_constructible_v<E, G> && std::is_assignable_v<E&, G>
                                 && (std::is_nothrow_constructible_v<E, G> || std::is_nothrow_move_constructible_v<E>
                                     || std::is_nothrow_move_constructible_v<T>),
                             int> = 0>
  auto
  operator=(unexpected<G>&& error) -> expected&
  {
    storage_.assign_error(std::move(error).error());
    return *this;
  }

  [[nodiscard]] constexpr auto
  has_value() const noexcept -> bool
  {
    return storage_.has_value();
  }
  constexpr explicit
  operator bool() const noexcept
  {
    return has_value();
  }
  constexpr auto
  operator->() noexcept -> T*
  {
    return &storage_.value();
  }
  constexpr auto
  operator->() const noexcept -> T const*
  {
    return &storage_.value();
  }
  constexpr auto
  operator*() & noexcept -> T&
  {
    return storage_.value();
  }
  [[nodiscard]] constexpr auto
  operator*() const& noexcept -> T const&
  {
    return storage_.value();
  }
  constexpr auto
  operator*() && noexcept -> T&&
  {
    return std::move(storage_).value();
  }
  [[nodiscard]] constexpr auto
  operator*() const&& noexcept -> T const&&
  {
    return std::move(storage_).value();
  }

  auto
  value() & -> T&
  {
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(storage_.error()));
    return storage_.value();
  }
  [[nodiscard]] auto
  value() const& -> T const&
  {
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(storage_.error()));
    return storage_.value();
  }
  auto
  value() && -> T&&
  {
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(std::move(storage_).error()));
    return std::move(storage_).value();
  }
  [[nodiscard]] auto
  value() const&& -> T const&&
  {
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(std::move(storage_).error()));
    return std::move(storage_).value();
  }
  constexpr auto
  error() & noexcept -> E&
  {
    return storage_.error();
  }
  [[nodiscard]] constexpr auto
  error() const& noexcept -> E const&
  {
    return storage_.error();
  }
  constexpr auto
  error() && noexcept -> E&&
  {
    return std::move(storage_).error();
  }
  [[nodiscard]] constexpr auto
  error() const&& noexcept -> E const&&
  {
    return std::move(storage_).error();
  }

  template <typename U>
  auto
  value_or(U&& fallback) const& -> T
  {
    return has_value() ? storage_.value() : static_cast<T>(std::forward<U>(fallback));
  }
  template <typename U>
  auto
  value_or(U&& fallback) && -> T
  {
    return has_value() ? std::move(storage_).value() : static_cast<T>(std::forward<U>(fallback));
  }
  template <typename... Args, std::enable_if_t<std::is_nothrow_constructible_v<T, Args...>, int> = 0>
  auto
  emplace(Args&&... args) noexcept -> T&
  {
    return storage_.emplace_value(std::forward<Args>(args)...);
  }
  void
  swap(expected& other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>
                                 && std::is_nothrow_swappable_v<T> && std::is_nothrow_swappable_v<E>)
  {
    storage_.swap_storage(other.storage_);
  }

  template <typename F>
  auto
  and_then(F&& f) &
  {
    return and_then_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  and_then(F&& f) const&
  {
    return and_then_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  and_then(F&& f) &&
  {
    return and_then_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  and_then(F&& f) const&&
  {
    return and_then_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  or_else(F&& f) &
  {
    return or_else_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  or_else(F&& f) const&
  {
    return or_else_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  or_else(F&& f) &&
  {
    return or_else_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  or_else(F&& f) const&&
  {
    return or_else_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  transform(F&& f) &
  {
    return transform_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  transform(F&& f) const&
  {
    return transform_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  transform(F&& f) &&
  {
    return transform_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  transform(F&& f) const&&
  {
    return transform_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  transform_error(F&& f) &
  {
    return transform_error_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  transform_error(F&& f) const&
  {
    return transform_error_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  transform_error(F&& f) &&
  {
    return transform_error_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  transform_error(F&& f) const&&
  {
    return transform_error_impl(std::move(*this), std::forward<F>(f));
  }

#if THREADSCHEDULE_HAS_STD_EXPECTED
  template <typename U = T, typename G = E,
            std::enable_if_t<std::is_copy_constructible_v<U> && std::is_copy_constructible_v<G>, int> = 0>
  operator std::expected<T, E>() const&
  {
    if (has_value())
      return std::expected<T, E>(std::in_place, storage_.value());
    return std::expected<T, E>(std::unexpect, storage_.error());
  }
  template <typename U = T, typename G = E,
            std::enable_if_t<std::is_move_constructible_v<U> && std::is_move_constructible_v<G>, int> = 0>
  operator std::expected<T, E>() &&
  {
    if (has_value())
      return std::expected<T, E>(std::in_place, std::move(storage_).value());
    return std::expected<T, E>(std::unexpect, std::move(storage_).error());
  }
#endif

  template <typename T2, typename E2>
  friend auto
  operator==(expected const& lhs, expected<T2, E2> const& rhs) -> bool
  {
    if (lhs.has_value() != rhs.has_value())
      return false;
    return lhs.has_value() ? *lhs == *rhs : lhs.error() == rhs.error();
  }
  template <typename T2, typename E2>
  friend auto
  operator!=(expected const& lhs, expected<T2, E2> const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename T2, std::enable_if_t<!std::is_same_v<expected, std::decay_t<T2>>, int> = 0>
  friend auto
  operator==(expected const& lhs, T2 const& rhs) -> bool
  {
    return lhs.has_value() && *lhs == rhs;
  }
  template <typename T2, std::enable_if_t<!std::is_same_v<expected, std::decay_t<T2>>, int> = 0>
  friend auto
  operator==(T2 const& lhs, expected const& rhs) -> bool
  {
    return rhs.has_value() && lhs == *rhs;
  }
  template <typename T2, std::enable_if_t<!std::is_same_v<expected, std::decay_t<T2>>, int> = 0>
  friend auto
  operator!=(expected const& lhs, T2 const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename T2, std::enable_if_t<!std::is_same_v<expected, std::decay_t<T2>>, int> = 0>
  friend auto
  operator!=(T2 const& lhs, expected const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename E2>
  friend auto
  operator==(expected const& lhs, unexpected<E2> const& rhs) -> bool
  {
    return !lhs.has_value() && lhs.error() == rhs.error();
  }
  template <typename E2>
  friend auto
  operator==(unexpected<E2> const& lhs, expected const& rhs) -> bool
  {
    return rhs == lhs;
  }
  template <typename E2>
  friend auto
  operator!=(expected const& lhs, unexpected<E2> const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename E2>
  friend auto
  operator!=(unexpected<E2> const& lhs, expected const& rhs) -> bool
  {
    return !(rhs == lhs);
  }

private:
  template <typename Self, typename F>
  static auto
  and_then_impl(Self&& self, F&& f)
  {
    using result_type = std::invoke_result_t<F, decltype(*std::forward<Self>(self))>;
    if (self.has_value())
      return std::invoke(std::forward<F>(f), *std::forward<Self>(self));
    return result_type(unexpect, std::forward<Self>(self).error());
  }
  template <typename Self, typename F>
  static auto
  or_else_impl(Self&& self, F&& f)
  {
    using result_type = std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>;
    if (self.has_value())
      return result_type(*std::forward<Self>(self));
    return std::invoke(std::forward<F>(f), std::forward<Self>(self).error());
  }
  template <typename Self, typename F>
  static auto
  transform_impl(Self&& self, F&& f)
  {
    using result_value = std::remove_cv_t<std::invoke_result_t<F, decltype(*std::forward<Self>(self))>>;
    if (self.has_value())
      {
        if constexpr (std::is_void_v<result_value>)
          {
            std::invoke(std::forward<F>(f), *std::forward<Self>(self));
            return expected<void, E>();
          }
        else
          return expected<result_value, E>(std::in_place, std::invoke(std::forward<F>(f), *std::forward<Self>(self)));
      }
    return expected<result_value, E>(unexpect, std::forward<Self>(self).error());
  }
  template <typename Self, typename F>
  static auto
  transform_error_impl(Self&& self, F&& f)
  {
    using result_error = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
    if (self.has_value())
      return expected<T, result_error>(*std::forward<Self>(self));
    return expected<T, result_error>(unexpect, std::invoke(std::forward<F>(f), std::forward<Self>(self).error()));
  }

  detail::expected_storage<T, E> storage_;
};

template <typename E>
class expected<void, E>
{
  using storage_type = detail::expected_storage<std::monostate, E>;

public:
  using value_type = void;
  using error_type = E;
  using unexpected_type = unexpected<E>;

  constexpr expected() : storage_(detail::expected_value_tag{}) {}
  expected(expected const&) = default;
  expected(expected&&) = default;
  auto operator=(expected const&) -> expected& = default;
  auto operator=(expected&&) -> expected& = default;
  ~expected() = default;

  template <typename... Args, std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
  constexpr explicit expected(unexpect_t, Args&&... args)
      : storage_(detail::expected_error_tag{}, std::forward<Args>(args)...)
  {
  }
  constexpr expected(unexpected<E> const& error) : storage_(detail::expected_error_tag{}, error.error()) {}
  constexpr expected(unexpected<E>&& error) : storage_(detail::expected_error_tag{}, std::move(error).error()) {}

  template <typename G, std::enable_if_t<std::is_constructible_v<E, G const&> && std::is_assignable_v<E&, G const&>
                                             && (std::is_nothrow_constructible_v<E, G const&>
                                                 || std::is_nothrow_move_constructible_v<E>),
                                         int> = 0>
  auto
  operator=(unexpected<G> const& error) -> expected&
  {
    storage_.assign_error(error.error());
    return *this;
  }
  template <typename G,
            std::enable_if_t<std::is_constructible_v<E, G> && std::is_assignable_v<E&, G>
                                 && (std::is_nothrow_constructible_v<E, G> || std::is_nothrow_move_constructible_v<E>),
                             int> = 0>
  auto
  operator=(unexpected<G>&& error) -> expected&
  {
    storage_.assign_error(std::move(error).error());
    return *this;
  }

  [[nodiscard]] constexpr auto
  has_value() const noexcept -> bool
  {
    return storage_.has_value();
  }
  constexpr explicit
  operator bool() const noexcept
  {
    return has_value();
  }
  void
  value() const
  {
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(storage_.error()));
  }
  constexpr auto
  error() & noexcept -> E&
  {
    return storage_.error();
  }
  [[nodiscard]] constexpr auto
  error() const& noexcept -> E const&
  {
    return storage_.error();
  }
  constexpr auto
  error() && noexcept -> E&&
  {
    return std::move(storage_).error();
  }
  [[nodiscard]] constexpr auto
  error() const&& noexcept -> E const&&
  {
    return std::move(storage_).error();
  }
  void
  emplace() noexcept
  {
    storage_.emplace_value();
  }
  void
  swap(expected& other) noexcept(std::is_nothrow_move_constructible_v<E> && std::is_nothrow_swappable_v<E>)
  {
    storage_.swap_storage(other.storage_);
  }

  template <typename F>
  auto
  and_then(F&& f) &
  {
    return and_then_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  and_then(F&& f) const&
  {
    return and_then_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  and_then(F&& f) &&
  {
    return and_then_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  and_then(F&& f) const&&
  {
    return and_then_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  or_else(F&& f) &
  {
    return or_else_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  or_else(F&& f) const&
  {
    return or_else_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  or_else(F&& f) &&
  {
    return or_else_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  or_else(F&& f) const&&
  {
    return or_else_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  transform(F&& f) &
  {
    return transform_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  transform(F&& f) const&
  {
    return transform_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  transform(F&& f) &&
  {
    return transform_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  transform(F&& f) const&&
  {
    return transform_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  transform_error(F&& f) &
  {
    return transform_error_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  transform_error(F&& f) const&
  {
    return transform_error_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  auto
  transform_error(F&& f) &&
  {
    return transform_error_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  auto
  transform_error(F&& f) const&&
  {
    return transform_error_impl(std::move(*this), std::forward<F>(f));
  }

#if THREADSCHEDULE_HAS_STD_EXPECTED
  template <typename G = E, std::enable_if_t<std::is_copy_constructible_v<G>, int> = 0>
  operator std::expected<void, E>() const&
  {
    if (has_value())
      return std::expected<void, E>();
    return std::expected<void, E>(std::unexpect, storage_.error());
  }
  template <typename G = E, std::enable_if_t<std::is_move_constructible_v<G>, int> = 0>
  operator std::expected<void, E>() &&
  {
    if (has_value())
      return std::expected<void, E>();
    return std::expected<void, E>(std::unexpect, std::move(storage_).error());
  }
#endif

  template <typename E2>
  friend auto
  operator==(expected const& lhs, expected<void, E2> const& rhs) -> bool
  {
    if (lhs.has_value() != rhs.has_value())
      return false;
    return lhs.has_value() || lhs.error() == rhs.error();
  }
  template <typename E2>
  friend auto
  operator!=(expected const& lhs, expected<void, E2> const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename E2>
  friend auto
  operator==(expected const& lhs, unexpected<E2> const& rhs) -> bool
  {
    return !lhs.has_value() && lhs.error() == rhs.error();
  }
  template <typename E2>
  friend auto
  operator==(unexpected<E2> const& lhs, expected const& rhs) -> bool
  {
    return rhs == lhs;
  }
  template <typename E2>
  friend auto
  operator!=(expected const& lhs, unexpected<E2> const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename E2>
  friend auto
  operator!=(unexpected<E2> const& lhs, expected const& rhs) -> bool
  {
    return !(rhs == lhs);
  }

private:
  template <typename Self, typename F>
  static auto
  and_then_impl(Self&& self, F&& f)
  {
    using result_type = std::invoke_result_t<F>;
    if (self.has_value())
      return std::invoke(std::forward<F>(f));
    return result_type(unexpect, std::forward<Self>(self).error());
  }
  template <typename Self, typename F>
  static auto
  or_else_impl(Self&& self, F&& f)
  {
    using result_type = std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>;
    if (self.has_value())
      return result_type();
    return std::invoke(std::forward<F>(f), std::forward<Self>(self).error());
  }
  template <typename Self, typename F>
  static auto
  transform_impl(Self&& self, F&& f)
  {
    using result_value = std::remove_cv_t<std::invoke_result_t<F>>;
    if (self.has_value())
      {
        if constexpr (std::is_void_v<result_value>)
          {
            std::invoke(std::forward<F>(f));
            return expected<void, E>();
          }
        else
          return expected<result_value, E>(std::in_place, std::invoke(std::forward<F>(f)));
      }
    return expected<result_value, E>(unexpect, std::forward<Self>(self).error());
  }
  template <typename Self, typename F>
  static auto
  transform_error_impl(Self&& self, F&& f)
  {
    using result_error = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
    if (self.has_value())
      return expected<void, result_error>();
    return expected<void, result_error>(unexpect, std::invoke(std::forward<F>(f), std::forward<Self>(self).error()));
  }

  storage_type storage_;
};

template <typename T, typename E>
void
swap(expected<T, E>& lhs, expected<T, E>& rhs) noexcept(noexcept(lhs.swap(rhs)))
{
  lhs.swap(rhs);
}

} // namespace threadschedule

#undef THREADSCHEDULE_EXPECTED_THROW
