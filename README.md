# ThreadSchedule

[![Tests](https://github.com/Katze719/ThreadSchedule/actions/workflows/tests.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/tests.yml)
[![Runtime Tests](https://github.com/Katze719/ThreadSchedule/actions/workflows/runtime-tests.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/runtime-tests.yml)
[![Documentation](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

ThreadSchedule is a C++17 library for creating, configuring, scheduling, and
observing threads on Linux and Windows. It is header-only by default. Its core
API stays the same from C++17 onward; C++20 additionally exposes `jthread` when
the standard library provides `std::jthread`.

Version 3.0 intentionally has a small default surface:

- `thread` and `thread_view` for owning and observing threads
- `jthread` under C++20 for cooperative cancellation with stop tokens
- `thread_pool` for general-purpose work submission
- `scheduled_pool` for delayed and periodic work
- `thread_registry` for process-wide discovery and control
- intent-based scheduling through `schedule::*`
- specialized pools and native controls under `threadschedule::advanced`

Operations that can fail return `threadschedule::expected<T,
std::error_code>`. Task exceptions remain attached to their `std::future`.
The lowercase core consists of independent v3 types; it is not an alias layer
over the former PascalCase API.

## Requirements

- CMake 3.14 or newer
- C++17 or newer
- Linux with GCC/libstdc++, or Windows with MinGW-w64/GCC or MSVC

The tested compiler versions are the compatibility contract. See
[Compatibility](docs/COMPATIBILITY.md) for the current matrix and the rules for
using the optional shared registry runtime.

## Installation

### FetchContent

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

### Installed package

```bash
cmake -S . -B build -DTHREADSCHEDULE_INSTALL=ON
cmake --build build
cmake --install build --prefix /your/prefix
```

```cmake
find_package(ThreadSchedule 3 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ThreadSchedule::ThreadSchedule)
```

## Quick start

```cpp
#include <threadschedule/threadschedule.hpp>

#include <iostream>

int main()
{
    threadschedule::thread_config config;
    config.name = "metrics";
    config.scheduling = threadschedule::schedule::background();
    config.affinity = threadschedule::thread_affinity({ 0 });

    if (auto worker = threadschedule::thread::create(config, [] {
            // Collect metrics on the configured thread.
        });
        !worker) {
        std::cerr << worker.error().message() << '\n';
        return 1;
    } else if (auto result = worker->join(); !result) {
        std::cerr << result.error().message() << '\n';
        return 1;
    }
}
```

CPU indices identify logical processors. Naming, affinity, and scheduling can
fail when the requested CPU is unavailable or the operating system denies the
operation; use `create(...)` when those failures should be reported as an
error value.

## Thread pools

```cpp
threadschedule::thread_pool_config config;
config.worker_count = 4;
config.workers.name = "worker";
config.workers.scheduling = threadschedule::schedule::normal();

threadschedule::thread_pool pool(std::move(config));

auto answer = pool.submit([] { return 42; });
if (!answer)
    report(answer.error());
else
    std::cout << answer->get() << '\n';
```

Submission itself is non-throwing. If the task throws, the exception is
reported when its future is read:

```cpp
threadschedule::thread_pool_config config;
config.on_task_error = [](threadschedule::task_error const& error) {
    log(error.what());
};

threadschedule::thread_pool pool(std::move(config));
auto task = pool.submit([]() -> int { throw std::runtime_error("failed"); });
task->get(); // rethrows the task exception
```

Direct constructors follow the standard-library style and may throw if thread
creation or initial configuration fails. Code that prefers an error value can
still opt into the corresponding `create(...)` factory:

```cpp
auto pool = threadschedule::thread_pool::create(std::move(config));
if (!pool)
    report(pool.error());
```

Under C++20, `jthread` forwards callables like `std::jthread`, including
automatic `std::stop_token` injection:

```cpp
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
threadschedule::jthread worker([](std::stop_token stop) {
    while (!stop.stop_requested())
        do_work();
});

worker.request_stop();
#endif
```

## Scheduling

Portable intent factories cover normal use:

```cpp
auto background = threadschedule::schedule::background();
auto interactive = threadschedule::schedule::interactive();
auto low_latency = threadschedule::schedule::low_latency();
auto realtime = threadschedule::schedule::realtime_fifo(80);
```

Realtime policies normally require elevated privileges. Native priority and
policy controls remain available through `threadschedule::advanced`.

## Advanced usage

Performance specialists can select a backend explicitly without expanding the
default learning surface:

```cpp
threadschedule::advanced::work_stealing_pool pool(8);
auto future = pool.submit(expensive_work);
```

The advanced namespace is public and follows semantic versioning. See
[Advanced APIs](docs/ADVANCED.md).

## Optional shared registry runtime

Header-only mode owns one registry per linked image. Applications that need one
registry shared by an executable and several compatible DSOs can build and link
the optional C++ runtime:

```cmake
set(THREADSCHEDULE_RUNTIME ON)
add_subdirectory(ThreadSchedule)
target_link_libraries(my_app PRIVATE ThreadSchedule::Runtime)
```

This is a C++ ABI within one documented toolchain line. It is not a portable
plugin ABI and must not be mixed across GCC/libstdc++, MinGW, and MSVC builds.

## Documentation

- [API overview](docs/API.md)
- [Advanced APIs](docs/ADVANCED.md)
- [CMake reference](docs/CMAKE_REFERENCE.md)
- [Compatibility and ABI](docs/COMPATIBILITY.md)
- [Migrating from 2.x](docs/MIGRATION_V3.md)
- [Changelog](CHANGELOG.md)

## License

ThreadSchedule is available under the [MIT License](LICENSE).
