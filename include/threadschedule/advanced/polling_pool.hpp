#pragma once

#include "../detail/pool/backend.hpp"

namespace threadschedule::advanced
{
class polling_pool final : public ::threadschedule::detail::polling_pool_backend
{
public:
  using ::threadschedule::detail::polling_pool_backend::polling_pool_backend;
};
} // namespace threadschedule::advanced
