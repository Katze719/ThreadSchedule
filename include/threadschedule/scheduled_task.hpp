#pragma once

/**
 * @file scheduled_task.hpp
 * @brief Copyable cancellation handle for scheduled work.
 *
 * Cancellation prevents future dispatch but does not interrupt a callable that has already started.
 */

#include "detail/scheduled/task_impl.hpp"
