# Migrating from ThreadSchedule 2.4.0 to 3.0.0

ThreadSchedule 3.0.0 replaces the 2.4.0 public API with a smaller lowercase core and a separate advanced API. It provides no
compatibility aliases for the 2.4.0 names, and its shared runtime is not binary compatible with version 2.4.0.

## Recommended migration order

1. Keep `<threadschedule/threadschedule.hpp>` temporarily, rename the core types, and make every new `result<T>` explicit.
2. Replace native priority/policy calls with `thread_config` and `schedule::*` wherever portable behavior is sufficient.
3. Move specialized pools and optional utilities to `threadschedule::advanced`.
4. Replace registry query chains with a checked `snapshot()` and ordinary STL algorithms.
5. Remove deleted module, stable-ABI, reflection, and coroutine build options or code.
6. Rebuild every executable and shared library that links `ThreadSchedule::Runtime`.
7. Once the code builds, replace the umbrellas with focused includes where useful.

## Includes and namespaces

The 2.4 umbrella included nearly every optional facility. In 3.0 it includes only the portable core:

```cpp
#include <threadschedule/threadschedule.hpp> // complete core
#include <threadschedule/advanced.hpp>       // optional and native facilities
```

Small consumers can include one contract directly, for example:

```cpp
#include <threadschedule/thread.hpp>
#include <threadschedule/thread_config.hpp>
#include <threadschedule/thread_pool.hpp>
```

The following 2.4 root headers moved or were replaced:

| 2.4 header | 3.0 header |
| --- | --- |
| `thread_wrapper.hpp` | `thread.hpp`, `jthread.hpp`, `thread_view.hpp` |
| `scheduler_policy.hpp` | `scheduling.hpp`, `thread_affinity.hpp`, `thread_config.hpp` |
| `profiles.hpp` | `advanced/thread_profile.hpp` |
| `topology.hpp` | `advanced/cpu_topology.hpp` |
| `chaos.hpp` | `advanced/testing/chaos_controller.hpp` |
| `futures.hpp` | `advanced/futures.hpp` |
| `task_group.hpp` | `advanced/task_group.hpp` |
| `error_handler.hpp` | `task_error.hpp` for core callbacks, or `advanced/error_handler.hpp` |
| `inline_pool.hpp` | `advanced/inline_pool.hpp` |
| `registered_threads.hpp` | No replacement header; register explicitly |
| `pthread_wrapper.hpp` | No replacement header; use `thread` or the platform API directly |
| `abi.hpp`, `task.hpp`, `generator.hpp`, `reflection.hpp`, `concepts.hpp`, `callable.hpp` | Removed |

All optional names now require the `threadschedule::advanced` qualifier. Do not include or name anything under
`threadschedule::detail`; those headers are implementation details and may change without compatibility guarantees.

## Core type mapping

The lowercase 3.0 types are new implementations, not aliases or subclasses of their 2.4 counterparts.

| 2.4 | 3.0 |
| --- | --- |
| `ThreadWrapper` | `thread` |
| `JThreadWrapper` | `jthread` when C++20 `std::jthread` is available |
| `ThreadWrapperView` | `thread_view` for configuration only |
| `ThreadByNameView` | `advanced::thread_by_name_view` for exact Linux process-thread lookup |
| `ThreadAffinity` | `thread_affinity` containing `cpu_id` values |
| `ThreadPriority` | `priority_level`, `nice_value`, or `realtime_priority` |
| `SchedulingPolicy` | `schedule::*` |
| `SchedulerParams` | No public replacement; use portable scheduling or the platform API |
| `Tid` | `thread_id` for registry operations; `advanced::native_thread_id` only for native code |
| `ThreadRegistry` | `thread_registry` |
| `RegisteredThreadInfo` | `registered_thread` |
| `AutoRegisterCurrentThread` | `auto_register_current_thread` |
| `ScheduledTaskHandle` | `scheduled_task` |
| `ShutdownPolicy` | `shutdown_policy` |
| `TaskError` used by a core pool | `task_error` |
| `BuildMode` / `build_mode()` | `build_mode` / `current_build_mode()` |
| `registry()` | `global_registry()` |
| `set_external_registry(...)` | scoped `global_registry_binding` |

`thread_view` also replaces `JThreadWrapperView` when C++20 is enabled. There is no 3.0 public replacement for
`PThreadWrapper`, `PThreadAttributes`, `PThreadMutex`, or the public `ThreadControlBlock`. The lowercase
`advanced::thread_by_name_view` replaces `ThreadByNameView` without a compatibility alias. It rejects ambiguous names and
checks the Linux thread start-time generation before control operations.

The build-mode enumerators are lowercase as well: `BuildMode::HEADER_ONLY` becomes `build_mode::header_only`, and
`BuildMode::RUNTIME` becomes `build_mode::runtime`. `is_runtime_build` and `build_mode_string()` keep their names.

`ThreadInfo` split into two clearer paths:

- configure the calling thread with `threadschedule::this_thread`;
- register another live thread and control it through `thread_registry::configure(thread_id, config)`.

## Results and exceptions

`result<T>` is `expected<T, std::error_code>`. In 3.0 it is the normal return type for recoverable configuration,
submission, registry, waiting, and shutdown failures.

```cpp
auto joined = worker.join();
if (!joined)
  report(joined.error());
```

The main behavior changes are:

| 2.4 operation | 3.0 operation |
| --- | --- |
| `ThreadWrapper::join()` / `detach()` | `thread::join()` / `detach()` return `result<void>` |
| Join or detach a non-joinable wrapper | 2.4 silently did nothing; 3.0 returns `invalid_argument` |
| `get_name()` / `get_affinity()` | Return `result<T>` instead of losing errors in `optional` |
| Pool `try_submit(...)` | Core `thread_pool::submit(...)` |
| Pool `submit(...)` | Core `thread_pool::submit_or_throw(...)`, or check `submit(...)` |
| Pool `try_post(...)` | Core `thread_pool::post(...)` |
| Pool `post(...)` | Core `thread_pool::post_or_throw(...)`, or check `post(...)` |
| Scheduled `schedule_*()` | Return `result<scheduled_task>` |

Direct constructors can throw `std::system_error`. Use `thread::create`, `jthread::create`, `thread_pool::create`, or
`scheduled_pool::create` when construction must return an error value. Explicit `*_or_throw` methods are provided where the
core operation otherwise returns a result.

Task exceptions are separate from submission errors. A successful `thread_pool::submit` contains a `std::future`; an
exception thrown by the task is rethrown by that future's `get()`. A `post()` has no future, so install a callback with
`set_error_callback(...)` if the exception must be observed.

## Threads and configuration

A direct 2.4 conversion looks like this:

```cpp
// 2.4
threadschedule::ThreadWrapper worker([] { run(); });
(void)worker.set_name("worker");
(void)worker.set_scheduling_policy(
    threadschedule::SchedulingPolicy::OTHER,
    threadschedule::ThreadPriority::normal());
worker.join();
```

```cpp
// 3.0
threadschedule::thread_config config;
config.set_name("worker").set_scheduling(threadschedule::schedule::normal());

threadschedule::thread worker(config, [] { run(); });
if (auto joined = worker.join(); !joined)
  report(joined.error());
```

Unlike `ThreadWrapper::create_with_config`, which started the callable and ignored configuration failures, a configured 3.0
thread uses a startup gate. If name, scheduling, or affinity setup fails, the callable is not invoked. The direct constructor
throws; the factory reports the same failure:

```cpp
auto worker = threadschedule::thread::create(config, [] { run(); });
if (!worker)
  report(worker.error());
else if (auto joined = worker->join(); !joined)
  report(joined.error());
```

`thread` still joins on destruction, so destruction and move assignment can block. `release()` transfers the owned
`std::thread`. Platform-native handle access is no longer a core member; use
`threadschedule::advanced::native_handle(worker)` only when a native API is genuinely required.

`thread_view` is deliberately control-only. It accepts `std::thread` and `thread`, plus `std::jthread` and `jthread` under
C++20. Keep using the original owning object for `join`, `detach`, cancellation, and direct access:

```cpp
std::thread native([] { run(); });
threadschedule::thread_view view(native);
(void)view.set_name("worker");
native.join();
```

On Linux, portable nice control needs a known identity. Core `thread_view` deliberately has no native-ID constructor;
nice operations on an external `std::thread` therefore return `operation_not_supported`. Native identity and handles belong
to the explicit advanced surface.

In C++20, replace `JThreadWrapper` with `jthread`. It follows `std::jthread` callable rules, including stop-token injection.
In C++17 no `jthread` name exists; the 2.4 fallback alias to `ThreadWrapper` was removed.

## Scheduling and affinity

Prefer portable scheduling intent:

```cpp
threadschedule::thread_config config;
config.set_scheduling(threadschedule::schedule::background())
    .set_affinity(threadschedule::thread_affinity(
        { threadschedule::cpu_id{2}, threadschedule::cpu_id{3} }));

config.set_scheduling(threadschedule::schedule::priority(
    threadschedule::priority_level::low));
config.set_scheduling(threadschedule::schedule::nice(
    threadschedule::nice_value{10}));
config.set_scheduling(threadschedule::schedule::realtime_fifo(
    threadschedule::realtime_priority{40}));
```

Portable nice values are validated as `-20..19`; portable FIFO/RR priorities are validated as `1..99`. Invalid direct
construction throws `invalid_argument`, while `nice_value::create(...)` and `realtime_priority::create(...)` return a failed
`result`. Negative nice values and realtime policies commonly require elevated Linux privileges.

The old static `ThreadWrapper::set_nice_value` changed process-level state. In 3.0, `thread::set_nice`,
`thread_view::set_nice`, and `this_thread::set_nice` target an individual thread. On Windows, portable nice and realtime
requests map to safe thread priorities and never select `THREAD_PRIORITY_TIME_CRITICAL`.

Affinity changes are now all-or-error: an unrepresentable mask is rejected, successful application requires exact readback,
and the implementation attempts rollback after a partial native change. On Windows, logical CPU IDs are flattened as
`processor_group * 64 + processor_index`; one mask cannot span processor groups.

There is no second public native scheduling model in 3.0. Use
`advanced::native_handle(...)` and the platform API when portable
`thread_config` and `schedule::*` cannot express the required operation. Raw
OS IDs remain available as `advanced::native_thread_id` for native APIs such as
Linux cgroup attachment.

## Pools

For ordinary work, migrate `ThreadPool` to the canonical facade:

```cpp
threadschedule::thread_pool_config config;
threadschedule::thread_config workers;
workers.set_name("worker");
config.set_worker_count(threadschedule::worker_count{4})
    .set_registration(threadschedule::worker_registration::global_registry)
    .set_worker_config(std::move(workers))
    .set_shutdown_policy(threadschedule::shutdown_policy::drain)
    .set_error_callback([](threadschedule::task_error const& error) {
      log(error.what());
    });

threadschedule::thread_pool pool(std::move(config));
auto submitted = pool.submit([] { return calculate(); });
if (!submitted)
  report(submitted.error());
else
  consume(submitted->get());
```

The canonical facade intentionally omits backend tuning, statistics, batch/range algorithms, and timed shutdown. If 2.4 code
depends on those lower-level operations, select the corresponding supported advanced pool:

| 2.4 pool | 3.0 replacement |
| --- | --- |
| `ThreadPool` | `thread_pool`; `advanced::raw_thread_pool` for backend-specific operations |
| `HighPerformancePool` | `advanced::work_stealing_pool` |
| `FastThreadPool` | `advanced::polling_pool` |
| `LightweightPool` | `advanced::lightweight_pool` |
| `InlinePool` | `advanced::inline_pool` |
| `GlobalThreadPool` | `advanced::global_thread_pool` |
| `GlobalHighPerformancePool` | `advanced::global_work_stealing_pool` |
| `ScheduledThreadPool` | `scheduled_pool`; `advanced::raw_scheduled_pool` for backend-specific operations |
| `ScheduledHighPerformancePool` | `advanced::scheduled_work_stealing_pool` |
| `ScheduledFastThreadPool` | `advanced::scheduled_polling_pool` |
| `ScheduledLightweightPool` | `advanced::scheduled_lightweight_pool` |

`ThreadPoolBase`, `PollingWait`, `LightweightPoolT`, `GlobalPool`, and `ScheduledThreadPoolT` are no longer public extension
points. Choose a named advanced pool instead of instantiating the generic implementation machinery.

Advanced pool constructors also require `worker_count`; raw `size_t` worker
counts are not accepted. Their non-throwing `submit`, `post`, batch, wait,
configuration, and shutdown paths return `result<T>`. Use the explicitly named
`*_or_throw` variants where exception-based flow is desired.

`PoolWithErrors`, `ThreadPoolWithErrors`, `FastThreadPoolWithErrors`, and `HighPerformancePoolWithErrors` were removed. Set
`thread_pool_config::set_error_callback` or `scheduled_pool_config::set_error_callback`. The independent lower-level `ErrorHandler`
family remains available with lowercase names in `threadschedule::advanced`.

Calling `wait()` or `shutdown()` from a task running on the same core pool now reports
`resource_deadlock_would_occur`. Destroying a pool from one of its own tasks remains unsupported; destroy it from an external
owner after the task returns.

## Scheduled work

`scheduled_pool_config` configures worker threads and the scheduler thread before either is used:

```cpp
threadschedule::scheduled_pool_config config;
threadschedule::thread_config workers;
workers.set_name("timer-worker");
threadschedule::thread_config scheduler;
scheduler.set_name("scheduler");
config.set_worker_count(threadschedule::worker_count{2})
    .set_worker_config(std::move(workers))
    .set_scheduler_config(std::move(scheduler));

threadschedule::scheduled_pool pool(std::move(config));
auto task = pool.schedule_periodic_after(
    std::chrono::seconds(5), std::chrono::seconds(1), [] { sample(); });
if (!task)
  report(task.error());
else
  task->cancel();
```

There is no core `configure_scheduler_thread` after construction; put its `thread_config` in
`scheduled_pool_config::set_scheduler_config`. Periodic intervals must be positive. Periodic executions do not overlap with themselves,
and missed fixed-rate deadlines are skipped. Cancellation is cooperative and does not interrupt a running invocation.

## Registry

The 2.4 chainable query facade and public control blocks were removed from the portable API. Take a checked value snapshot and
use normal C++ algorithms:

```cpp
auto& registry = threadschedule::global_registry();
threadschedule::auto_register_current_thread registration("main", "app");

auto snapshot = registry.snapshot();
if (!snapshot) {
  report(snapshot.error());
} else {
  for (auto const& entry : *snapshot)
    inspect(entry.name, entry.component, entry.id, entry.alive);
}
```

`register_current_thread` and `unregister_current_thread` now return `result<void>`. Entries registered through the public
API retain a guarded native control object while they are live, so they can be reconfigured using the snapshot's `thread_id`:

```cpp
threadschedule::thread_config config;
config.set_scheduling(threadschedule::schedule::priority(
    threadschedule::priority_level::low));

auto changed = registry.configure(entry.id, config);
if (!changed)
  report(changed.error());
```

Stale, exited, unregistered, or replaced registrations report `no_such_process`. `registered_thread` is only a portable value
snapshot; it does not expose a native handle or own a control block.

Replace manual external-registry installation and `nullptr` reset calls with a
scope:

```cpp
threadschedule::thread_registry application_registry;
threadschedule::global_registry_binding binding(application_registry);
// global_registry() now resolves to application_registry until binding dies.
```

The binding keeps the backend alive and restores the previously active
registry. Nested bindings must follow ordinary stack order.

Replace the automatically registering `ThreadWrapperReg`, `JThreadWrapperReg`, and `PThreadWrapperReg` by placing an
`auto_register_current_thread` guard in the callable, or select
`worker_registration::global_registry` with `set_registration(...)` in a pool config.

For independent registries, replace `CompositeThreadRegistry` with `advanced::composite_thread_registry`. Its `attach` takes
a `thread_registry&`, and it exposes `snapshot`, `count`, and `empty` rather than the old chainable query facade.

## Optional advanced utilities

| 2.4 | 3.0 |
| --- | --- |
| `ThreadProfile`, `apply_profile`, `profiles::*` | `advanced::thread_profile`, `advanced::apply_profile`, `advanced::profiles::*` |
| `CpuTopology`, topology/NUMA helpers | `advanced::cpu_topology` and lowercase helpers |
| `ChaosConfig`, `ChaosController` | `<threadschedule/advanced/testing/chaos_controller.hpp>` and the lowercase advanced types |
| `CompositeThreadRegistry` | `advanced::composite_thread_registry` |
| `when_all`, `when_any`, `when_all_settled` | Same names in `advanced` |
| `task_group` | `advanced::task_group` |
| `ErrorHandler`, `FutureWithErrorHandler` | `advanced::error_handler`, `advanced::future_with_error_handler` |
| `ErrorHandledTask`, `make_error_handled_task` | Lowercase equivalents in `advanced` |
| `cgroup_attach_tid` | `advanced::cgroup_attach_tid` on Linux |

These advanced types are supported public APIs, but they are not included by the core umbrella.

## Removed facilities

The following 2.4 facilities have no 3.0 compatibility layer:

- the experimental `threadschedule::abi` C ABI, opaque registry handles, status types, validation macro, and stable-ABI modes;
- the C++20 module source and `ThreadSchedule::Module` target;
- coroutine `task`, `generator`, `sync_wait`, `schedule_on`, `run_on`, and executor helpers;
- C++26 reflection and reflection-backed registry queries;
- public concepts/type traits and callable-storage helpers;
- standard-dependent ranges overloads;
- the dedicated pthread wrapper, registered-wrapper subclasses, and jthread view;
- generic public pool bases and wait policies.

Use standard-library or application-owned implementations where no v3 replacement is listed. In particular, applications
that need a compiler-neutral plugin ABI must define that ABI in their own protocol.

## CMake and packaging

The supported targets remain:

```cmake
target_link_libraries(app PRIVATE ThreadSchedule::ThreadSchedule)

# Optional shared process registry:
set(THREADSCHEDULE_RUNTIME ON)
target_link_libraries(app PRIVATE ThreadSchedule::Runtime)
```

`THREADSCHEDULE_RUNTIME` now defaults to `OFF`, making header-only mode the default even for a top-level build.
`THREADSCHEDULE_BUILD_DOCS` also defaults to `OFF`.

Remove these 2.4 options:

- `THREADSCHEDULE_STABLE_ABI`
- `THREADSCHEDULE_STABLE_ABI_STRICT`
- `THREADSCHEDULE_MODULE`
- `THREADSCHEDULE_ENABLE_REFLECTION`

The installed package now uses same-major version compatibility, so `find_package(ThreadSchedule 2 ...)` will not accept a
3.x installation. Adding the project as a subdirectory no longer rewrites parent MSVC runtime flags or injects `_WIN32_WINNT`.
A Conan 2 recipe is available; its normal `shared=True` option selects the optional runtime, while the default package stays
header-only.

## Runtime and ABI

The 3.0 runtime ABI is incompatible with 2.4. The public C ABI and the exported C++ `registry()` /
`set_external_registry()` entry points are gone. The runtime exports private registry-storage hooks used by
`global_registry()` and scoped `global_registry_binding`, plus runtime mode inspection through `current_build_mode()`.

After upgrading:

- rebuild the executable and every participating shared library with the same v3 headers;
- use a compatible compiler and standard-library toolchain across the runtime boundary;
- link every participant that must share one registry to `ThreadSchedule::Runtime`;
- do not mix the 2.4 runtime library with 3.0 headers or vice versa.

Header-only mode still has one registry instance per linked image. Use the optional runtime for one same-toolchain registry
across DSOs, or `advanced::composite_thread_registry` when explicitly merging independent registries is the better model.
