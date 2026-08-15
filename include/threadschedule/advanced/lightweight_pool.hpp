#pragma once

#include "../detail/pool/backend.hpp"
#include "../detail/pool/facade.hpp"

namespace threadschedule::advanced
{
class lightweight_pool final
    : private ::threadschedule::detail::lightweight_pool_facade<::threadschedule::detail::lightweight_pool_backend>
{
  using base = ::threadschedule::detail::lightweight_pool_facade<::threadschedule::detail::lightweight_pool_backend>;

public:
  explicit lightweight_pool(worker_count count = worker_count::automatic(),
                            worker_registration registration = worker_registration::disabled)
      : base(count, registration)
  {
  }

  using base::configure_workers;
  using base::distribute_workers;
  using base::is_current_worker;
  using base::post;
  using base::post_or_throw;
  using base::shutdown;
  using base::shutdown_for;
  using base::size;
};
} // namespace threadschedule::advanced
