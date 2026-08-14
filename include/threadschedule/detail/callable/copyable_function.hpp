#pragma once

#include <functional>
#include <utility>

namespace threadschedule::detail
{

template <typename Signature>
using copyable_function = std::function<Signature>;

template <typename Signature, typename Callable>
[[nodiscard]] auto
make_copyable_function(Callable&& callable) -> copyable_function<Signature>
{
  return copyable_function<Signature>(std::forward<Callable>(callable));
}

} // namespace threadschedule::detail
