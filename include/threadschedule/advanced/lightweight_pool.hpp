#pragma once

#include "../detail/pool/backend.hpp"

namespace threadschedule::advanced
{
class lightweight_pool final : public ::threadschedule::detail::lightweight_pool_backend
{
public:
  using ::threadschedule::detail::lightweight_pool_backend::lightweight_pool_backend;
};
} // namespace threadschedule::advanced
