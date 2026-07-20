# API overview

All standard APIs are available from:

```cpp
#include <threadschedule/threadschedule.hpp>
```

The core public surface is C++17. When the standard library exposes
`std::jthread`, C++20 consumers additionally get `threadschedule::jthread`.
This is the only language-standard-dependent core type.

The lowercase classes and configuration objects are independent v3 types, not
aliases or public subclasses of the former PascalCase API. The standard-thread
adapter lives under `threadschedule::detail`; specialized implementation types
are supported only through the explicitly named `advanced` surface.

## Results and errors

`result<T>` is an alias for `expected<T, std::error_code>`. Configuration,
submission, and shutdown operations use this result type. Core objects are
directly constructible; construction can throw like the corresponding standard
library operation. The static `create(...)` factories remain available as an
optional non-throwing construction path. Explicitly named `*_or_throw` helpers
are available where a throwing operation is otherwise useful.

An accepted task returns a standard future. Exceptions thrown by the task are
stored in the future and rethrown by `get()`. `thread_pool_config::on_task_error`
can observe the same exception as a `task_error` without consuming it.

## Threads

```cpp
#include <future>

std::promise<void> release;
auto ready_to_finish = release.get_future().share();

threadschedule::thread worker([ready_to_finish] { ready_to_finish.wait(); });

if (auto result = worker.set_name("worker"); !result)
    {
        report(result.error());
    }
if (auto result = worker.set_affinity(threadschedule::thread_affinity({ 0 })); !result)
    {
        report(result.error());
    }

threadschedule::thread_config config;
config.scheduling = threadschedule::schedule::background();
if (auto result = worker.configure(config); !result)
    {
        report(result.error());
    }

release.set_value();
if (auto result = worker.join(); !result)
    {
        report(result.error());
    }
```

`thread` owns a `std::thread` and joins it on destruction. `join`, `detach`,
and `configure` return `result<void>`; `join_or_throw` and `detach_or_throw`
are the explicit throwing forms. Joining or detaching a non-joinable thread
returns `std::errc::invalid_argument`. `thread_view` configures an existing
`std::thread` without taking ownership.

### Thread configuration

For error-returning construction, pass `thread_config` to `create(...)` to
apply a name, portable scheduling priority, and CPU affinity before the thread
runs:

```cpp
threadschedule::thread_config config;
config.name = "metrics";
config.scheduling = threadschedule::schedule::background();
config.affinity = threadschedule::thread_affinity({ 0, 1 });

if (auto worker = threadschedule::thread::create(config, [] {
        // Collect metrics on the configured thread.
    });
    !worker)
    {
        report(worker.error());
    }
else if (auto result = worker->join(); !result)
    {
        report(result.error());
    }
```

`thread_affinity` contains logical CPU indices. The portable scheduling
factories include `background`, `normal`, `interactive`, and `low_latency`.
The operating system can reject a name, scheduling request, or CPU mask, for
example because a CPU is unavailable or the process lacks permission.
`create(...)` reports initial-configuration failures as an error value; the
direct constructor reports them like `std::thread` construction. If initial
configuration fails, the callable is not started. Configuration operations
preserve the specific error from the first failed name, scheduling, or
affinity step.

Under C++20, `jthread` mirrors `std::jthread` construction and cancellation:

```cpp
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
threadschedule::jthread worker([](std::stop_token stop) {
    while (!stop.stop_requested())
        do_work();
});
worker.request_stop();
#endif
```

It also accepts `thread_config` as its first constructor argument. There is no
fallback `jthread` type in C++17.

## Thread pools

```cpp
threadschedule::thread_pool_config config;
config.worker_count = 8;
config.register_workers = true;
config.shutdown = threadschedule::shutdown_policy::drain;

threadschedule::thread_pool pool(std::move(config));
auto future = pool.submit([] { return calculate(); });
auto posted = pool.post([] { publish_metrics(); });
pool.wait();
```

`submit` returns `result<std::future<T>>`; `post` returns `result<void>`.
Destruction uses the configured shutdown policy. `drain` completes accepted
work, while `drop_pending` discards work that has not started.

## Scheduled work

```cpp
threadschedule::scheduled_pool_config config;
config.worker_count = 2;
config.register_workers = true;
config.workers.name = "scheduled-worker";
config.scheduler.name = "scheduler";
config.shutdown = threadschedule::shutdown_policy::drain;
config.on_task_error = [](threadschedule::task_error const& error) {
    report(error.what());
};

threadschedule::scheduled_pool scheduler(std::move(config));
auto once = scheduler.schedule_after(250ms, [] { refresh(); });
auto periodic = scheduler.schedule_periodic(1s, [] { sample(); });
auto delayed = scheduler.schedule_periodic_after(
    5s, 1s, [] { sample_after_warmup(); });
periodic->cancel();
```

Periodic intervals must be positive. Periodic tasks use fixed-rate scheduling:
each next deadline is based on the preceding deadline, not task completion.
Cancellation is cooperative and does not interrupt a running task. Scheduling
after shutdown returns `std::errc::operation_canceled`.

`scheduled_pool_config` supports the same worker registration, worker
configuration, shutdown policy, and task-error callback as `thread_pool_config`,
plus an independent `scheduler` thread configuration. Shutdown stops accepting
and dispatching scheduled entries; the selected policy controls work already
queued in the worker pool.

## Registry

```cpp
threadschedule::thread_registry registry;
registry.register_current_thread("main", "application");

auto entries = registry.snapshot();
for (auto const& entry : *entries)
    inspect(entry.name, entry.component);
```

`registered_thread` is a lowercase value snapshot without native control-block
ownership. `global_registry()` returns the active process registry.
`use_global_registry(pointer)` injects an application-owned registry.
Header-only builds have one instance per linked image; the optional runtime
supplies one instance to compatible DSOs that link it.

Entries added by `register_current_thread` retain a native control block, so
their `native_id` can be passed to `thread_registry::configure` while the
registered thread remains alive.

## Scheduling

The portable factories are `background`, `normal`, `interactive`,
`low_latency`, `priority`, `nice`, `realtime_fifo`, and `realtime_rr`.

`schedule::priority(priority_level)` provides `lowest`, `low`, `normal`,
`high`, and `highest`. Their Linux nice values are respectively 19, 5, 0, -5,
and -20. `schedule::nice(value)` exposes the full -20 through 19 scale. Values
outside that range fail with `invalid_argument` when the configuration is
applied.

On Windows, normal priorities map to `IDLE`, `BELOW_NORMAL`, `NORMAL`,
`ABOVE_NORMAL`, or `HIGHEST`. Exact nice values use the same safe mapping and
never select `TIME_CRITICAL`; that level remains exclusive to explicit
realtime scheduling. MinGW-w64 uses the same Win32 behavior through its
pthread-to-`HANDLE` adapter.

`thread`, C++20 `jthread`, and `thread_view` provide `set_priority`,
`set_nice`, and `get_priority`. A Linux `thread_view` over an external
`std::thread` needs the constructor overload taking its native TID for nice
control; without it, nice operations report `operation_not_supported`.
Registry-managed threads expose matching operations by `native_id`, and pool
workers use the same settings through `thread_config`.

Increasing priority with a negative nice value usually requires privileges on
Linux. Applying realtime policies can likewise fail with `permission_denied`
or `operation_not_permitted`. Platform-native policies and priority values are
advanced APIs.
