#pragma once

#include "../detail/pool/backend.hpp"

namespace threadschedule::advanced
{
class raw_thread_pool final : public ::threadschedule::detail::thread_pool_backend
{
public:
  using ::threadschedule::detail::thread_pool_backend::thread_pool_backend;
};
} // namespace threadschedule::advanced
