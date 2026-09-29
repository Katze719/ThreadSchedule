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

### GCC 14 ThreadSanitizer limitation

GCC 14's ThreadSanitizer can incorrectly report
`unlock of an unlocked mutex (or by a wrong thread)` when a pool uses
`shutdown_for(...)`. libstdc++ acquires the timed mutex through
`pthread_mutex_clocklock`, which GCC 14's TSan does not fully intercept, but it
does observe the later unlock. This is a sanitizer false positive rather than an
unmatched unlock in ThreadSchedule. The sanitizer CI therefore uses GCC 16,
where the same tests pass cleanly.

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

Windows Vista compatibility mode is available when older platform targeting is
required. It reduces Windows feature usage to avoid Win7+ only paths. This mode
is currently not tested on real Vista hardware and may be unstable. Validation
is limited because no active Vista test machine is available.

```bash
cmake -S . -B build -DTHREADSCHEDULE_WINDOWS_VISTA_COMPAT=ON
```

```bash
conan create . -o '&:windows_vista_compat=True' --build=missing
```

The recipe is tested in CI. Its standard `shared=True` option packages the
optional `ThreadSchedule::Runtime`; header-only mode remains the default.

## Start in five minutes

```cpp
#include <threadschedule/threadschedule.hpp>

#include <iostream>

int main()
{
    threadschedule::thread_pool pool(threadschedule::worker_count{2});
    auto answer = pool.submit([] { return 42; });
    if (!answer) {
        std::cerr << answer.error().message() << '\n';
        return 1;
    }

    std::cout << answer->get() << '\n';
}
```

The complete [getting-started project](examples/getting_started/CMakeLists.txt)
includes its own `CMakeLists.txt` and is tested against a freshly installed
package.

## Choose the right type

| Need                                                           | Start with                      |
| -------------------------------------------------------------- | ------------------------------- |
| Own one thread                                                 | `thread`                        |
| Run short-lived work where thread startup cost matters         | `std::thread`                   |
| Configure or query an existing thread without taking ownership | `thread_view`                   |
| Own one cooperatively cancellable C++20 thread                 | `jthread`                       |
| Configure the calling thread                                   | `this_thread`                   |
| Submit general-purpose work                                    | `thread_pool`                   |
| Run delayed or periodic work                                   | `scheduled_pool`                |
| Discover and control registered threads                        | `thread_registry`               |
| Find unregistered Linux threads by OS name                     | `advanced::thread_by_name_view` |
| Select a specialized pool or native control                    | `advanced::*`                   |

Include `<threadschedule/threadschedule.hpp>` for the complete core. Include
`<threadschedule/advanced.hpp>` only when the workload requires native or
specialized choices.

For small consumers, each core contract is independently includable. For
example, a single managed thread needs only:

```cpp
#include <threadschedule/thread.hpp>
#include <threadschedule/thread_config.hpp>
```

Pools can use `<threadschedule/thread_pool.hpp>` or
`<threadschedule/scheduled_pool.hpp>` directly; registry-only code can use
`<threadschedule/thread_registry.hpp>`. The focused headers avoid making an
application opt into unrelated APIs, while `threadschedule.hpp` remains the
convenient complete core umbrella.

## Results, exceptions, and lifetime

ThreadSchedule keeps failure channels explicit:

| Operation                       | Failure channel                                                                  |
| ------------------------------- | -------------------------------------------------------------------------------- |
| Direct construction             | May throw `std::system_error`, like standard types                               |
| `create(...)`                   | Returns `expected<T, std::error_code>`                                           |
| Configuration and shutdown      | Return `expected<void, std::error_code>`                                         |
| `thread_pool::submit(...)`      | Submission error in `expected`; task exception in the future                     |
| `thread_pool::post(...)`        | Submission error in `expected`; task exception via the configured error callback |
| Explicit `*_or_throw` operation | Throws `std::system_error` on failure                                            |

Always inspect an `expected` before dereferencing it. A task submitted with
`post()` has no future; call `set_error_callback(...)` on the pool config if its
exceptions must be observed.

`threadschedule::thread` owns a `std::thread` but deliberately joins a joinable
thread on destruction. Destruction and move assignment can therefore block. Call
`join()`, `detach()`, or `release()` explicitly when that timing matters.

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
config.set_name("metrics").set_scheduling(threadschedule::schedule::background());

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

`this_thread` also provides `configure`, `set_nice`, `get_priority`, `set_name`,
and `get_name`. Affinity readback reports the logical CPU indices the process is
actually allowed to use, which is safer than assuming CPU 0 is available.

Under C++20, `jthread` mirrors standard callable forwarding and stop-token
injection:

```cpp
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
threadschedule::jthread worker([](std::stop_token stop) {
    while (!stop.stop_requested())
        do_work();
});
(void)worker.request_stop();
#endif
```

See the compile-tested [jthread example](examples/jthread_example.cpp).

### Short-lived threads and startup cost

Starting a `threadschedule::thread` adds bookkeeping and synchronization
compared with constructing `std::thread` directly, even without a
`thread_config`. For short-lived threads where startup cost matters, prefer
`std::thread`. When you need to configure or query it, use a non-owning
`threadschedule::thread_view`:

```cpp
#include <threadschedule/thread_view.hpp>

#include <future>
#include <iostream>
#include <thread>

namespace ts = threadschedule;

int
main()
{
  std::promise<void> start;
  auto ready = start.get_future();
  int answer = 0;

  std::thread worker(
      [&]
        {
          ready.wait(); // Keep the thread alive until configuration is complete.
          answer = 42;  // Short-lived work.
        });

  ts::thread_view view(worker);
  auto named = view.set_name("short-worker");

  // Release and join even if naming failed; the work can still run.
  start.set_value();
  worker.join();

  if (!named)
    {
      std::cerr << "Could not name the thread: " << named.error().message() << '\n';
      return 1;
    }
  std::cout << "answer: " << answer << '\n';
}
```

The view never owns, joins, or keeps the thread alive. Keep the referenced
`std::thread` object valid and prevent the worker from exiting while applying
configuration. The example's future provides that synchronization and ensures
naming is attempted before the short work starts; it also adds overhead of its
own. Without configuration, omit the view and the start gate. This pattern is
not a guarantee that configuration plus synchronization will be faster than
`threadschedule::thread` for every workload.

On Linux, a view over an external `std::thread` cannot recover its kernel TID,
so nice and portable priority controls report `operation_not_supported`. Use
`threadschedule::this_thread` inside the worker for those settings.

See the compile-tested
[short-lived thread example](examples/short_lived_thread_example.cpp).

If you control the worker's callable, configure the calling thread directly with
`ts::this_thread` before doing the work. This also works inside a plain
`std::thread`, including portable priority control on Linux, and needs no
external start gate:

```cpp
#include <threadschedule/this_thread.hpp>

#include <iostream>
#include <system_error>
#include <thread>

namespace ts = threadschedule;

int
main()
{
  std::error_code configuration_error;
  int answer = 0;

  std::thread worker(
      [&]
        {
          if (auto configured = ts::this_thread::set_priority(ts::priority_level::low); !configured)
            {
              configuration_error = configured.error();
              return;
            }
          answer = 42; // Short-lived work, after successful configuration.
        });

  worker.join();

  if (configuration_error)
    {
      std::cerr << "Could not configure the thread: " << configuration_error.message() << '\n';
      return 1;
    }
  std::cout << "answer: " << answer << '\n';
}
```

`join()` synchronizes access to the result and configuration error. If the OS
rejects the priority setting, the worker skips the work and the caller reports
the error after joining. See the compile-tested
[`this_thread` example](examples/this_thread_example.cpp).

### Measured thread costs

The following local measurements compare direct construction and `create()` with
`std::thread`, both without configuration and with the name `ts-bench`. The
factory measurement includes checking the result and moving the thread into an
empty owner. The configured standard thread names itself through `this_thread`
before starting its work.

<!-- thread-benchmark-results:start -->

Measured on 2026-09-29T19:05:19Z: **AMD Ryzen 5 5600X 6-Core Processor**, Linux
7.2.7-200.fc44.x86_64 x86_64. Compiler: GCC 16.2.1 20260819 (Red Hat 16.2.1-2);
standard library: libstdc++ 20260819; Language: C++17; build: Release. CMake
compiler flags: `-O3 -DNDEBUG`.

200 warmup rounds, 2,000 startup samples and 30 running-work samples per
variant; 4,194,304 iterations per work sample. Allocations: 200 warmup rounds
and 2,000 measured cycles per variant.

Each timing cell is **median / p95**; parentheses show the median ratio to the
`std::thread` baseline with the same configuration. Startup times are in **µs**.

**Without configuration**

| Variant            |             Creation µs |      First user code µs | Ready after configuration µs |        Create + join µs |
| ------------------ | ----------------------: | ----------------------: | ---------------------------: | ----------------------: |
| std::thread        | 13.114 / 19.576 (1.00×) | 16.210 / 24.436 (1.00×) |      16.270 / 24.526 (1.00×) | 23.023 / 35.897 (1.00×) |
| ts::thread         | 20.839 / 31.950 (1.59×) | 17.353 / 27.161 (1.07×) |      17.403 / 27.261 (1.07×) | 21.090 / 37.871 (0.92×) |
| ts::thread::create | 20.193 / 31.459 (1.54×) | 16.721 / 26.530 (1.03×) |      16.762 / 26.570 (1.03×) | 20.398 / 37.260 (0.89×) |

**With name configuration (`ts-bench`)**

| Variant                             |             Creation µs |      First user code µs | Ready after configuration µs |        Create + join µs |
| ----------------------------------- | ----------------------: | ----------------------: | ---------------------------: | ----------------------: |
| std::thread + this_thread::set_name | 13.184 / 20.228 (1.00×) | 16.291 / 24.646 (1.00×) |      17.738 / 26.720 (1.00×) | 24.516 / 37.851 (1.00×) |
| ts::thread(config)                  | 34.240 / 52.198 (2.60×) | 37.776 / 57.488 (2.32×) |      37.836 / 57.538 (2.13×) | 45.200 / 70.432 (1.84×) |
| ts::thread::create(config)          | 34.139 / 52.379 (2.59×) | 37.681 / 57.618 (2.31×) |      37.760 / 57.678 (2.13×) | 45.114 / 70.903 (1.84×) |

**Work inside an already running thread**

| Variant                             | Name configured | ns/iteration: median / p95 (ratio) |
| ----------------------------------- | --------------- | ---------------------------------: |
| std::thread                         | no              |              2.273 / 2.374 (1.00×) |
| ts::thread                          | no              |              2.262 / 2.312 (1.00×) |
| ts::thread::create                  | no              |              2.263 / 2.323 (1.00×) |
| std::thread + this_thread::set_name | yes             |              2.274 / 2.342 (1.00×) |
| ts::thread(config)                  | yes             |              2.270 / 2.331 (1.00×) |
| ts::thread::create(config)          | yes             |              2.261 / 2.340 (0.99×) |

**Object storage** (bytes; excludes dynamic allocations and OS thread stacks)

| Type                     | `sizeof` | `alignof` |
| ------------------------ | -------: | --------: |
| `std::thread`            |        8 |         8 |
| `ts::thread`             |       24 |         8 |
| `ts::thread_view`        |       24 |         8 |
| `ts::result<ts::thread>` |       32 |         8 |

**C++ heap allocations per complete create/join cycle**

| Variant                             | Name configured | Allocation count: median / p95 (ratio) | Requested bytes: median / p95 (ratio) |
| ----------------------------------- | --------------- | -------------------------------------: | ------------------------------------: |
| std::thread                         | no              |                  1.000 / 1.000 (1.00×) |               16.000 / 16.000 (1.00×) |
| ts::thread                          | no              |                  2.000 / 2.000 (2.00×) |             152.000 / 152.000 (9.50×) |
| ts::thread::create                  | no              |                  2.000 / 2.000 (2.00×) |             152.000 / 152.000 (9.50×) |
| std::thread + this_thread::set_name | yes             |                  1.000 / 1.000 (1.00×) |               16.000 / 16.000 (1.00×) |
| ts::thread(config)                  | yes             |                  3.000 / 3.000 (3.00×) |            288.000 / 288.000 (18.00×) |
| ts::thread::create(config)          | yes             |                  3.000 / 3.000 (3.00×) |            288.000 / 288.000 (18.00×) |

Callable entry and constructor return can occur in either order; the startup
columns are independent elapsed times from the same start point. For the named
`std::thread`, first user code precedes `ts::this_thread::set_name()`; readiness
follows successful configuration. Factory creation includes its success check
and the move into an empty thread object.

Running work excludes startup, configuration and join. Small differences may
reflect measurement noise; these numbers describe this system and this
integer-arithmetic workload, not a general speed guarantee. Allocation
instrumentation counts C++ `new` requests in a separate executable; it excludes
OS thread stacks and allocations made directly by the OS or C library. Requested
bytes are cumulative, not peak or retained memory. No Windows results are
inferred from this run.

<!-- thread-benchmark-results:end -->

The implementation does not perform repeated wrapper operations inside user
code. Its additional startup state remains allocated while the callable runs;
the allocation table measures cumulative requested bytes rather than retained
memory.

See the [benchmark instructions](benchmarks/README.md) to reproduce the
measurements and the [recorded results](docs/benchmarks/thread_costs.json).

```bash
cmake -S . -B build-thread-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 \
  -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON
cmake --build build-thread-bench --target run_thread_benchmarks
```

## Thread pools

```cpp
threadschedule::thread_pool_config config;
threadschedule::thread_config workers;
workers.set_name("worker");
config.set_worker_count(threadschedule::worker_count{4})
    .set_worker_config(std::move(workers))
    .set_error_callback([](threadschedule::task_error const& error) {
        log(error.what());
    });

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
auto exact_nice = threadschedule::schedule::nice(
    threadschedule::nice_value{10});
auto realtime = threadschedule::schedule::realtime_fifo(
    threadschedule::realtime_priority{80});
```

The five `priority_level` values are the simplest cross-platform choice.
Negative nice values and realtime policies normally require elevated privileges
on Linux. Native scheduling remains available through
`threadschedule::advanced`.

## Advanced usage

```cpp
#include <threadschedule/advanced.hpp>

threadschedule::advanced::work_stealing_pool pool(
    threadschedule::worker_count{8});
auto future = pool.submit(expensive_work);
if (!future)
    report(future.error());
else
    use(future->get());
```

On Linux, an unregistered process thread can also be found by its exact
kernel-visible name. A singular lookup rejects duplicate names; use `find_all()`
when duplicates are intentional:

```cpp
auto worker = threadschedule::advanced::thread_by_name_view::create("io-worker");
if (!worker)
    report(worker.error());
else if (auto lowered = worker->set_priority(
             threadschedule::priority_level::low);
         !lowered)
    report(lowered.error());
```

The view remembers the Linux TID and its start-time generation, so exited or
recycled targets report `no_such_process`. This native lookup cannot fully close
the race between the last identity check and a TID-based syscall; use
`thread_registry` when target lifetime must be coupled to control operations.

The advanced namespace is public and follows semantic versioning. See
[Advanced APIs](docs/ADVANCED.md) for native controls, profiles, topology,
future combinators, task groups, chaos testing, and lower-level error handling.

## Optional shared registry runtime

Header-only mode owns one registry per linked image. Applications that need one
registry shared by an executable and compatible DSOs can link the optional C++
runtime:

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
