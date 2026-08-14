#pragma once

#include "../detail/scheduled/backend.hpp"

namespace threadschedule::advanced
{
class scheduled_lightweight_pool final : public ::threadschedule::detail::scheduled_lightweight_pool_backend
{
public:
  using ::threadschedule::detail::scheduled_lightweight_pool_backend::scheduled_lightweight_pool_backend;
};
} // namespace threadschedule::advanced
