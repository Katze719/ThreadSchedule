# ThreadSchedule

[![Tests](https://github.com/Katze719/ThreadSchedule/actions/workflows/tests.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/tests.yml)
[![Integration](https://github.com/Katze719/ThreadSchedule/actions/workflows/integration.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/integration.yml)
[![Registry Integration](https://github.com/Katze719/ThreadSchedule/actions/workflows/registry-integration.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/registry-integration.yml)
[![Runtime Tests](https://github.com/Katze719/ThreadSchedule/actions/workflows/runtime-tests.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/runtime-tests.yml)
[![Documentation](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A modern C++ library for advanced thread management on Linux and Windows.
ThreadSchedule provides enhanced wrappers for `std::thread`, `std::jthread`, and
`pthread` with extended functionality including thread naming, priority
management, CPU affinity, and high-performance thread pools.

Available as **header-only**, as a **C++20 module** (`import threadschedule;`),
or with optional **shared runtime** for multi-DSO applications.

## Key Features

- **Modern C++**: Full C++17, C++20, C++23, and C++26 support with automatic
  feature detection and optimization
- **C++20 Modules**: Optional `import threadschedule;` support (C++20+)
- **Header-Only or Shared Runtime**: Choose based on your needs
- **Enhanced Wrappers**: Extend `std::thread`, `std::jthread`, and `pthread`
  with powerful features
- **Non-owning Views**: Zero-overhead views to configure existing threads or
  find by name (Linux)
- **Thread Naming**: Human-readable thread names for debugging
- **Priority & Scheduling**: Fine-grained control over thread priorities and
  scheduling policies
- **CPU Affinity**: Pin threads to specific CPU cores
- **Global Control Registry**: Process-wide registry to list and control running
  threads (affinity, priority, name)
- **Profiles**: High-level presets for priority/policy/affinity
- **NUMA-aware Topology Helpers**: Easy affinity builders across nodes
- **Chaos Testing**: RAII controller to perturb affinity/priority for validation
- **C++20 Coroutines**: `task<T>`, `generator<T>`, and `sync_wait` out of the
  box -- no boilerplate promise types needed
- **High-Performance Pools**: Work-stealing thread pool optimized for 10k+
  tasks/second
- **Scheduled Tasks**: Run tasks at specific times, after delays, or
  periodically
- **Error Handling**: Comprehensive exception handling with error callbacks and
  context
- **Performance Metrics**: Built-in statistics and monitoring
- **RAII & Exception Safety**: Automatic resource management
- **Multiple Integration Methods**: CMake, CPM, Conan, FetchContent

## Documentation

- **[Integration Guide](docs/INTEGRATION.md)** - CMake, Conan, FetchContent,
  system installation
- **[Thread Registry Guide](docs/REGISTRY.md)** - Process-wide thread control
  and multi-DSO patterns
- **[Scheduled Tasks Guide](docs/SCHEDULED_TASKS.md)** - Timer and periodic task
  scheduling
- **[Error Handling Guide](docs/ERROR_HANDLING.md)** - Exception handling with
  callbacks
- **[CMake Reference](docs/CMAKE_REFERENCE.md)** - Build options, targets, and
  troubleshooting
- **[Profiles](docs/PROFILES.md)** - High-level presets for
  priority/policy/affinity
- **[Topology & NUMA](docs/TOPOLOGY_NUMA.md)** - NUMA-aware affinity builders
- **[Chaos Testing](docs/CHAOS_TESTING.md)** - RAII controller to perturb
  affinity/priority for validation
- **[Coroutines](docs/COROUTINES.md)** - C++20 `task<T>`, `generator<T>`, and
  `sync_wait`
- **Feature Roadmap** - Current features and future plans (see below)

## Platform Support

ThreadSchedule is designed to work on any platform with a C++17 (or newer)
compiler and standard threading support. The library is **continuously tested**
on:

| Platform            | Compiler          | C++17 | C++20 | C++23 | C++26 |
| ------------------- | ----------------- | :---: | :---: | :---: | :---: |
| **Linux (x86_64)**  |                   |       |       |       |       |
| Ubuntu 22.04        | GCC 11            |  ✅   |  ✅   |  ✅   |   -   |
| Ubuntu 22.04        | GCC 12            |   -   |  ✅   |   -   |   -   |
| Ubuntu 22.04        | Clang 14          |  ✅   |  ✅   |  ✅   |   -   |
| Ubuntu 22.04        | Clang 15          |   -   |  ✅   |  ✅   |   -   |
| Ubuntu 24.04        | GCC 13            |  ✅   |  ✅   |  ✅   |   -   |
| Ubuntu 24.04        | GCC 14            |  ✅   |  ✅   |  ✅   |  ✅   |
| Ubuntu 24.04        | GCC 15            |   -   |  ✅   |  ✅   |  ✅   |
| Ubuntu 24.04        | Clang 16          |  ✅   |  ✅   |   -   |   -   |
| Ubuntu 24.04        | Clang 18          |  ✅   |  ✅   |   -   |   -   |
| Ubuntu 24.04        | Clang 19          |   -   |  ✅   |  ✅   |  ✅   |
| Ubuntu 24.04        | Clang 21          |   -   |  ✅   |  ✅   |  ✅   |
| **Linux (ARM64)**   |                   |       |       |       |       |
| Ubuntu 24.04 ARM64  | GCC 13 (system)   |  ✅   |  ✅   |  ✅   |   -   |
| Ubuntu 24.04 ARM64  | GCC 14            |   -   |  ✅   |  ✅   |  ✅   |
| **Windows**         |                   |       |       |       |       |
| Windows Server 2022 | MSVC 2022         |  ✅   |  ✅   |  ✅   |   -   |
| Windows Server 2022 | MinGW-w64 (GCC 15)|  ✅   |  ✅   |  ✅   |   -   |
| Windows Server 2025 | MSVC 2022         |  ✅   |  ✅   |  ✅   |   -   |
| Windows Server 2025 | MinGW-w64 (GCC 15)|  ✅   |  ✅   |  ✅   |   -   |

**Additional platforms:** ThreadSchedule should work on other platforms (macOS,
FreeBSD, other Linux distributions) with standard C++17+ compilers, but these
are not regularly tested in CI.

> **C++23**: GCC 12's libstdc++ lacks monadic `std::expected` operations
> (`and_then`, `transform`, …). Clang 16/18 on Ubuntu 24.04 use GCC 14's
> libstdc++ headers which expose `std::expected` incorrectly to those Clang
> versions. These combinations are therefore only tested up to C++20.
>
> **C++26**: Requires GCC 14+ or Clang 19+. MSVC does not yet expose
> `cxx_std_26` to CMake; C++26 on Windows is not tested.
>
> **GCC 15**: Installed via `ppa:ubuntu-toolchain-r/test` on Ubuntu 24.04.
>
> **Clang 21**: Installed via the official LLVM apt repository
> (`apt.llvm.org`) on Ubuntu 24.04.
>
> **Windows ARM64**: Not currently covered by GitHub-hosted runners, requires
> self-hosted runner for testing.
>
> **MinGW**: MinGW-w64 (MSYS2) ships GCC 15 and provides full Windows API
> support including thread naming (Windows 10+).

## Quick Start

### Installation

Add to your CMakeLists.txt using
[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake):

```cmake
include(${CMAKE_BINARY_DIR}/cmake/CPM.cmake)

CPMAddPackage(
    NAME ThreadSchedule
    GITHUB_REPOSITORY Katze719/ThreadSchedule
    GIT_TAG main  # or specific version tag
    OPTIONS "THREADSCHEDULE_BUILD_EXAMPLES OFF" "THREADSCHEDULE_BUILD_TESTS OFF"
)

add_executable(your_app src/main.cpp)
target_link_libraries(your_app PRIVATE ThreadSchedule::ThreadSchedule)
```

**Other integration methods:** See [docs/INTEGRATION.md](docs/INTEGRATION.md)
for FetchContent, Conan, system installation, and shared runtime option.

### C++20 Module Usage

ThreadSchedule can also be consumed as a C++20 module (requires CMake 3.28+ and
Ninja or Visual Studio 17.4+):

```cmake
# In your CMakeLists.txt
set(CMAKE_CXX_STANDARD 20)

CPMAddPackage(
    NAME ThreadSchedule
    GITHUB_REPOSITORY Katze719/ThreadSchedule
    GIT_TAG main
    OPTIONS "THREADSCHEDULE_MODULE ON"
)

add_executable(your_app src/main.cpp)
target_link_libraries(your_app PRIVATE ThreadSchedule::Module)
```

```cpp
// src/main.cpp
import threadschedule;

int main() {
    ts::HighPerformancePool pool(4);
    auto future = pool.submit([]() { return 42; });
    return future.get() != 42;
}
```

### Basic Usage

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    // Enhanced thread with configuration
    ThreadWrapper worker([]() {
        std::cout << "Worker running!" << std::endl;
    });
    worker.set_name("my_worker");
    worker.set_priority(ThreadPriority::normal());
    
    // High-performance thread pool
    HighPerformancePool pool(4);
    pool.configure_threads("worker");
    pool.distribute_across_cpus();
    
    auto future = pool.submit([]() { return 42; });
    std::cout << "Result: " << future.get() << std::endl;
    
    // Scheduled tasks (uses ThreadPool by default)
    ScheduledThreadPool scheduler(4);
    auto handle = scheduler.schedule_periodic(std::chrono::seconds(5), []() {
        std::cout << "Periodic task executed!" << std::endl;
    });
    
    // Or use high-performance pool for frequent tasks
    ScheduledHighPerformancePool scheduler_hp(4);
    auto handle_hp = scheduler_hp.schedule_periodic(std::chrono::milliseconds(100), []() {
        std::cout << "Frequent task!" << std::endl;
    });
    
    // Error handling
    HighPerformancePoolWithErrors pool_safe(4);
    pool_safe.add_error_callback([](const TaskError& error) {
        std::cerr << "Task error: " << error.what() << std::endl;
    });
    
    return 0;
}
```

### Non-owning Thread Views

Operate on existing threads without owning their lifetime.

```cpp
#include <threadschedule/threadschedule.hpp>
using namespace threadschedule;

std::thread t([]{ /* work */ });

// Configure existing std::thread
ThreadWrapperView v(t);
v.set_name("worker_0");
v.set_affinity(ThreadAffinity({0}));
v.join(); // joins the underlying t
```

#### Using thread views with APIs expecting std::thread/std::jthread references

- Views do not own threads. Use `.get()` to pass a reference to APIs that expect
  `std::thread&` or (C++20) `std::jthread&`.
- Ownership stays with the original `std::thread`/`std::jthread` object.

```cpp
void configure(std::thread& t);

std::thread t([]{ /* work */ });
ThreadWrapperView v(t);
configure(v.get()); // non-owning reference
```

You can also pass threads directly to APIs that take views; the view is created
implicitly (non-owning):

```cpp
void operate(threadschedule::ThreadWrapperView v);

std::thread t2([]{});
operate(t2); // implicit, non-owning
```

`std::jthread` (C++20):

```cpp
std::jthread jt([](std::stop_token st){ /* work */ });
JThreadWrapperView jv(jt);
jv.set_name("jworker");
jv.request_stop();
jv.join();
```

### Global Thread Registry

Opt-in registered threads with process-wide control, without imposing overhead
on normal wrappers.

```cpp
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>
using namespace threadschedule;

int main() {
    // Opt-in registration via *Reg wrapper
    ThreadWrapperReg t("worker-1", "io", [] {
        // ... work ...
    });

    // Chainable query API - direct filter and apply operations
    registry()
        .filter([](const RegisteredThreadInfo& e){ return e.componentTag == "io"; })
        .for_each([&](const RegisteredThreadInfo& e){
            (void)registry().set_name(e.tid, std::string("io-")+e.name);
            (void)registry().set_priority(e.tid, ThreadPriority{0});
        });
    
    // Count threads by tag
    auto io_count = registry()
        .filter([](const RegisteredThreadInfo& e){ return e.componentTag == "io"; })
        .count();
    
    // Check if any IO threads exist
    bool has_io = registry().any([](const RegisteredThreadInfo& e){ return e.componentTag == "io"; });
    
    // Find specific thread
    auto found = registry().find_if([](const RegisteredThreadInfo& e){ return e.name == "worker-1"; });
    
    // Map to extract TIDs
    auto tids = registry().filter(...).map([](auto& e) { return e.tid; });

    t.join();
}
```

**For multi-DSO applications:** Use the shared runtime option
(`THREADSCHEDULE_RUNTIME=ON`) to ensure a single process-wide registry. See
[docs/REGISTRY.md](docs/REGISTRY.md) for detailed patterns.

Notes:

- Normal wrappers (`ThreadWrapper`, `JThreadWrapper`, `PThreadWrapper`) remain
  zero-overhead.
- The registry **requires control blocks** for all operations. Threads must be
  registered with control blocks to be controllable via the registry.
- Use `*Reg` wrappers (e.g., `ThreadWrapperReg`) or `AutoRegisterCurrentThread`
  for automatic control block creation and registration.

Find by name (Linux):

```cpp
ThreadByNameView by_name("th_1");
if (by_name.found()) {
    by_name.set_name("new_name");
    ThreadAffinity one_core; one_core.add_cpu(0);
    by_name.set_affinity(one_core);
}
```

### Error handling with expected

ThreadSchedule uses `threadschedule::expected<T, std::error_code>` (and
`expected<void, std::error_code>`). When available, this aliases to
`std::expected`, otherwise, a compatible fallback based on
[P0323R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0323r3.pdf)
is used.

> Note: when building with `-fno-exceptions`, behavior is not
> standard-conforming because `value()`/`operator*` cannot throw
> `bad_expected_access` on error (exceptions are disabled). In that mode, always
> check `has_value()` or use `value_or()` before accessing the value.

Recommended usage:

```cpp
auto r = worker.set_name("my_worker");
if (!r) {
    // Inspect r.error() (std::error_code)
}

auto value = pool.submit([]{ return 42; }); // standard future-based API remains unchanged
```

### Coroutines (C++20)

Lazy coroutine primitives -- no boilerplate promise types required.

```cpp
#include <threadschedule/threadschedule.hpp>
using namespace threadschedule;

// Lazy single-value coroutine
task<int> compute(int x) {
    co_return x * 2;
}

task<int> pipeline() {
    int a = co_await compute(21);  // lazy -- starts here
    co_return a;                   // 42
}

int main() {
    // Blocking bridge for synchronous code
    int result = sync_wait(pipeline());

    // Lazy sequence coroutine
    auto fib = []() -> generator<int> {
        int a = 0, b = 1;
        while (true) {
            co_yield a;
            auto tmp = a; a = b; b = tmp + b;
        }
    };

    for (int v : fib()) {
        if (v > 1000) break;
        std::cout << v << "\n";
    }
}
```

**For more details:** See the [Coroutines Guide](docs/COROUTINES.md).

## API Overview

### Thread Wrappers

| Class            | Description                                                   | Available On   |
| ---------------- | ------------------------------------------------------------- | -------------- |
| `ThreadWrapper`  | Enhanced `std::thread` with naming, priority, affinity        | Linux, Windows |
| `JThreadWrapper` | Enhanced `std::jthread` with cooperative cancellation (C++20) | Linux, Windows |
| `PThreadWrapper` | Modern C++ interface for POSIX threads                        | Linux only     |

#### Passing wrappers into APIs expecting std::thread/std::jthread

- `std::thread` and `std::jthread` are move-only. When an API expects
  `std::thread&&` or `std::jthread&&`, pass the underlying thread via
  `release()` from the wrapper.
- Avoid relying on implicit conversions; `release()` clearly transfers ownership
  and prevents accidental selection of the functor constructor of `std::thread`.

```cpp
void accept_std_thread(std::thread&& t);

ThreadWrapper w([]{ /* work */ });
accept_std_thread(w.release()); // move ownership of the underlying std::thread
```

- Conversely, you can construct wrappers from rvalue threads:

```cpp
void take_wrapper(ThreadWrapper w);

std::thread make_thread();
take_wrapper(make_thread());       // implicit move into ThreadWrapper

std::thread t([]{});
take_wrapper(std::move(t));        // explicit move into ThreadWrapper
```

### Thread Views (non-owning)

Zero-overhead helpers to operate on existing threads without taking ownership.

| Class                | Description                                  | Available On   |
| -------------------- | -------------------------------------------- | -------------- |
| `ThreadWrapperView`  | View over an existing `std::thread`          | Linux, Windows |
| `JThreadWrapperView` | View over an existing `std::jthread` (C++20) | Linux, Windows |
| `ThreadByNameView`   | Locate and control a thread by its name      | Linux only     |

### Thread Pools

| Class                 | Use Case                                | Performance      |
| --------------------- | --------------------------------------- | ---------------- |
| `ThreadPool`          | General-purpose, simple API             | < 1k tasks/sec   |
| `HighPerformancePool` | Work-stealing, optimized for throughput | 10k+ tasks/sec   |
| `FastThreadPool`      | Single-queue, minimal overhead          | 1k-10k tasks/sec |

### Configuration

```cpp
// Scheduling policies
SchedulingPolicy::OTHER    // Standard time-sharing
SchedulingPolicy::FIFO     // Real-time FIFO
SchedulingPolicy::RR       // Real-time round-robin
SchedulingPolicy::BATCH    // Batch processing
SchedulingPolicy::IDLE     // Low priority background

// Priority management
ThreadPriority::lowest()   // Minimum priority
ThreadPriority::normal()   // Default priority
ThreadPriority::highest()  // Maximum priority
ThreadPriority(value)      // Custom priority

// CPU affinity
ThreadAffinity affinity({0, 1, 2});  // Pin to CPUs 0, 1, 2
worker.set_affinity(affinity);
```

**For more details:** See the [Integration Guide](docs/INTEGRATION.md),
[Registry Guide](docs/REGISTRY.md), and
[CMake Reference](docs/CMAKE_REFERENCE.md) linked at the top of this README.

### Benchmark Results

Performance varies by system configuration, workload characteristics, and task
complexity. See [benchmarks/](benchmarks/) for detailed performance analysis,
real-world scenario testing, and optimization recommendations.

## Platform-Specific Features

### Linux

- Full `pthread` API support
- Real-time scheduling policies (FIFO, RR, DEADLINE)
- CPU affinity and NUMA control
- Nice values for process priority

### Windows

- Thread naming (Windows 10 1607+)
- Thread priority classes
- CPU affinity masking
- Process priority control

**Note**: `PThreadWrapper` is Linux-only. Use `ThreadWrapper` or
`JThreadWrapper` for cross-platform code.

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes with clear messages
4. Push to your branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file
for details.

## Acknowledgments

- POSIX threads documentation
- Modern C++ threading best practices
- Linux kernel scheduling documentation
- C++20/23/26 concurrency improvements
