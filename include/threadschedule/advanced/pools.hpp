#pragma once

/**
 * @file advanced/pools.hpp
 * @brief Specialized and low-level thread pool implementations.
 */

#include "../core.hpp"
#include "../detail/pool/inline.hpp"

namespace threadschedule::advanced
{

using raw_thread_pool = ::threadschedule::detail::thread_pool_backend;
using work_stealing_pool = ::threadschedule::detail::work_stealing_pool_backend;
using polling_pool = ::threadschedule::detail::polling_pool_backend;
using lightweight_pool = ::threadschedule::detail::lightweight_pool_backend;
using inline_pool = ::threadschedule::detail::inline_pool_backend;
using global_thread_pool = ::threadschedule::detail::global_thread_pool_backend;
using global_work_stealing_pool = ::threadschedule::detail::global_work_stealing_pool_backend;

using raw_scheduled_pool = ::threadschedule::detail::scheduled_pool_backend;
using scheduled_work_stealing_pool = ::threadschedule::detail::scheduled_work_stealing_pool_backend;
using scheduled_polling_pool = ::threadschedule::detail::scheduled_polling_pool_backend;
using scheduled_lightweight_pool = ::threadschedule::detail::scheduled_lightweight_pool_backend;

} // namespace threadschedule::advanced
