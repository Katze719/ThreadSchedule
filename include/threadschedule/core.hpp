#pragma once

/**
 * @file core.hpp
 * @brief Umbrella for ThreadSchedule's portable C++17 API and optional C++20 jthread.
 */

#include "cpu_id.hpp"
#include "jthread.hpp"
#include "nice_value.hpp"
#include "pool_statistics.hpp"
#include "realtime_priority.hpp"
#include "runtime.hpp"
#include "scheduled_pool.hpp"
#include "scheduled_task.hpp"
#include "scheduling.hpp"
#include "shutdown_policy.hpp"
#include "task_error.hpp"
#include "this_thread.hpp"
#include "thread.hpp"
#include "thread_affinity.hpp"
#include "thread_config.hpp"
#include "thread_id.hpp"
#include "thread_pool.hpp"
#include "thread_registry.hpp"
#include "thread_view.hpp"
#include "worker_count.hpp"
#include "worker_registration.hpp"
