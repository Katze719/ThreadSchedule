# Migrating to ThreadSchedule 3.0

Version 3.0 is a hard API reset. It deliberately does not provide deprecated
aliases for 2.x names.

## Core renames

| 2.x | 3.0 |
| --- | --- |
| `ThreadWrapper` | `thread` |
| `ThreadWrapperView` / ordinary `ThreadInfo` use | `thread_view` |
| `ThreadRegistry` | `thread_registry` |
| `registry()` | `global_registry()` |
| `set_external_registry()` | `use_global_registry()` |
| `build_mode()` | `current_build_mode()` |
| `ThreadPool` | `thread_pool` |
| `ScheduledThreadPool` | `scheduled_pool` |
| `ThreadConfig` | `thread_config` |
| `ThreadSchedulingConfig` | `scheduling_config` |
| `ThreadAffinity` | `thread_affinity` |
| `ShutdownPolicy` | `shutdown_policy` |
| `ScheduledTaskHandle` | `scheduled_task` |
| `TaskError` | `task_error` |

These are real v3 types, not source-compatibility aliases. The implementation
backends are private details. Code that depended on inheritance or native
storage must move to the documented lowercase operations or an explicit
`advanced` API.

Threads, registries, and pools are directly constructible in the standard
library style. Their constructors can throw on resource or initial
configuration failures. The static `create(...)` factories remain available
when an `expected` result is preferable. `thread_pool::submit` and `post` are
non-throwing submission operations; use `submit_or_throw` or `post_or_throw`
only when that policy is intentional.

## Thread state and error handling

Operations whose normal failure mode is recoverable now return
`threadschedule::result<T>` (an `expected<T, std::error_code>` specialization).
Callers should inspect the returned error instead of relying on exceptions or
on an empty value that loses the platform failure.

In particular, `thread::join()` and `thread::detach()` report
`std::errc::invalid_argument` when the thread is not joinable. Use
`join_or_throw()` and `detach_or_throw()` only when standard-style exceptions
are desired. A control object for a thread that exited or was unregistered
reports `std::errc::no_such_process`.

Affinity getters now return `result<thread_affinity>`. An empty mask is a valid
value and is no longer used as a fallback for failed native reads. Native
permission, support, and validation errors are preserved, so code should not
assume that every configuration failure is `permission_denied`.

## Scheduling and affinity semantics

Portable realtime policies use the explicit native-style range 1 through 99:

```cpp
config.scheduling = threadschedule::schedule::realtime_fifo(40);
config.scheduling = threadschedule::schedule::realtime_rr(20);
```

Values outside that range return `std::errc::invalid_argument`. During
configured thread construction they are rejected before the callable starts.
For non-realtime work, use `schedule::priority(priority_level)` or
`schedule::nice(-20..19)`; the latter is an exact Linux nice value and a safe
discrete priority mapping on Windows.

Affinity application is now lossless. A mask that the native platform cannot
represent is rejected instead of being truncated, and a successful operation
requires exact readback. On a partial native change the library attempts to
restore the previous mask and returns an error. Pool worker distribution uses
the process's allowed CPU set rather than assuming CPUs start at zero.

On Windows, topology CPU identifiers are flattened as
`processor_group * 64 + processor_index`. One affinity value can represent one
processor group; requests that mix groups are rejected. If Windows exposes a
default thread across multiple groups, `get_affinity()` reports the primary
group returned by `GetThreadGroupAffinity` rather than claiming an all-group
mask.

## Advanced APIs

| 2.x | 3.0 |
| --- | --- |
| `HighPerformancePool` | `advanced::work_stealing_pool` |
| `FastThreadPool` | `advanced::polling_pool` |
| `LightweightPool` | `advanced::lightweight_pool` |
| `InlinePool` | `advanced::inline_pool` |
| `PThreadWrapper` | Removed; use `thread` (`std::thread` internally) |
| `ThreadPriority` / `SchedulingPolicy` | `advanced::native_thread_priority` / `advanced::native_scheduling_policy` |

The architectural cleanup also removes the accidentally public v3 backend
names without deprecated aliases:

| Removed root/backend name | Supported public replacement |
| --- | --- |
| `thread_pool_backend` | `advanced::raw_thread_pool` |
| `work_stealing_pool_backend` | `advanced::work_stealing_pool` |
| `polling_pool_backend` | `advanced::polling_pool` |
| `lightweight_pool_backend` | `advanced::lightweight_pool` |
| `inline_pool_backend` | `advanced::inline_pool` |
| `global_thread_pool_backend` | `advanced::global_thread_pool` |
| `global_work_stealing_pool_backend` | `advanced::global_work_stealing_pool` |
| `scheduled_pool_backend` | `advanced::raw_scheduled_pool` |
| `scheduled_work_stealing_pool_backend` | `advanced::scheduled_work_stealing_pool` |
| `scheduled_polling_pool_backend` | `advanced::scheduled_polling_pool` |
| `scheduled_lightweight_pool_backend` | `advanced::scheduled_lightweight_pool` |
| `thread_registry_backend` | `thread_registry` for portable use; no public backend replacement |

Optional root headers and root namespace copies were removed. Include
`<threadschedule/advanced.hpp>` or a focused header below
`<threadschedule/advanced/>` and qualify the API with
`threadschedule::advanced`. Backend-focused tests inside the library may use
`threadschedule::detail`; applications must not depend on it.

For ordinary non-realtime priority, prefer the new core API instead of the
native replacements:

```cpp
config.scheduling
    = threadschedule::schedule::priority(threadschedule::priority_level::low);
config.scheduling = threadschedule::schedule::nice(10);
```

The five-level form is portable. Nice values are exact per-thread values on
Linux and map to safe, discrete Win32 thread priorities on MSVC and MinGW.
Code that genuinely needs the platform handle can call
`threadschedule::advanced::native_handle(thread)`; native handles are not part
of the portable core member surface.

`PoolWithErrors` and its aliases were removed. Set
`thread_pool_config::on_task_error` instead; task exceptions remain available
through the returned future. `scheduled_pool_config::on_task_error` provides
the same reporting hook for scheduled fire-and-forget work.

## Pool and scheduled-work lifecycle

Calling a pool's shutdown or wait operation from one of that pool's workers
is rejected with `std::errc::resource_deadlock_would_occur`. Destroying a pool
from one of its own tasks remains unsupported; arrange for an owning thread
to destroy it after the task returns.

`shutdown_for(timeout)` stops accepting submissions before waiting. It returns
`true` only if every accepted task finishes by the deadline. On timeout,
pending tasks are discarded and their futures become ready, while running
tasks are joined before the operation returns. Consequently, the call itself
can return after the requested timeout when a running task cannot be stopped.
Concurrent shutdown callers are serialized safely.

After a move, the source pool has size zero. Submission, waiting, and worker
configuration report `operation_canceled`; shutdown remains an idempotent
success.

Periodic jobs use fixed-rate scheduling, never overlap with themselves, and
skip missed deadlines instead of accumulating an overdue backlog. Cancellation
is cooperative: it prevents future invocations but does not interrupt one that
is already running. Pool shutdown cancels work that has not yet become due;
work already dispatched to a worker follows the configured shutdown policy.

## Registry lifetime

`auto_register_current_thread` retains the registry selected at construction.
Nested registration of the same native thread is a no-op and the inner guard
does not unregister the outer registration. An explicitly supplied registry
backend must outlive its guard. Moving the public registry preserves existing
global guards and live control blocks; the moved-from registry remains valid
to inspect or assign but mutating operations report `operation_canceled`.

Registry control handles are valid only for the matching live registration.
Unregistration and thread exit invalidate them before a later thread can reuse
the same native identifier, and stale configuration attempts report
`no_such_process`.

Optional profiles, topology helpers, future combinators, task groups, chaos
testing, and lower-level error handling are supported through
`<threadschedule/advanced.hpp>` and `threadschedule::advanced`.

## Removed features

- the C ABI, opaque ABI handles, `ThreadSchedule::StableAbi`, and stable-ABI CMake options
- the C++20 module target
- the old `JThreadWrapper`; C++20 instead exposes the independent lowercase
  `jthread`, with no C++17 fallback alias
- coroutine `task` and `generator` helpers
- C++26 reflection and reflection-backed registry queries
- standard-dependent ranges overloads

Applications can compile the full C++17 core API unchanged in newer language
modes. `jthread` is the deliberate exception and exists only when C++20
`std::jthread` support is detected.

## Runtime migration

`THREADSCHEDULE_RUNTIME=ON` now builds only the optional shared C++ registry.
All participating DSOs must use one supported toolchain line and identical v3
headers. Projects that require a compiler-neutral plugin ABI must define that
boundary in their own application protocol.

The runtime ABI changes in this release. The shared library now exports only
private registry-storage hooks used by the portable `global_registry()` and
`use_global_registry()` facade. The backend-returning `registry()` and
`set_external_registry()` symbols are removed. Rebuild every participating DSO
against the same headers and runtime library.
