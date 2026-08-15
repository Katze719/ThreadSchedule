#pragma once

#include "../detail/scheduled/backend.hpp"
#include "../detail/scheduled/facade.hpp"

namespace threadschedule::advanced
{
class scheduled_polling_pool final
    : private ::threadschedule::detail::scheduled_pool_facade<::threadschedule::detail::scheduled_polling_pool_backend>
{
  using base
      = ::threadschedule::detail::scheduled_pool_facade<::threadschedule::detail::scheduled_polling_pool_backend>;

public:
  explicit scheduled_polling_pool(worker_count count = worker_count::automatic(),
                                  worker_registration registration = worker_registration::disabled)
      : base(count, registration)
  {
  }

  using base::configure_scheduler;
  using base::configure_workers;
  using base::schedule_after;
  using base::schedule_at;
  using base::schedule_periodic;
  using base::schedule_periodic_after;
  using base::scheduled_count;
  using base::shutdown;
};
} // namespace threadschedule::advanced
