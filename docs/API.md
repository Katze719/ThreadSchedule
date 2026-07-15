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
threadschedule::thread_config config;
config.name = "worker";
config.scheduling = threadschedule::schedule::background();

threadschedule::thread worker(config, [] { do_work(); });
```

`thread` owns a `std::thread` and joins it on destruction. `join`, `detach`,
and `configure` return `result<void>`; `join_or_throw` is the explicit throwing
form. `thread_view` configures an existing `std::thread` without taking
ownership.

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
threadschedule::scheduled_pool scheduler(2);
auto once = scheduler.schedule_after(250ms, [] { refresh(); });
auto periodic = scheduler.schedule_periodic(1s, [] { sample(); });
periodic->cancel();
```

Scheduling after shutdown returns `std::errc::operation_canceled`.

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

## Scheduling

The portable factories are `background`, `normal`, `interactive`,
`low_latency`, `realtime_fifo`, and `realtime_rr`. Applying realtime policies
can fail with `permission_denied` or `operation_not_permitted`. Platform-native
policies and priority values are advanced APIs.
