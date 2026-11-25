#pragma once

/**
 * @file expected.hpp
 * @brief Stable library-owned expected implementation for ThreadSchedule APIs.
 */

#include "detail/lifetime.hpp"

#include <cassert>
#include <cstddef>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
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
protected:
  bad_expected_access() noexcept = default;
  bad_expected_access(bad_expected_access const&) = default;
  bad_expected_access(bad_expected_access&&) = default;
  auto operator=(bad_expected_access const&) -> bad_expected_access& = default;
  auto operator=(bad_expected_access&&) -> bad_expected_access& = default;
  ~bad_expected_access() override = default;

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
  what() const noexcept -> char const* override
  {
    return bad_expected_access<void>::what();
  }
  [[nodiscard]] auto
  error() const& noexcept THREADSCHEDULE_LIFETIMEBOUND -> E const&
  {
    return error_;
  }
  auto
  error() & noexcept THREADSCHEDULE_LIFETIMEBOUND -> E&
  {
    return error_;
  }
  [[nodiscard]] auto
  error() const&& noexcept THREADSCHEDULE_LIFETIMEBOUND -> E const&&
  {
    return std::move(error_);
  }
  auto
  error() && noexcept THREADSCHEDULE_LIFETIMEBOUND -> E&&
  {
    return std::move(error_);
  }

private:
  E error_;
};
/// @endcond

template <typename E>
class unexpected;

namespace detail
{

template <typename T>
using expected_remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename>
inline constexpr bool is_unexpected_v = false;
template <typename E>
inline constexpr bool is_unexpected_v<unexpected<E>> = true;

template <typename E>
inline constexpr bool valid_expected_error_v = std::is_object_v<E> && !std::is_array_v<E> && !std::is_const_v<E>
                                               && !std::is_volatile_v<E> && !is_unexpected_v<E>;

} // namespace detail

template <typename E>
class unexpected
{
  static_assert(detail::valid_expected_error_v<E>);

public:
  unexpected(unexpected const&) = default;
  unexpected(unexpected&&) = default;
  auto operator=(unexpected const&) -> unexpected& = default;
  auto operator=(unexpected&&) -> unexpected& = default;

  template <typename G = E, std::enable_if_t<!std::is_same_v<detail::expected_remove_cvref_t<G>, unexpected>
                                                 && !std::is_same_v<detail::expected_remove_cvref_t<G>, std::in_place_t>
                                                 && std::is_constructible_v<E, G>,
                                             int> = 0>
  constexpr explicit unexpected(G&& error) : error_(std::forward<G>(error))
  {
  }
  template <typename... Args, std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
  constexpr explicit unexpected(std::in_place_t, Args&&... args) : error_(std::forward<Args>(args)...)
  {
  }
  template <typename U, typename... Args,
            std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U>&, Args...>, int> = 0>
  constexpr explicit unexpected(std::in_place_t, std::initializer_list<U> list, Args&&... args)
      : error_(list, std::forward<Args>(args)...)
  {
  }
  [[nodiscard]] constexpr auto
  error() const& noexcept THREADSCHEDULE_LIFETIMEBOUND -> E const&
  {
    return error_;
  }
  constexpr auto
  error() & noexcept THREADSCHEDULE_LIFETIMEBOUND -> E&
  {
    return error_;
  }
  [[nodiscard]] constexpr auto
  error() const&& noexcept THREADSCHEDULE_LIFETIMEBOUND -> E const&&
  {
    return std::move(error_);
  }
  constexpr auto
  error() && noexcept THREADSCHEDULE_LIFETIMEBOUND -> E&&
  {
    return std::move(error_);
  }

  constexpr void
  swap(unexpected& other) noexcept(std::is_nothrow_swappable_v<E>)
  {
    static_assert(std::is_swappable_v<E>);
    using std::swap;
    swap(error_, other.error_);
  }

  template <typename G = E, std::enable_if_t<std::is_swappable_v<E>, G>* = nullptr>
  friend constexpr void
  swap(unexpected& lhs, unexpected& rhs) noexcept(noexcept(lhs.swap(rhs)))
  {
    lhs.swap(rhs);
  }

  template <typename G>
  friend constexpr auto
  operator==(unexpected const& lhs, unexpected<G> const& rhs) -> bool
  {
    return lhs.error() == rhs.error();
  }
  template <typename G>
  friend constexpr auto
  operator!=(unexpected const& lhs, unexpected<G> const& rhs) -> bool
  {
    return !(lhs == rhs);
  }

private:
  E error_;
};

template <typename E>
unexpected(E) -> unexpected<E>;

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
struct expected_from_other_tag
{
};
struct expected_invoke_tag
{
};
struct expected_void_value
{
};

template <bool Enabled>
struct expected_move_constructor
{
};
template <>
struct expected_move_constructor<false>
{
  expected_move_constructor() = default;
  expected_move_constructor(expected_move_constructor const&) = default;
  expected_move_constructor(expected_move_constructor&&) = delete;
  auto operator=(expected_move_constructor const&) -> expected_move_constructor& = default;
  auto operator=(expected_move_constructor&&) -> expected_move_constructor& = default;
  ~expected_move_constructor() = default;
};

constexpr auto
expected_in_constant_evaluation() noexcept -> bool
{
#if defined(__has_builtin)
#  if __has_builtin(__builtin_is_constant_evaluated)
  return __builtin_is_constant_evaluated();
#  else
  return false;
#  endif
#elif defined(_MSC_VER)
  return __builtin_is_constant_evaluated();
#else
  return false;
#endif
}

// Use ADL on the variant pointer to find std::construct_at when available.
template <std::size_t I, typename Storage, typename... Args>
constexpr auto
emplace_expected_storage(Storage& storage, int, Args&&... args) noexcept
    -> decltype(construct_at(std::addressof(storage), std::in_place_index<I>, std::in_place,
                             std::forward<Args>(args)...),
                void())
{
  if (!expected_in_constant_evaluation())
    {
      storage.template emplace<I>(std::in_place, std::forward<Args>(args)...);
      return;
    }

  // Reconstructing the whole variant avoids inactive union access in MSVC's constexpr emplace.
  auto* address = std::addressof(storage);
  std::destroy_at(address);
  construct_at(address, std::in_place_index<I>, std::in_place, std::forward<Args>(args)...);
}
template <std::size_t I, typename Storage, typename... Args>
constexpr void
emplace_expected_storage(Storage& storage, long, Args&&... args) noexcept
{
  storage.template emplace<I>(std::in_place, std::forward<Args>(args)...);
}

template <typename T, typename E>
class expected_void;

template <typename T>
inline constexpr bool valid_expected_value_v
    = std::is_void_v<T>
      || (std::is_object_v<T> && !std::is_array_v<T> && !std::is_same_v<std::remove_cv_t<T>, std::in_place_t>
          && !std::is_same_v<std::remove_cv_t<T>, unexpect_t> && !is_unexpected_v<std::remove_cv_t<T>>);

template <typename>
inline constexpr bool is_expected_v = false;
template <typename T, typename E>
inline constexpr bool is_expected_v<expected<T, E>> = true;

template <typename T, typename Source>
using constructs_from_expected
    = std::disjunction<std::is_constructible<T, Source&>, std::is_constructible<T, Source const&>,
                       std::is_constructible<T, Source&&>, std::is_constructible<T, Source const&&>>;

template <typename T, typename Source>
using converts_from_expected = std::disjunction<constructs_from_expected<T, Source>, std::is_convertible<Source&, T>,
                                                std::is_convertible<Source const&, T>, std::is_convertible<Source&&, T>,
                                                std::is_convertible<Source const&&, T>>;

template <typename T, typename E, typename Source, typename Value, typename Error>
inline constexpr bool expected_can_convert_v = std::conjunction_v<
    std::negation<std::is_same<expected<T, E>, Source>>, std::is_constructible<T, Value>,
    std::is_constructible<E, Error>,
    std::disjunction<std::is_same<std::remove_cv_t<T>, bool>, std::negation<converts_from_expected<T, Source>>>,
    std::negation<constructs_from_expected<unexpected<E>, Source>>>;

template <typename T, typename E, typename U>
inline constexpr bool expected_can_forward_v
    = !std::is_same_v<std::decay_t<U>, expected<T, E>> && !std::is_same_v<std::decay_t<U>, std::in_place_t>
      && !is_unexpected_v<expected_remove_cvref_t<U>>
      && !(std::is_same_v<std::remove_cv_t<T>, bool> && is_expected_v<std::decay_t<U>>)
      && std::is_constructible_v<T, U>;

template <typename F, typename T, typename From>
using expected_require_constructible
    = std::enable_if_t<std::conjunction_v<std::is_constructible<T, From>, std::is_same<F, F>>, int>;

template <typename T, typename E>
inline constexpr bool expected_can_swap_v
    = std::is_swappable_v<T> && std::is_swappable_v<E> && std::is_move_constructible_v<T>
      && std::is_move_constructible_v<E>
      && (std::is_nothrow_move_constructible_v<T> || std::is_nothrow_move_constructible_v<E>);

template <typename T, typename E, typename U, typename G, typename Error>
inline constexpr bool expected_void_can_convert_v
    = std::conjunction_v<std::negation<std::is_same<expected<T, E>, expected<U, G>>>, std::is_void<U>,
                         std::is_constructible<E, Error>,
                         std::negation<constructs_from_expected<unexpected<E>, expected<U, G>>>>;

template <typename T>
struct expected_value_holder
{
  template <typename... Args>
  explicit constexpr expected_value_holder(std::in_place_t,
                                           Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
      : value(std::forward<Args>(args)...)
  {
  }
  template <typename F, typename... Args>
  explicit constexpr expected_value_holder(std::in_place_t, expected_invoke_tag, F&& f, Args&&... args)
      : value(std::invoke(std::forward<F>(f), std::forward<Args>(args)...))
  {
  }
  T value;
};

template <typename E>
struct expected_error_holder
{
  template <typename... Args>
  explicit constexpr expected_error_holder(std::in_place_t,
                                           Args&&... args) noexcept(std::is_nothrow_constructible_v<E, Args...>)
      : error(std::forward<Args>(args)...)
  {
  }
  template <typename F, typename... Args>
  explicit constexpr expected_error_holder(std::in_place_t, expected_invoke_tag, F&& f, Args&&... args)
      : error(std::invoke(std::forward<F>(f), std::forward<Args>(args)...))
  {
  }
  E error;
};

template <typename T, typename E>
class expected_storage_common
    : private expected_move_constructor<std::is_move_constructible_v<T> && std::is_move_constructible_v<E>>
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

  template <typename Source>
  explicit constexpr expected_storage_common(expected_from_other_tag, Source&& source)
      : data_(make_from_other(std::forward<Source>(source)))
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
  value() & noexcept THREADSCHEDULE_LIFETIMEBOUND -> T&
  {
    return std::get<0>(data_).value;
  }
  [[nodiscard]] constexpr auto
  value() const& noexcept THREADSCHEDULE_LIFETIMEBOUND -> T const&
  {
    return std::get<0>(data_).value;
  }
  constexpr auto
  value() && noexcept THREADSCHEDULE_LIFETIMEBOUND -> T&&
  {
    return std::move(std::get<0>(data_).value);
  }
  [[nodiscard]] constexpr auto
  value() const&& noexcept THREADSCHEDULE_LIFETIMEBOUND -> T const&&
  {
    return std::move(std::get<0>(data_).value);
  }
  constexpr auto
  error() & noexcept THREADSCHEDULE_LIFETIMEBOUND -> E&
  {
    return std::get<1>(data_).error;
  }
  [[nodiscard]] constexpr auto
  error() const& noexcept THREADSCHEDULE_LIFETIMEBOUND -> E const&
  {
    return std::get<1>(data_).error;
  }
  constexpr auto
  error() && noexcept THREADSCHEDULE_LIFETIMEBOUND -> E&&
  {
    return std::move(std::get<1>(data_).error);
  }
  [[nodiscard]] constexpr auto
  error() const&& noexcept THREADSCHEDULE_LIFETIMEBOUND -> E const&&
  {
    return std::move(std::get<1>(data_).error);
  }

  template <typename U>
  constexpr void
  assign_value(U&& source)
  {
    if (has_value())
      value() = std::forward<U>(source);
    else
      replace_with_value(std::forward<U>(source));
  }

  template <typename G>
  constexpr void
  assign_error(G&& source)
  {
    if (has_value())
      replace_with_error(std::forward<G>(source));
    else
      error() = std::forward<G>(source);
  }

  template <typename... Args>
  constexpr auto
  emplace_value(Args&&... args) noexcept -> T&
  {
    emplace_nothrow<0>(std::forward<Args>(args)...);
    return value();
  }

  constexpr void
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

  constexpr void
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

  constexpr void
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
  using storage_type = std::variant<value_holder, error_holder>;

  template <typename Source>
  static constexpr auto
  make_from_other(Source&& source) -> storage_type
  {
    if (!source.has_value())
      return storage_type(std::in_place_index<1>, std::in_place, std::forward<Source>(source).error());
    if constexpr (std::is_void_v<typename std::remove_reference_t<Source>::value_type>)
      return storage_type(std::in_place_index<0>, std::in_place);
    else
      return storage_type(std::in_place_index<0>, std::in_place, *std::forward<Source>(source));
  }

  template <std::size_t I, typename... Args>
  constexpr void
  emplace_nothrow(Args&&... args) noexcept
  {
    emplace_expected_storage<I>(data_, 0, std::forward<Args>(args)...);
  }

#ifdef __cpp_exceptions
  template <std::size_t I, typename Backup, typename... Args>
  void
  emplace_with_rollback(Backup& backup, Args&&... args)
  {
    try
      {
        data_.template emplace<I>(std::in_place, std::forward<Args>(args)...);
      }
    catch (...)
      {
        data_.template emplace<1 - I>(std::in_place, std::move(backup));
        throw;
      }
  }
#endif

  template <std::size_t I, typename Backup, typename... Args>
  constexpr void
  emplace_with_backup(Backup&& backup, Args&&... args)
  {
#ifdef __cpp_exceptions
    // An exception already makes constant evaluation fail. Roll back at runtime.
    if (!expected_in_constant_evaluation())
      {
        emplace_with_rollback<I>(backup, std::forward<Args>(args)...);
        return;
      }
#endif
    (void)backup;
    data_.template emplace<I>(std::in_place, std::forward<Args>(args)...);
  }

  template <typename U>
  constexpr void
  replace_with_value(U&& source)
  {
    if constexpr (std::is_nothrow_constructible_v<T, U>)
      emplace_nothrow<0>(std::forward<U>(source));
    else if constexpr (std::is_nothrow_move_constructible_v<T>)
      {
        T replacement(std::forward<U>(source));
        emplace_nothrow<0>(std::move(replacement));
      }
    else
      {
        static_assert(std::is_nothrow_move_constructible_v<E>);
        E backup(std::move(error()));
        emplace_with_backup<0>(backup, std::forward<U>(source));
      }
  }

  template <typename G>
  constexpr void
  replace_with_error(G&& source)
  {
    if constexpr (std::is_nothrow_constructible_v<E, G>)
      emplace_nothrow<1>(std::forward<G>(source));
    else if constexpr (std::is_same_v<T, expected_void_value>)
      emplace_with_backup<1>(expected_void_value{}, std::forward<G>(source));
    else if constexpr (std::is_nothrow_move_constructible_v<E>)
      {
        E replacement(std::forward<G>(source));
        emplace_nothrow<1>(std::move(replacement));
      }
    else
      {
        static_assert(std::is_nothrow_move_constructible_v<T>);
        T backup(std::move(value()));
        emplace_with_backup<1>(backup, std::forward<G>(source));
      }
  }

  static constexpr void
  swap_value_error(expected_storage_common& with_value, expected_storage_common& with_error)
  {
    if constexpr (std::is_same_v<T, expected_void_value>)
      {
        with_value.replace_with_error(std::move(with_error.error()));
        with_error.emplace_value();
      }
    else if constexpr (std::is_nothrow_move_constructible_v<E>)
      {
        E backup(std::move(with_error.error()));
        with_error.template emplace_with_backup<0>(backup, std::move(with_value.value()));
        with_value.template emplace_nothrow<1>(std::move(backup));
      }
    else
      {
        static_assert(std::is_nothrow_move_constructible_v<T>);
        T backup(std::move(with_value.value()));
        with_value.template emplace_with_backup<1>(backup, std::move(with_error.error()));
        with_error.template emplace_nothrow<0>(std::move(backup));
      }
  }

  storage_type data_;
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
  constexpr auto
  operator=(expected_storage const& other) -> expected_storage&
  {
    this->copy_assign(other);
    return *this;
  }
  constexpr auto
  operator=(expected_storage&& other) noexcept(std::is_nothrow_move_constructible_v<T>
                                               && std::is_nothrow_move_constructible_v<E>
                                               && std::is_nothrow_move_assignable_v<T>
                                               && std::is_nothrow_move_assignable_v<E>) -> expected_storage&
  {
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
  constexpr auto
  operator=(expected_storage const& other) -> expected_storage&
  {
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
  constexpr auto
  operator=(expected_storage&& other) noexcept(std::is_nothrow_move_constructible_v<T>
                                               && std::is_nothrow_move_constructible_v<E>
                                               && std::is_nothrow_move_assignable_v<T>
                                               && std::is_nothrow_move_assignable_v<E>) -> expected_storage&
  {
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
  static_assert(!std::is_void_v<T> && detail::valid_expected_value_v<T>);
  static_assert(detail::valid_expected_error_v<E>);

public:
  using value_type = T;
  using error_type = E;
  using unexpected_type = unexpected<E>;
  template <typename U>
  using rebind = expected<U, E>;

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

  template <typename U = T,
            std::enable_if_t<detail::expected_can_forward_v<T, E, U> && std::is_convertible_v<U, T>, int> = 0>
  constexpr expected(U&& value) : storage_(detail::expected_value_tag{}, std::forward<U>(value))
  {
  }
  template <typename U = T,
            std::enable_if_t<detail::expected_can_forward_v<T, E, U> && !std::is_convertible_v<U, T>, int> = 0>
  constexpr explicit expected(U&& value) : storage_(detail::expected_value_tag{}, std::forward<U>(value))
  {
  }

  template <typename U, typename G,
            std::enable_if_t<detail::expected_can_convert_v<T, E, expected<U, G>, U const&, G const&>
                                 && std::is_convertible_v<U const&, T> && std::is_convertible_v<G const&, E>,
                             int> = 0>
  constexpr expected(expected<U, G> const& source) : storage_(detail::expected_from_other_tag{}, source)
  {
  }
  template <typename U, typename G,
            std::enable_if_t<detail::expected_can_convert_v<T, E, expected<U, G>, U const&, G const&>
                                 && !(std::is_convertible_v<U const&, T> && std::is_convertible_v<G const&, E>),
                             int> = 0>
  constexpr explicit expected(expected<U, G> const& source) : storage_(detail::expected_from_other_tag{}, source)
  {
  }
  template <typename U, typename G,
            std::enable_if_t<detail::expected_can_convert_v<T, E, expected<U, G>, U, G> && std::is_convertible_v<U, T>
                                 && std::is_convertible_v<G, E>,
                             int> = 0>
  constexpr expected(expected<U, G>&& source) : storage_(detail::expected_from_other_tag{}, std::move(source))
  {
  }
  template <typename U, typename G,
            std::enable_if_t<detail::expected_can_convert_v<T, E, expected<U, G>, U, G>
                                 && !(std::is_convertible_v<U, T> && std::is_convertible_v<G, E>),
                             int> = 0>
  constexpr explicit expected(expected<U, G>&& source) : storage_(detail::expected_from_other_tag{}, std::move(source))
  {
  }

  template <typename... Args, std::enable_if_t<std::is_constructible_v<T, Args...>, int> = 0>
  constexpr explicit expected(std::in_place_t, Args&&... args)
      : storage_(detail::expected_value_tag{}, std::forward<Args>(args)...)
  {
  }
  template <typename U, typename... Args,
            std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args...>, int> = 0>
  constexpr explicit expected(std::in_place_t, std::initializer_list<U> list, Args&&... args)
      : storage_(detail::expected_value_tag{}, list, std::forward<Args>(args)...)
  {
  }
  template <typename G,
            std::enable_if_t<std::is_constructible_v<E, G const&> && std::is_convertible_v<G const&, E>, int> = 0>
  constexpr expected(unexpected<G> const& source) : storage_(detail::expected_error_tag{}, source.error())
  {
  }
  template <typename G,
            std::enable_if_t<std::is_constructible_v<E, G const&> && !std::is_convertible_v<G const&, E>, int> = 0>
  constexpr explicit expected(unexpected<G> const& source) : storage_(detail::expected_error_tag{}, source.error())
  {
  }
  template <typename G, std::enable_if_t<std::is_constructible_v<E, G> && std::is_convertible_v<G, E>, int> = 0>
  constexpr expected(unexpected<G>&& source) : storage_(detail::expected_error_tag{}, std::move(source).error())
  {
  }
  template <typename G, std::enable_if_t<std::is_constructible_v<E, G> && !std::is_convertible_v<G, E>, int> = 0>
  constexpr explicit expected(unexpected<G>&& source)
      : storage_(detail::expected_error_tag{}, std::move(source).error())
  {
  }
  template <typename... Args, std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
  constexpr explicit expected(unexpect_t, Args&&... args)
      : storage_(detail::expected_error_tag{}, std::forward<Args>(args)...)
  {
  }

  template <typename U, typename... Args,
            std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U>&, Args...>, int> = 0>
  constexpr explicit expected(unexpect_t, std::initializer_list<U> list, Args&&... args)
      : storage_(detail::expected_error_tag{}, list, std::forward<Args>(args)...)
  {
  }

  template <typename U = T,
            std::enable_if_t<!std::is_same_v<detail::expected_remove_cvref_t<U>, expected>
                                 && !detail::is_unexpected_v<detail::expected_remove_cvref_t<U>>
                                 && std::is_constructible_v<T, U> && std::is_assignable_v<T&, U>
                                 && (std::is_nothrow_constructible_v<T, U> || std::is_nothrow_move_constructible_v<T>
                                     || std::is_nothrow_move_constructible_v<E>),
                             int> = 0>
  constexpr auto
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
  constexpr auto
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
  constexpr auto
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
  operator->() noexcept THREADSCHEDULE_LIFETIMEBOUND -> T*
  {
    return std::addressof(storage_.value());
  }
  constexpr auto
  operator->() const noexcept THREADSCHEDULE_LIFETIMEBOUND -> T const*
  {
    return std::addressof(storage_.value());
  }
  constexpr auto
  operator*() & noexcept THREADSCHEDULE_LIFETIMEBOUND -> T&
  {
    return storage_.value();
  }
  [[nodiscard]] constexpr auto
  operator*() const& noexcept THREADSCHEDULE_LIFETIMEBOUND -> T const&
  {
    return storage_.value();
  }
  constexpr auto
  operator*() && noexcept THREADSCHEDULE_LIFETIMEBOUND -> T&&
  {
    return std::move(storage_).value();
  }
  [[nodiscard]] constexpr auto
  operator*() const&& noexcept THREADSCHEDULE_LIFETIMEBOUND -> T const&&
  {
    return std::move(storage_).value();
  }

  constexpr auto
  value() & THREADSCHEDULE_LIFETIMEBOUND -> T&
  {
    static_assert(std::is_copy_constructible_v<E>);
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(std::as_const(storage_).error()));
    return storage_.value();
  }
  [[nodiscard]] constexpr auto
  value() const& THREADSCHEDULE_LIFETIMEBOUND -> T const&
  {
    static_assert(std::is_copy_constructible_v<E>);
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(storage_.error()));
    return storage_.value();
  }
  constexpr auto
  value() && THREADSCHEDULE_LIFETIMEBOUND -> T&&
  {
    static_assert(std::is_copy_constructible_v<E> && std::is_constructible_v<E, E&&>);
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(std::move(storage_).error()));
    return std::move(storage_).value();
  }
  [[nodiscard]] constexpr auto
  value() const&& THREADSCHEDULE_LIFETIMEBOUND -> T const&&
  {
    static_assert(std::is_copy_constructible_v<E> && std::is_constructible_v<E, E const&&>);
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(std::move(storage_).error()));
    return std::move(storage_).value();
  }
  constexpr auto
  error() & noexcept THREADSCHEDULE_LIFETIMEBOUND -> E&
  {
    return storage_.error();
  }
  [[nodiscard]] constexpr auto
  error() const& noexcept THREADSCHEDULE_LIFETIMEBOUND -> E const&
  {
    return storage_.error();
  }
  constexpr auto
  error() && noexcept THREADSCHEDULE_LIFETIMEBOUND -> E&&
  {
    return std::move(storage_).error();
  }
  [[nodiscard]] constexpr auto
  error() const&& noexcept THREADSCHEDULE_LIFETIMEBOUND -> E const&&
  {
    return std::move(storage_).error();
  }

  template <typename U>
  constexpr auto
  value_or(U&& fallback) const& -> T
  {
    static_assert(std::is_copy_constructible_v<T> && std::is_convertible_v<U, T>);
    return has_value() ? storage_.value() : static_cast<T>(std::forward<U>(fallback));
  }
  template <typename U>
  constexpr auto
  value_or(U&& fallback) && -> T
  {
    static_assert(std::is_move_constructible_v<T> && std::is_convertible_v<U, T>);
    return has_value() ? std::move(storage_).value() : static_cast<T>(std::forward<U>(fallback));
  }
  template <typename G = E>
  constexpr auto
  error_or(G&& fallback) const& -> E
  {
    static_assert(std::is_copy_constructible_v<E> && std::is_convertible_v<G, E>);
    if (has_value())
      return std::forward<G>(fallback);
    return storage_.error();
  }
  template <typename G = E>
  constexpr auto
  error_or(G&& fallback) && -> E
  {
    static_assert(std::is_move_constructible_v<E> && std::is_convertible_v<G, E>);
    if (has_value())
      return std::forward<G>(fallback);
    return std::move(storage_).error();
  }
  template <typename... Args, std::enable_if_t<std::is_nothrow_constructible_v<T, Args...>, int> = 0>
  constexpr auto
  emplace(Args&&... args) noexcept -> T&
  {
    return storage_.emplace_value(std::forward<Args>(args)...);
  }
  template <typename U, typename... Args,
            std::enable_if_t<std::is_nothrow_constructible_v<T, std::initializer_list<U>&, Args...>, int> = 0>
  constexpr auto
  emplace(std::initializer_list<U> list, Args&&... args) noexcept -> T&
  {
    return storage_.emplace_value(list, std::forward<Args>(args)...);
  }
  template <typename U = T, std::enable_if_t<detail::expected_can_swap_v<T, E>, U>* = nullptr>
  constexpr void
  swap(expected& other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>
                                 && std::is_nothrow_swappable_v<T> && std::is_nothrow_swappable_v<E>)
  {
    storage_.swap_storage(other.storage_);
  }

  template <typename F, detail::expected_require_constructible<F, E, E&> = 0>
  constexpr auto
  and_then(F&& f) &
  {
    return and_then_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E const&> = 0>
  constexpr auto
  and_then(F&& f) const&
  {
    return and_then_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E&&> = 0>
  constexpr auto
  and_then(F&& f) &&
  {
    return and_then_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E const&&> = 0>
  constexpr auto
  and_then(F&& f) const&&
  {
    return and_then_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, T, T&> = 0>
  constexpr auto
  or_else(F&& f) &
  {
    return or_else_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, T, T const&> = 0>
  constexpr auto
  or_else(F&& f) const&
  {
    return or_else_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, T, T&&> = 0>
  constexpr auto
  or_else(F&& f) &&
  {
    return or_else_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, T, T const&&> = 0>
  constexpr auto
  or_else(F&& f) const&&
  {
    return or_else_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E&> = 0>
  constexpr auto
  transform(F&& f) &
  {
    return transform_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E const&> = 0>
  constexpr auto
  transform(F&& f) const&
  {
    return transform_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E&&> = 0>
  constexpr auto
  transform(F&& f) &&
  {
    return transform_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E const&&> = 0>
  constexpr auto
  transform(F&& f) const&&
  {
    return transform_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, T, T&> = 0>
  constexpr auto
  transform_error(F&& f) &
  {
    return transform_error_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, T, T const&> = 0>
  constexpr auto
  transform_error(F&& f) const&
  {
    return transform_error_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, T, T&&> = 0>
  constexpr auto
  transform_error(F&& f) &&
  {
    return transform_error_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, T, T const&&> = 0>
  constexpr auto
  transform_error(F&& f) const&&
  {
    return transform_error_impl(std::move(*this), std::forward<F>(f));
  }

#if THREADSCHEDULE_HAS_STD_EXPECTED
  template <typename U = T, typename G = E,
            std::enable_if_t<std::is_copy_constructible_v<U> && std::is_copy_constructible_v<G>, int> = 0>
  constexpr
  operator std::expected<T, E>() const&
  {
    if (has_value())
      return std::expected<T, E>(std::in_place, storage_.value());
    return std::expected<T, E>(std::unexpect, storage_.error());
  }
  template <typename U = T, typename G = E,
            std::enable_if_t<std::is_move_constructible_v<U> && std::is_move_constructible_v<G>, int> = 0>
  constexpr
  operator std::expected<T, E>() &&
  {
    if (has_value())
      return std::expected<T, E>(std::in_place, std::move(storage_).value());
    return std::expected<T, E>(std::unexpect, std::move(storage_).error());
  }
#endif

  template <typename T2, typename E2, std::enable_if_t<!std::is_void_v<T2>, int> = 0>
  friend constexpr auto
  operator==(expected const& lhs, expected<T2, E2> const& rhs) -> bool
  {
    if (lhs.has_value() != rhs.has_value())
      return false;
    if (lhs.has_value())
      return *lhs == *rhs;
    return lhs.error() == rhs.error();
  }
  template <typename T2, typename E2, std::enable_if_t<!std::is_void_v<T2>, int> = 0>
  friend constexpr auto
  operator!=(expected const& lhs, expected<T2, E2> const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename T2, std::enable_if_t<!detail::is_expected_v<T2> && !detail::is_unexpected_v<T2>, int> = 0>
  friend constexpr auto
  operator==(expected const& lhs, T2 const& rhs) -> bool
  {
    if (!lhs.has_value())
      return false;
    return *lhs == rhs;
  }
  template <typename T2, std::enable_if_t<!detail::is_expected_v<T2> && !detail::is_unexpected_v<T2>, int> = 0>
  friend constexpr auto
  operator==(T2 const& lhs, expected const& rhs) -> bool
  {
    return rhs == lhs;
  }
  template <typename T2, std::enable_if_t<!detail::is_expected_v<T2> && !detail::is_unexpected_v<T2>, int> = 0>
  friend constexpr auto
  operator!=(expected const& lhs, T2 const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename T2, std::enable_if_t<!detail::is_expected_v<T2> && !detail::is_unexpected_v<T2>, int> = 0>
  friend constexpr auto
  operator!=(T2 const& lhs, expected const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename E2>
  friend constexpr auto
  operator==(expected const& lhs, unexpected<E2> const& rhs) -> bool
  {
    if (lhs.has_value())
      return false;
    return lhs.error() == rhs.error();
  }
  template <typename E2>
  friend constexpr auto
  operator==(unexpected<E2> const& lhs, expected const& rhs) -> bool
  {
    return rhs == lhs;
  }
  template <typename E2>
  friend constexpr auto
  operator!=(expected const& lhs, unexpected<E2> const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename E2>
  friend constexpr auto
  operator!=(unexpected<E2> const& lhs, expected const& rhs) -> bool
  {
    return !(rhs == lhs);
  }

private:
  template <typename, typename>
  friend class expected;
  template <typename, typename>
  friend class detail::expected_void;

  template <typename Tag, typename F, typename... Args>
  constexpr explicit expected(Tag tag, detail::expected_invoke_tag, F&& f, Args&&... args)
      : storage_(tag, detail::expected_invoke_tag{}, std::forward<F>(f), std::forward<Args>(args)...)
  {
  }

  template <typename Self, typename F>
  static constexpr auto
  and_then_impl(Self&& self, F&& f)
  {
    using result_type = detail::expected_remove_cvref_t<std::invoke_result_t<F, decltype(*std::forward<Self>(self))>>;
    static_assert(detail::is_expected_v<result_type> && std::is_same_v<typename result_type::error_type, E>);
    if (self.has_value())
      return result_type(std::invoke(std::forward<F>(f), *std::forward<Self>(self)));
    return result_type(unexpect, std::forward<Self>(self).error());
  }
  template <typename Self, typename F>
  static constexpr auto
  or_else_impl(Self&& self, F&& f)
  {
    using result_type
        = detail::expected_remove_cvref_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
    static_assert(detail::is_expected_v<result_type> && std::is_same_v<typename result_type::value_type, T>);
    if (self.has_value())
      return result_type(std::in_place, *std::forward<Self>(self));
    return result_type(std::invoke(std::forward<F>(f), std::forward<Self>(self).error()));
  }
  template <typename Self, typename F>
  static constexpr auto
  transform_impl(Self&& self, F&& f)
  {
    using result_value = std::remove_cv_t<std::invoke_result_t<F, decltype(*std::forward<Self>(self))>>;
    static_assert(detail::valid_expected_value_v<result_value>);
    using result_type = expected<result_value, E>;
    if (!self.has_value())
      return result_type(unexpect, std::forward<Self>(self).error());
    if constexpr (std::is_void_v<result_value>)
      {
        std::invoke(std::forward<F>(f), *std::forward<Self>(self));
        return result_type();
      }
    else
      return result_type(detail::expected_value_tag{}, detail::expected_invoke_tag{}, std::forward<F>(f),
                         *std::forward<Self>(self));
  }
  template <typename Self, typename F>
  static constexpr auto
  transform_error_impl(Self&& self, F&& f)
  {
    using result_error = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
    static_assert(detail::valid_expected_error_v<result_error>);
    using result_type = expected<T, result_error>;
    if (self.has_value())
      return result_type(std::in_place, *std::forward<Self>(self));
    return result_type(detail::expected_error_tag{}, detail::expected_invoke_tag{}, std::forward<F>(f),
                       std::forward<Self>(self).error());
  }

  detail::expected_storage<T, E> storage_;
};

namespace detail
{

template <typename T, typename E>
class expected_void
{
  static_assert(std::is_void_v<T> && valid_expected_error_v<E>);
  using storage_type = expected_storage<expected_void_value, E>;
  using derived_type = expected<T, E>;

public:
  using value_type = T;
  using error_type = E;
  using unexpected_type = unexpected<E>;

  template <typename U>
  using rebind = expected<U, E>;

  constexpr expected_void() noexcept : expected_void(std::in_place) {}
  constexpr explicit expected_void(std::in_place_t) noexcept : storage_(detail::expected_value_tag{}) {}

  template <
      typename U, typename G,
      std::enable_if_t<detail::expected_void_can_convert_v<T, E, U, G, G const&> && std::is_convertible_v<G const&, E>,
                       int> = 0>
  constexpr expected_void(expected<U, G> const& source) : storage_(detail::expected_from_other_tag{}, source)
  {
  }
  template <
      typename U, typename G,
      std::enable_if_t<detail::expected_void_can_convert_v<T, E, U, G, G const&> && !std::is_convertible_v<G const&, E>,
                       int> = 0>
  constexpr explicit expected_void(expected<U, G> const& source) : storage_(detail::expected_from_other_tag{}, source)
  {
  }
  template <typename U, typename G,
            std::enable_if_t<detail::expected_void_can_convert_v<T, E, U, G, G> && std::is_convertible_v<G, E>, int>
            = 0>
  constexpr expected_void(expected<U, G>&& source) : storage_(detail::expected_from_other_tag{}, std::move(source))
  {
  }
  template <typename U, typename G,
            std::enable_if_t<detail::expected_void_can_convert_v<T, E, U, G, G> && !std::is_convertible_v<G, E>, int>
            = 0>
  constexpr explicit expected_void(expected<U, G>&& source)
      : storage_(detail::expected_from_other_tag{}, std::move(source))
  {
  }

  template <typename... Args, std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
  constexpr explicit expected_void(unexpect_t, Args&&... args)
      : storage_(detail::expected_error_tag{}, std::forward<Args>(args)...)
  {
  }
  template <typename G,
            std::enable_if_t<std::is_constructible_v<E, G const&> && std::is_convertible_v<G const&, E>, int> = 0>
  constexpr expected_void(unexpected<G> const& source) : storage_(detail::expected_error_tag{}, source.error())
  {
  }
  template <typename G,
            std::enable_if_t<std::is_constructible_v<E, G const&> && !std::is_convertible_v<G const&, E>, int> = 0>
  constexpr explicit expected_void(unexpected<G> const& source) : storage_(detail::expected_error_tag{}, source.error())
  {
  }
  template <typename G, std::enable_if_t<std::is_constructible_v<E, G> && std::is_convertible_v<G, E>, int> = 0>
  constexpr expected_void(unexpected<G>&& source) : storage_(detail::expected_error_tag{}, std::move(source).error())
  {
  }
  template <typename G, std::enable_if_t<std::is_constructible_v<E, G> && !std::is_convertible_v<G, E>, int> = 0>
  constexpr explicit expected_void(unexpected<G>&& source)
      : storage_(detail::expected_error_tag{}, std::move(source).error())
  {
  }
  template <typename U, typename... Args,
            std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U>&, Args...>, int> = 0>
  constexpr explicit expected_void(unexpect_t, std::initializer_list<U> list, Args&&... args)
      : storage_(detail::expected_error_tag{}, list, std::forward<Args>(args)...)
  {
  }
  template <typename G,
            std::enable_if_t<std::is_constructible_v<E, G const&> && std::is_assignable_v<E&, G const&>, int> = 0>
  constexpr auto
  operator=(unexpected<G> const& error) -> derived_type&
  {
    storage_.assign_error(error.error());
    return static_cast<derived_type&>(*this);
  }
  template <typename G, std::enable_if_t<std::is_constructible_v<E, G> && std::is_assignable_v<E&, G>, int> = 0>
  constexpr auto
  operator=(unexpected<G>&& error) -> derived_type&
  {
    storage_.assign_error(std::move(error).error());
    return static_cast<derived_type&>(*this);
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
  constexpr void
  operator*() const noexcept
  {
    assert(has_value());
  }
  constexpr void
  value() const&
  {
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(storage_.error()));
  }
  constexpr void
  value() &&
  {
    if (!has_value())
      THREADSCHEDULE_EXPECTED_THROW(bad_expected_access<E>(std::move(storage_).error()));
  }
  constexpr auto
  error() & noexcept THREADSCHEDULE_LIFETIMEBOUND -> E&
  {
    return storage_.error();
  }
  [[nodiscard]] constexpr auto
  error() const& noexcept THREADSCHEDULE_LIFETIMEBOUND -> E const&
  {
    return storage_.error();
  }
  constexpr auto
  error() && noexcept THREADSCHEDULE_LIFETIMEBOUND -> E&&
  {
    return std::move(storage_).error();
  }
  [[nodiscard]] constexpr auto
  error() const&& noexcept THREADSCHEDULE_LIFETIMEBOUND -> E const&&
  {
    return std::move(storage_).error();
  }
  template <typename G = E>
  constexpr auto
  error_or(G&& fallback) const& -> E
  {
    static_assert(std::is_copy_constructible_v<E> && std::is_convertible_v<G, E>);
    if (has_value())
      return std::forward<G>(fallback);
    return storage_.error();
  }
  template <typename G = E>
  constexpr auto
  error_or(G&& fallback) && -> E
  {
    static_assert(std::is_move_constructible_v<E> && std::is_convertible_v<G, E>);
    if (has_value())
      return std::forward<G>(fallback);
    return std::move(storage_).error();
  }
  constexpr void
  emplace() noexcept
  {
    if (!has_value())
      storage_.emplace_value();
  }
  template <typename G = E, std::enable_if_t<std::is_move_constructible_v<E> && std::is_swappable_v<E>, G>* = nullptr>
  constexpr void
  swap(derived_type& other) noexcept(std::is_nothrow_move_constructible_v<E> && std::is_nothrow_swappable_v<E>)
  {
    storage_.swap_storage(other.storage_);
  }

  template <typename F, detail::expected_require_constructible<F, E, E&> = 0>
  constexpr auto
  and_then(F&& f) &
  {
    return and_then_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E const&> = 0>
  constexpr auto
  and_then(F&& f) const&
  {
    return and_then_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E&&> = 0>
  constexpr auto
  and_then(F&& f) &&
  {
    return and_then_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E const&&> = 0>
  constexpr auto
  and_then(F&& f) const&&
  {
    return and_then_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  constexpr auto
  or_else(F&& f) &
  {
    return or_else_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  constexpr auto
  or_else(F&& f) const&
  {
    return or_else_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  constexpr auto
  or_else(F&& f) &&
  {
    return or_else_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  constexpr auto
  or_else(F&& f) const&&
  {
    return or_else_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E&> = 0>
  constexpr auto
  transform(F&& f) &
  {
    return transform_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E const&> = 0>
  constexpr auto
  transform(F&& f) const&
  {
    return transform_impl(*this, std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E&&> = 0>
  constexpr auto
  transform(F&& f) &&
  {
    return transform_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F, detail::expected_require_constructible<F, E, E const&&> = 0>
  constexpr auto
  transform(F&& f) const&&
  {
    return transform_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  constexpr auto
  transform_error(F&& f) &
  {
    return transform_error_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  constexpr auto
  transform_error(F&& f) const&
  {
    return transform_error_impl(*this, std::forward<F>(f));
  }
  template <typename F>
  constexpr auto
  transform_error(F&& f) &&
  {
    return transform_error_impl(std::move(*this), std::forward<F>(f));
  }
  template <typename F>
  constexpr auto
  transform_error(F&& f) const&&
  {
    return transform_error_impl(std::move(*this), std::forward<F>(f));
  }

#if THREADSCHEDULE_HAS_STD_EXPECTED
  template <typename G = E, std::enable_if_t<std::is_copy_constructible_v<G>, int> = 0>
  constexpr
  operator std::expected<T, E>() const&
  {
    if (has_value())
      return std::expected<T, E>();
    return std::expected<T, E>(std::unexpect, storage_.error());
  }
  template <typename G = E, std::enable_if_t<std::is_move_constructible_v<G>, int> = 0>
  constexpr
  operator std::expected<T, E>() &&
  {
    if (has_value())
      return std::expected<T, E>();
    return std::expected<T, E>(std::unexpect, std::move(storage_).error());
  }
#endif

  template <typename T2, typename E2, std::enable_if_t<std::is_void_v<T2>, int> = 0>
  friend constexpr auto
  operator==(derived_type const& lhs, expected<T2, E2> const& rhs) -> bool
  {
    if (lhs.has_value() != rhs.has_value())
      return false;
    if (lhs.has_value())
      return true;
    return lhs.error() == rhs.error();
  }
  template <typename T2, typename E2, std::enable_if_t<std::is_void_v<T2>, int> = 0>
  friend constexpr auto
  operator!=(derived_type const& lhs, expected<T2, E2> const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename E2>
  friend constexpr auto
  operator==(derived_type const& lhs, unexpected<E2> const& rhs) -> bool
  {
    if (lhs.has_value())
      return false;
    return lhs.error() == rhs.error();
  }
  template <typename E2>
  friend constexpr auto
  operator==(unexpected<E2> const& lhs, derived_type const& rhs) -> bool
  {
    return rhs == lhs;
  }
  template <typename E2>
  friend constexpr auto
  operator!=(derived_type const& lhs, unexpected<E2> const& rhs) -> bool
  {
    return !(lhs == rhs);
  }
  template <typename E2>
  friend constexpr auto
  operator!=(unexpected<E2> const& lhs, derived_type const& rhs) -> bool
  {
    return !(rhs == lhs);
  }

private:
  template <typename Self, typename F>
  static constexpr auto
  and_then_impl(Self&& self, F&& f)
  {
    using result_type = expected_remove_cvref_t<std::invoke_result_t<F>>;
    static_assert(is_expected_v<result_type> && std::is_same_v<typename result_type::error_type, E>);
    if (self.has_value())
      return result_type(std::invoke(std::forward<F>(f)));
    return result_type(unexpect, std::forward<Self>(self).error());
  }
  template <typename Self, typename F>
  static constexpr auto
  or_else_impl(Self&& self, F&& f)
  {
    using result_type = expected_remove_cvref_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
    static_assert(is_expected_v<result_type> && std::is_same_v<typename result_type::value_type, T>);
    if (self.has_value())
      return result_type();
    return result_type(std::invoke(std::forward<F>(f), std::forward<Self>(self).error()));
  }
  template <typename Self, typename F>
  static constexpr auto
  transform_impl(Self&& self, F&& f)
  {
    using result_value = std::remove_cv_t<std::invoke_result_t<F>>;
    static_assert(valid_expected_value_v<result_value>);
    using result_type = expected<result_value, E>;
    if (!self.has_value())
      return result_type(unexpect, std::forward<Self>(self).error());
    if constexpr (std::is_void_v<result_value>)
      {
        std::invoke(std::forward<F>(f));
        return result_type();
      }
    else
      return result_type(expected_value_tag{}, expected_invoke_tag{}, std::forward<F>(f));
  }
  template <typename Self, typename F>
  static constexpr auto
  transform_error_impl(Self&& self, F&& f)
  {
    using result_error = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
    static_assert(valid_expected_error_v<result_error>);
    using result_type = expected<T, result_error>;
    if (self.has_value())
      return result_type();
    return result_type(expected_error_tag{}, expected_invoke_tag{}, std::forward<F>(f),
                       std::forward<Self>(self).error());
  }

  template <typename, typename>
  friend class threadschedule::expected;
  template <typename, typename>
  friend class expected_void;

  template <typename F, typename... Args>
  constexpr explicit expected_void(expected_error_tag, expected_invoke_tag, F&& f, Args&&... args)
      : storage_(expected_error_tag{}, expected_invoke_tag{}, std::forward<F>(f), std::forward<Args>(args)...)
  {
  }

  storage_type storage_;
};

} // namespace detail

template <typename E>
class expected<void, E> : public detail::expected_void<void, E>
{
  using base = detail::expected_void<void, E>;

public:
  using base::base;
  using base::operator=;
};

template <typename E>
class expected<const void, E> : public detail::expected_void<const void, E>
{
  using base = detail::expected_void<const void, E>;

public:
  using base::base;
  using base::operator=;
};

template <typename E>
class expected<volatile void, E> : public detail::expected_void<volatile void, E>
{
  using base = detail::expected_void<volatile void, E>;

public:
  using base::base;
  using base::operator=;
};

template <typename E>
class expected<const volatile void, E> : public detail::expected_void<const volatile void, E>
{
  using base = detail::expected_void<const volatile void, E>;

public:
  using base::base;
  using base::operator=;
};

template <typename T, typename E>
constexpr auto
swap(expected<T, E>& lhs, expected<T, E>& rhs) noexcept(noexcept(lhs.swap(rhs))) -> decltype(lhs.swap(rhs))
{
  lhs.swap(rhs);
}

} // namespace threadschedule

#undef THREADSCHEDULE_EXPECTED_THROW
