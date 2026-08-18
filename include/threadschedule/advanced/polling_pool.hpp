#pragma once

#include "../detail/pool/backend.hpp"
#include "../detail/pool/facade.hpp"

namespace threadschedule::advanced
{
class polling_pool final
    : private ::threadschedule::detail::submitting_pool_facade<::threadschedule::detail::polling_pool_backend>
{
  using base = ::threadschedule::detail::submitting_pool_facade<::threadschedule::detail::polling_pool_backend>;

public:
  explicit polling_pool(worker_count count = worker_count::automatic(),
                        worker_registration registration = worker_registration::disabled)
      : base(count, registration)
  {
  }

  using base::configure_workers;
  using base::distribute_workers;
  using base::get_statistics;
  using base::is_current_worker;
  using base::parallel_for_each;
  using base::pending_tasks;
  using base::post;
  using base::post_or_throw;
  using base::shutdown;
  using base::shutdown_for;
  using base::size;
  using base::submit;
  using base::submit_batch;
  using base::submit_batch_or_throw;
  using base::submit_or_throw;
  using base::wait;
};
} // namespace threadschedule::advanced
