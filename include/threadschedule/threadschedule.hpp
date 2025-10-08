#pragma once

#include "concepts.hpp"
#include "pthread_wrapper.hpp"
#include "scheduler_policy.hpp"
#include "thread_pool.hpp"
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
using ts::FastThreadPool;
using ts::HighPerformancePool;
using ts::JThreadWrapper;
using ts::JThreadWrapperView;
using ts::SchedulingPolicy;
using ts::ThreadAffinity;
using ts::ThreadByNameView;
using ts::ThreadPool;
using ts::ThreadPriority;
using ts::ThreadWrapper;
using ts::ThreadWrapperView;

} // namespace threadschedule
