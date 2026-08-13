#pragma once

#include "../../shutdown_policy.hpp"
#include "backend.hpp"

namespace threadschedule::detail
{

[[nodiscard]] constexpr auto
to_native(shutdown_policy policy) noexcept -> shutdown_policy_backend
{
  return policy == shutdown_policy::drop_pending ? shutdown_policy_backend::drop_pending
                                                 : shutdown_policy_backend::drain;
}

} // namespace threadschedule::detail
