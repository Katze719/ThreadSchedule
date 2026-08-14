#pragma once

#include "../detail/scheduled/backend.hpp"

namespace threadschedule::advanced
{
class raw_scheduled_pool final : public ::threadschedule::detail::scheduled_pool_backend
{
public:
  using ::threadschedule::detail::scheduled_pool_backend::scheduled_pool_backend;
};
} // namespace threadschedule::advanced
