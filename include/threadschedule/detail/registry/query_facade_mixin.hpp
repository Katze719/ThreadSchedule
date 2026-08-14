#pragma once

/** @file detail/registry/query_facade_mixin.hpp
 *  @brief Snapshot query facade shared by registry implementations.
 */

/**
 * @brief CRTP mixin that provides functional-style query facade methods.
 *
 * The derived class must implement a public @c query() method returning a
 * query_view-like object. All facade methods (filter, map, for_each,
 * find_if, any, all, none, take, skip, count, empty, apply) delegate to it.
 *
 * Return types are deduced via @c auto so the mixin can be used as a base
 * class before the concrete query_view type is fully defined (CRTP).
 *
 * @tparam Derived CRTP derived type.
 */
template <typename Derived>
class query_facade_mixin
{
  auto
  self() const -> Derived const&
  {
    return static_cast<Derived const&>(*this);
  }

public:
  template <typename Predicate>
  [[nodiscard]] auto
  filter(Predicate&& pred) const
  {
    return self().query().filter(std::forward<Predicate>(pred));
  }

  [[nodiscard]] auto
  count() const -> size_t
  {
    return self().query().count();
  }

  [[nodiscard]] auto
  empty() const -> bool
  {
    return self().query().empty();
  }

  template <typename Fn>
  void
  for_each(Fn&& fn) const
  {
    self().query().for_each(std::forward<Fn>(fn));
  }

  template <typename Predicate, typename Fn>
  void
  apply(Predicate&& pred, Fn&& fn) const
  {
    self().query().filter(std::forward<Predicate>(pred)).for_each(std::forward<Fn>(fn));
  }

  template <typename Fn>
  [[nodiscard]] auto
  map(Fn&& fn) const -> std::vector<std::invoke_result_t<Fn, registered_thread_info_backend const&>>
  {
    return self().query().map(std::forward<Fn>(fn));
  }

  template <typename Predicate>
  [[nodiscard]] auto
  find_if(Predicate&& pred) const -> std::optional<registered_thread_info_backend>
  {
    return self().query().find_if(std::forward<Predicate>(pred));
  }

  template <typename Predicate>
  [[nodiscard]] auto
  any(Predicate&& pred) const -> bool
  {
    return self().query().any(std::forward<Predicate>(pred));
  }

  template <typename Predicate>
  [[nodiscard]] auto
  all(Predicate&& pred) const -> bool
  {
    return self().query().all(std::forward<Predicate>(pred));
  }

  template <typename Predicate>
  [[nodiscard]] auto
  none(Predicate&& pred) const -> bool
  {
    return self().query().none(std::forward<Predicate>(pred));
  }

  [[nodiscard]] auto
  take(size_t n) const
  {
    return self().query().take(n);
  }

  [[nodiscard]] auto
  skip(size_t n) const
  {
    return self().query().skip(n);
  }
};
