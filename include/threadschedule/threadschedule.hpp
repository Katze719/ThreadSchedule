#pragma once

#include "concepts.hpp"
#include "error_handler.hpp"
#include "pthread_wrapper.hpp"
#include "scheduled_pool.hpp"
#include "scheduler_policy.hpp"
#include "thread_pool.hpp"
#include "thread_pool_with_errors.hpp"
#include "thread_registry.hpp"
#include "thread_wrapper.hpp"

/**
 * @file threadschedule.hpp
 * @brief Modern C++23 Thread Scheduling Library
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
 * - Modern C++23 features (ranges, concepts, format)
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
using ts::ErrorCallback;
using ts::ErrorHandler;
using ts::FastThreadPool;
using ts::FastThreadPoolWithErrors;
using ts::FutureWithErrorHandler;
using ts::HighPerformancePool;
using ts::HighPerformancePoolWithErrors;
using ts::JThreadWrapper;
using ts::JThreadWrapperView;
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
using ts::ThreadPoolWithErrors;
using ts::ThreadPriority;
using ts::ThreadWrapper;
using ts::ThreadWrapperView;

} // namespace threadschedule
