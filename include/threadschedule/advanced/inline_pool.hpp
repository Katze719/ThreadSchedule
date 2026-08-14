#pragma once

#include "../detail/pool/inline_pool_backend.hpp"

namespace threadschedule::advanced
{
class inline_pool final : public ::threadschedule::detail::inline_pool_backend
{
public:
  using ::threadschedule::detail::inline_pool_backend::inline_pool_backend;
};
} // namespace threadschedule::advanced
