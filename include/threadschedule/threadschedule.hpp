#pragma once

#include "chaos.hpp"
#include "concepts.hpp"
#include "error_handler.hpp"
#include "generator.hpp"
#include "profiles.hpp"
#include "pthread_wrapper.hpp"
#include "scheduled_pool.hpp"
#include "scheduler_policy.hpp"
#include "task.hpp"
#include "thread_pool.hpp"
#include "thread_pool_with_errors.hpp"
#include "thread_registry.hpp"
#include "thread_wrapper.hpp"
#include "topology.hpp"

/**
 * @file threadschedule.hpp
 * @brief Modern C++17/20/23/26 Thread Scheduling Library
 *
 * A comprehensive header-only library for advanced thread management
 * on Linux systems, providing C++ wrappers for pthreads, std::thread,
 * and std::jthread with extended functionality.
 *
 * Features:
 * - Thread naming and identification
 * - Priority and scheduling policy management
 * - Nice value control
 * - CPU affinity management
 * - Modern C++20/23/26 features (ranges, concepts, format)
 * - Type-safe interfaces
 */

namespace threadschedule
{

/**
 * @brief Main namespace for all thread scheduling functionality
 */
namespace ts = threadschedule;

// Re-export main types for convenience
#ifndef _WIN32
using ts::PThreadWrapper;
#endif
using ts::affinity_for_node;
using ts::apply_profile;
using ts::ChaosConfig;
using ts::ChaosController;
using ts::CpuTopology;
using ts::distribute_affinities_by_numa;
using ts::ErrorCallback;
using ts::ErrorHandler;
using ts::FastThreadPool;
using ts::FastThreadPoolWithErrors;
using ts::FutureWithErrorHandler;
using ts::GlobalHighPerformancePool;
using ts::GlobalPool;
using ts::GlobalThreadPool;
using ts::HighPerformancePool;
using ts::HighPerformancePoolWithErrors;
using ts::JThreadWrapper;
using ts::JThreadWrapperView;
using ts::read_topology;
using ts::ScheduledFastThreadPool;
using ts::ScheduledHighPerformancePool;
using ts::ScheduledTaskHandle;
using ts::ScheduledThreadPool;
using ts::ScheduledThreadPoolT;
using ts::SchedulingPolicy;
using ts::TaskError;
using ts::ThreadAffinity;
using ts::ThreadByNameView;
using ts::ThreadPool;
using ts::ThreadPoolBase;
using ts::ThreadPoolWithErrors;
using ts::PoolWithErrors;
using ts::ThreadPriority;
using ts::ThreadProfile;
using ts::ThreadWrapper;
using ts::ThreadWrapperView;

// Build-mode introspection
using ts::BuildMode;
using ts::build_mode;
using ts::build_mode_string;

// Coroutine primitives (C++20)
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
using ts::task;
using ts::sync_wait;
using ts::generator;
#endif

} // namespace threadschedule
