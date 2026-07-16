#pragma once

/**
 * @file advanced.hpp
 * @brief Optional ThreadSchedule facilities beyond the portable core API.
 */

#include "core.hpp"
#include "chaos.hpp"
#include "error_handler.hpp"
#include "futures.hpp"
#include "profiles.hpp"
#include "task_group.hpp"
#include "topology.hpp"

namespace threadschedule::advanced
{

using chaos_config = ::threadschedule::chaos_config;
using chaos_controller = ::threadschedule::chaos_controller;

using cpu_topology = ::threadschedule::cpu_topology;
using ::threadschedule::affinity_for_node;
using ::threadschedule::distribute_affinities_by_numa;
using ::threadschedule::read_topology;

using thread_profile = ::threadschedule::thread_profile;
namespace profiles = ::threadschedule::profiles;
using ::threadschedule::apply_profile;
using ::threadschedule::apply_profile_detailed;

using ::threadschedule::when_all;
using ::threadschedule::when_all_settled;
using ::threadschedule::when_any;

template <typename Pool>
using task_group = ::threadschedule::task_group<Pool>;

using task_error = ::threadschedule::task_error_backend;
using error_callback = ::threadschedule::error_callback_backend;
using error_handler = ::threadschedule::error_handler_backend;

template <typename Func>
using error_handled_task = ::threadschedule::error_handled_task<Func>;

template <typename T>
using future_with_error_handler
    = ::threadschedule::future_with_error_handler<T>;

using ::threadschedule::make_error_handled_task;

} // namespace threadschedule::advanced
