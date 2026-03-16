// C++20 Module interface for ThreadSchedule
// Usage: import threadschedule;
//
// Requires C++20 or later and a compiler with module support.

module;

// Global module fragment — all headers are included here so their
// declarations are reachable (but not yet visible) to module importers.
#include "threadschedule/threadschedule.hpp"
#include "threadschedule/registered_threads.hpp"

export module threadschedule;

// ---------------------------------------------------------------------------
// Re-export the full public API of the threadschedule namespace.
//
// Entities declared in the global module fragment are *reachable* but not
// *visible* to importers.  An `export using` declaration makes each name
// visible under its original namespace when a consumer writes
//   import threadschedule;
// ---------------------------------------------------------------------------

export namespace threadschedule {

// -- expected.hpp -----------------------------------------------------------
using ::threadschedule::bad_expected_access;
using ::threadschedule::expected;
using ::threadschedule::unexpected;
using ::threadschedule::unexpect_t;
using ::threadschedule::unexpect;

// -- concepts.hpp -----------------------------------------------------------
using ::threadschedule::is_duration_impl;
using ::threadschedule::is_thread_like;
using ::threadschedule::is_thread_like_v;
using ::threadschedule::enable_if_thread_callable_t;
using ::threadschedule::enable_if_duration_t;

#if __cpp_concepts >= 201907L
using ::threadschedule::ThreadCallable;
using ::threadschedule::ThreadIdentifiable;
using ::threadschedule::Duration;
using ::threadschedule::PriorityType;
using ::threadschedule::CPUSetType;
#endif

// -- scheduler_policy.hpp ---------------------------------------------------
using ::threadschedule::SchedulingPolicy;
using ::threadschedule::ThreadPriority;
using ::threadschedule::ThreadAffinity;
using ::threadschedule::SchedulerParams;
using ::threadschedule::to_string;

// -- thread_wrapper.hpp -----------------------------------------------------
using ::threadschedule::ThreadWrapper;
using ::threadschedule::ThreadWrapperView;
using ::threadschedule::JThreadWrapper;
using ::threadschedule::JThreadWrapperView;
using ::threadschedule::ThreadByNameView;
using ::threadschedule::ThreadInfo;

// -- thread_registry.hpp ----------------------------------------------------
using ::threadschedule::Tid;
using ::threadschedule::RegisteredThreadInfo;
using ::threadschedule::ThreadControlBlock;
using ::threadschedule::ThreadRegistry;
using ::threadschedule::BuildMode;
using ::threadschedule::is_runtime_build;
using ::threadschedule::build_mode;
using ::threadschedule::build_mode_string;
using ::threadschedule::CompositeThreadRegistry;
using ::threadschedule::AutoRegisterCurrentThread;
using ::threadschedule::registry;
using ::threadschedule::set_external_registry;
#ifndef THREADSCHEDULE_RUNTIME
using ::threadschedule::registry_storage;
#endif

// -- error_handler.hpp ------------------------------------------------------
using ::threadschedule::TaskError;
using ::threadschedule::ErrorCallback;
using ::threadschedule::ErrorHandler;
using ::threadschedule::ErrorHandledTask;
using ::threadschedule::make_error_handled_task;
using ::threadschedule::FutureWithErrorHandler;

// -- thread_pool.hpp --------------------------------------------------------
using ::threadschedule::WorkStealingDeque;
using ::threadschedule::HighPerformancePool;
using ::threadschedule::FastThreadPool;
using ::threadschedule::ThreadPool;
using ::threadschedule::GlobalThreadPool;
using ::threadschedule::GlobalHighPerformancePool;
using ::threadschedule::parallel_for_each;

// -- thread_pool_with_errors.hpp --------------------------------------------
using ::threadschedule::HighPerformancePoolWithErrors;
using ::threadschedule::FastThreadPoolWithErrors;
using ::threadschedule::ThreadPoolWithErrors;

// -- scheduled_pool.hpp -----------------------------------------------------
using ::threadschedule::ScheduledTaskHandle;
using ::threadschedule::ScheduledThreadPoolT;
using ::threadschedule::ScheduledThreadPool;
using ::threadschedule::ScheduledHighPerformancePool;
using ::threadschedule::ScheduledFastThreadPool;

// -- profiles.hpp -----------------------------------------------------------
using ::threadschedule::ThreadProfile;
using ::threadschedule::apply_profile;

// -- topology.hpp -----------------------------------------------------------
using ::threadschedule::CpuTopology;
using ::threadschedule::read_topology;
using ::threadschedule::affinity_for_node;
using ::threadschedule::distribute_affinities_by_numa;

// -- chaos.hpp --------------------------------------------------------------
using ::threadschedule::ChaosConfig;
using ::threadschedule::ChaosController;

// -- task.hpp / generator.hpp (C++20 coroutines) ---------------------------
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
using ::threadschedule::task;
using ::threadschedule::sync_wait;
using ::threadschedule::generator;
#endif

// -- registered_threads.hpp -------------------------------------------------
using ::threadschedule::ThreadWrapperReg;
#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
using ::threadschedule::JThreadWrapperReg;
#endif
#ifndef _WIN32
using ::threadschedule::PThreadWrapperReg;
using ::threadschedule::PThreadWrapper;
using ::threadschedule::cgroup_attach_tid;
#endif

} // export namespace threadschedule

// Re-export profiles sub-namespace
export namespace threadschedule::profiles {
    using ::threadschedule::profiles::realtime;
    using ::threadschedule::profiles::low_latency;
    using ::threadschedule::profiles::throughput;
    using ::threadschedule::profiles::background;
}

// Convenience namespace alias
export namespace ts = ::threadschedule;
