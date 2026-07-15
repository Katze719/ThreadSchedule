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

## Advanced APIs

| 2.x | 3.0 |
| --- | --- |
| `HighPerformancePool` | `advanced::work_stealing_pool` |
| `FastThreadPool` | `advanced::polling_pool` |
| `LightweightPool` | `advanced::lightweight_pool` |
| `InlinePool` | `advanced::inline_pool` |
| `PThreadWrapper` | Removed; use `thread` (`std::thread` internally) |
| `ThreadPriority` / `SchedulingPolicy` | `advanced::native_thread_priority` / `advanced::native_scheduling_policy` |

`PoolWithErrors` and its aliases were removed. Set
`thread_pool_config::on_task_error` instead; task exceptions remain available
through the returned future.

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
