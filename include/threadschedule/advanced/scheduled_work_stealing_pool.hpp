#pragma once

#include "../detail/scheduled/backend.hpp"

namespace threadschedule::advanced
{
class scheduled_work_stealing_pool final : public ::threadschedule::detail::scheduled_work_stealing_pool_backend
{
public:
  using ::threadschedule::detail::scheduled_work_stealing_pool_backend::scheduled_work_stealing_pool_backend;
};
} // namespace threadschedule::advanced
