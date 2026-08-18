#pragma once

#include <type_traits>
#include <utility>

namespace threadschedule::detail
{

template <typename F>
class scope_exit
{
public:
  explicit scope_exit(F&& function) noexcept(std::is_nothrow_move_constructible_v<F>) : function_(std::move(function))
  {
  }

  scope_exit(scope_exit const&) = delete;
  auto operator=(scope_exit const&) -> scope_exit& = delete;
  scope_exit(scope_exit&&) = delete;
  auto operator=(scope_exit&&) -> scope_exit& = delete;

  ~scope_exit() noexcept
  {
    if (active_)
      function_();
  }

  void
  release() noexcept
  {
    active_ = false;
  }

private:
  F function_;
  bool active_{ true };
};

template <typename F>
[[nodiscard]] auto
make_scope_exit(F&& function) -> scope_exit<std::decay_t<F>>
{
  return scope_exit<std::decay_t<F>>(std::forward<F>(function));
}

} // namespace threadschedule::detail
