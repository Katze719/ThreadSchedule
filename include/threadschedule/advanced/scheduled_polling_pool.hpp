#pragma once

#include "../detail/scheduled/backend.hpp"

namespace threadschedule::advanced
{
class scheduled_polling_pool final : public ::threadschedule::detail::scheduled_polling_pool_backend
{
public:
  using ::threadschedule::detail::scheduled_polling_pool_backend::scheduled_polling_pool_backend;
};
} // namespace threadschedule::advanced
