# ThreadSchedule

[![Tests](https://github.com/Katze719/ThreadSchedule/actions/workflows/tests.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/tests.yml)
[![Runtime Tests](https://github.com/Katze719/ThreadSchedule/actions/workflows/runtime-tests.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/runtime-tests.yml)
[![Documentation](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml/badge.svg)](https://katze719.github.io/ThreadSchedule/)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

ThreadSchedule is a C++17 library for creating, configuring, scheduling, and
observing threads on Linux and Windows. It is header-only by default. C++20
consumers additionally get `threadschedule::jthread` when the standard library
provides `std::jthread`.

The v3 core deliberately stays small and uses lowercase, standard-style names.
Operations whose normal failure mode should not require exceptions return
`threadschedule::expected<T, std::error_code>`.

## Requirements

- CMake 3.14 or newer
- C++17 or newer
- Linux with GCC/libstdc++, or Windows with MinGW-w64/GCC or MSVC

The tested compiler versions are the compatibility contract. See
[Compatibility](docs/COMPATIBILITY.md) for the current matrix.

## Install

The recommended source integration uses CMake FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(
    ThreadSchedule
    GIT_REPOSITORY https://github.com/Katze719/ThreadSchedule.git
    GIT_TAG v3.0.0
)
FetchContent_MakeAvailable(ThreadSchedule)

target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

An existing checkout can be added directly:

```cmake
add_subdirectory(path/to/ThreadSchedule)
target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

To install and consume the CMake package:

```bash
cmake -S . -B build -DTHREADSCHEDULE_INSTALL=ON
cmake --build build
cmake --install build --prefix /your/prefix
```

```cmake
find_package(ThreadSchedule 3 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

Conan 2 consumers can build a local package directly from the release source:

```bash
conan profile detect
conan create . --build=missing
```

The recipe is tested in CI. Its standard `shared=True` option packages the
optional `ThreadSchedule::Runtime`; header-only mode remains the default.

## Start in five minutes

```cpp
#include <threadschedule/threadschedule.hpp>

#include <iostream>

int main()
{
    threadschedule::thread_pool pool(2);
    auto answer = pool.submit([] { return 42; });
    if (!answer) {
        std::cerr << answer.error().message() << '\n';
        return 1;
    }

    std::cout << answer->get() << '\n';
}
```

The complete
[getting-started project](examples/getting_started/CMakeLists.txt) includes its
own `CMakeLists.txt` and is tested against a freshly installed package.

## Choose the right type

| Need | Start with |
| --- | --- |
| Own one thread | `thread` |
| Own one cooperatively cancellable C++20 thread | `jthread` |
| Configure the calling thread | `this_thread` |
| Submit general-purpose work | `thread_pool` |
| Run delayed or periodic work | `scheduled_pool` |
| Discover and control registered threads | `thread_registry` |
| Select a specialized pool or native control | `advanced::*` |

Include `<threadschedule/threadschedule.hpp>` for the complete core. Include
`<threadschedule/advanced.hpp>` only when the workload requires native or
specialized choices.

## Results, exceptions, and lifetime

ThreadSchedule keeps failure channels explicit:

| Operation | Failure channel |
| --- | --- |
| Direct construction | May throw `std::system_error`, like standard types |
| `create(...)` | Returns `expected<T, std::error_code>` |
| Configuration and shutdown | Return `expected<void, std::error_code>` |
| `thread_pool::submit(...)` | Submission error in `expected`; task exception in the future |
| `thread_pool::post(...)` | Submission error in `expected`; task exception via `on_task_error` |
| Explicit `*_or_throw` operation | Throws `std::system_error` on failure |

Always inspect an `expected` before dereferencing it. A task submitted with
`post()` has no future; configure `on_task_error` if its exceptions must be
observed.

`threadschedule::thread` owns a `std::thread` but deliberately joins a joinable
thread on destruction. Destruction and move assignment can therefore block.
Call `join()`, `detach()`, or `release()` explicitly when that timing matters.

## Threads and configuration

Direct construction is the ordinary path:

```cpp
threadschedule::thread worker([] { do_work(); });
if (auto joined = worker.join(); !joined)
    report(joined.error());
```

Use `create(...)` when initial configuration failures should be returned as an
error value:

```cpp
threadschedule::thread_config config;
config.name = "metrics";
config.scheduling = threadschedule::schedule::background();

auto worker = threadschedule::thread::create(config, [] {
    collect_metrics();
});
if (!worker) {
    report(worker.error());
} else if (auto joined = worker->join(); !joined) {
    report(joined.error());
}
```

Affinity uses logical CPU indices and is intentionally absent from this first
configured example: containers and restricted CPU sets may not make CPU 0
available. Query the deployment environment before pinning a thread.

Code running inside any thread can configure itself without wrapping or
registering the thread first:

```cpp
auto allowed = threadschedule::this_thread::get_affinity();
if (!allowed) {
    report(allowed.error());
} else {
    threadschedule::thread_affinity pinned({ allowed->cpus().front() });
    if (auto result = threadschedule::this_thread::set_affinity(pinned);
        !result)
        report(result.error());
}

if (auto result = threadschedule::this_thread::set_priority(
        threadschedule::priority_level::low);
    !result)
    report(result.error());
```

`this_thread` also provides `configure`, `set_nice`, `get_priority`,
`set_name`, and `get_name`. Affinity readback reports the logical CPU indices
the process is actually allowed to use, which is safer than assuming CPU 0 is
available.

Under C++20, `jthread` mirrors standard callable forwarding and stop-token
injection:

```cpp
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
threadschedule::jthread worker([](std::stop_token stop) {
    while (!stop.stop_requested())
        do_work();
});
worker.request_stop();
#endif
```

See the compile-tested [jthread example](examples/jthread_example.cpp).

## Thread pools

```cpp
threadschedule::thread_pool_config config;
config.worker_count = 4;
config.workers.name = "worker";
config.on_task_error = [](threadschedule::task_error const& error) {
    log(error.what());
};

threadschedule::thread_pool pool(std::move(config));
auto answer = pool.submit([] { return calculate(); });
if (!answer)
    report(answer.error());
else
    use(answer->get());
```

Task exceptions from `submit()` remain attached to the returned future and are
rethrown by `get()`. Direct pool construction can throw when worker creation or
configuration fails; `thread_pool::create(...)` offers the error-value path.

## Scheduling

Portable intent factories cover ordinary use:

```cpp
auto background = threadschedule::schedule::background();
auto interactive = threadschedule::schedule::interactive();
auto low_latency = threadschedule::schedule::low_latency();
auto lower_priority = threadschedule::schedule::priority(
    threadschedule::priority_level::low);
auto exact_nice = threadschedule::schedule::nice(10);
auto realtime = threadschedule::schedule::realtime_fifo(80);
```

The five `priority_level` values are the simplest cross-platform choice.
Negative nice values and realtime policies normally require elevated
privileges on Linux. Native scheduling remains available through
`threadschedule::advanced`.

## Advanced usage

```cpp
#include <threadschedule/advanced.hpp>

threadschedule::advanced::work_stealing_pool pool(8);
auto future = pool.submit(expensive_work);
```

The advanced namespace is public and follows semantic versioning. See
[Advanced APIs](docs/ADVANCED.md) for native controls, profiles, topology,
future combinators, task groups, chaos testing, and lower-level error handling.

## Optional shared registry runtime

Header-only mode owns one registry per linked image. Applications that need
one registry shared by an executable and compatible DSOs can link the optional
C++ runtime:

```cmake
set(THREADSCHEDULE_RUNTIME ON)
add_subdirectory(ThreadSchedule)
target_link_libraries(my_app PRIVATE ThreadSchedule::Runtime)
```

This is a same-toolchain C++ ABI, not a portable plugin ABI. Do not mix GCC,
MinGW, and MSVC artifacts.

## Documentation

- [Online API reference](https://katze719.github.io/ThreadSchedule/)
- [API overview](docs/API.md)
- [Advanced APIs](docs/ADVANCED.md)
- [CMake reference](docs/CMAKE_REFERENCE.md)
- [Compatibility and ABI](docs/COMPATIBILITY.md)
- [Migrating from 2.x](docs/MIGRATION_V3.md)
- [Changelog](CHANGELOG.md)

## License

ThreadSchedule is available under the [MIT License](LICENSE).
