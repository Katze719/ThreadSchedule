#pragma once

#include "../detail/pool/backend.hpp"

namespace threadschedule::advanced
{
class work_stealing_pool final : public ::threadschedule::detail::work_stealing_pool_backend
{
public:
  using ::threadschedule::detail::work_stealing_pool_backend::work_stealing_pool_backend;
};
} // namespace threadschedule::advanced
