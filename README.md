# ThreadSchedule

[![Tests](https://github.com/Katze719/ThreadSchedule/actions/workflows/tests.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/tests.yml)
[![Integration](https://github.com/Katze719/ThreadSchedule/actions/workflows/integration.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/integration.yml)
[![Registry Integration](https://github.com/Katze719/ThreadSchedule/actions/workflows/registry-integration.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/registry-integration.yml)
[![Runtime Tests](https://github.com/Katze719/ThreadSchedule/actions/workflows/runtime-tests.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/runtime-tests.yml)
[![Code Quality](https://github.com/Katze719/ThreadSchedule/actions/workflows/code-quality.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/code-quality.yml)
[![Documentation](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A modern C++ library for advanced thread management on Linux and Windows. ThreadSchedule provides enhanced wrappers for `std::thread`, `std::jthread`, and `pthread` with extended functionality including thread naming, priority management, CPU affinity, and high-performance thread pools.

Available as **header-only** or with optional **shared runtime** for multi-DSO applications.

## Key Features

- **Modern C++**: Full C++17, C++20, and C++23 support with automatic feature detection and optimization
- **Header-Only or Shared Runtime**: Choose based on your needs
- **Enhanced Wrappers**: Extend `std::thread`, `std::jthread`, and `pthread` with powerful features
- **Non-owning Views**: Zero-overhead views to configure existing threads or find by name (Linux)
- **Thread Naming**: Human-readable thread names for debugging
- **Priority & Scheduling**: Fine-grained control over thread priorities and scheduling policies
- **CPU Affinity**: Pin threads to specific CPU cores
- **Global Control Registry**: Process-wide registry to list and control running threads (affinity, priority, name)
- **Profiles**: High-level presets for priority/policy/affinity
- **NUMA-aware Topology Helpers**: Easy affinity builders across nodes
- **Chaos Testing**: RAII controller to perturb affinity/priority for validation
- **High-Performance Pools**: Work-stealing thread pool optimized for 10k+ tasks/second
- **Scheduled Tasks**: Run tasks at specific times, after delays, or periodically
- **Error Handling**: Comprehensive exception handling with error callbacks and context
- **Performance Metrics**: Built-in statistics and monitoring
- **RAII & Exception Safety**: Automatic resource management
- **Multiple Integration Methods**: CMake, CPM, Conan, FetchContent

## Documentation

- **[Integration Guide](docs/INTEGRATION.md)** - CMake, Conan, FetchContent, system installation
- **[Thread Registry Guide](docs/REGISTRY.md)** - Process-wide thread control and multi-DSO patterns
- **[Scheduled Tasks Guide](docs/SCHEDULED_TASKS.md)** - Timer and periodic task scheduling
- **[Error Handling Guide](docs/ERROR_HANDLING.md)** - Exception handling with callbacks
- **[CMake Reference](docs/CMAKE_REFERENCE.md)** - Build options, targets, and troubleshooting
- **[Profiles](docs/PROFILES.md)** - High-level presets for priority/policy/affinity
- **[Topology & NUMA](docs/TOPOLOGY_NUMA.md)** - NUMA-aware affinity builders
- **[Chaos Testing](docs/CHAOS_TESTING.md)** - RAII controller to perturb affinity/priority for validation
- **Feature Roadmap** - Current features and future plans (see below)

## Platform Support

ThreadSchedule is designed to work on any platform with a C++17 (or newer) compiler and standard threading support. The library is **continuously tested** on:

| Platform | Compiler | C++17 | C++20 | C++23 |
|----------|----------|:-----:|:-----:|:-----:|
| **Linux (x86_64)** | | | | |
| Ubuntu 22.04 | GCC 11 | ✅ | ✅ | ✅ |
| Ubuntu 22.04 | Clang 14 | ✅ | ✅ | ✅ |
| Ubuntu 24.04 | GCC 11 | ✅ | ✅ | ✅ |
| Ubuntu 24.04 | Clang 14 | ✅ | - | - |
| Ubuntu 24.04 | Clang 19 | - | ✅ | ✅ |
| **Linux (ARM64)** | | | | |
| Ubuntu 24.04 ARM64 | GCC (system) | ✅ | ✅ | ✅ |
| **Windows** | | | | |
| Windows Server 2022 | MSVC 2022 | ✅ | ✅ | ✅ |
| Windows Server 2022 | MinGW-w64 (GCC) | ✅ | ✅ | ✅ |
| Windows Server 2025 | MSVC 2022 | ✅ | ✅ | ✅ |
| Windows Server 2025 | MinGW-w64 (GCC) | ✅ | ✅ | ✅ |

**Additional platforms:** ThreadSchedule should work on other platforms (macOS, FreeBSD, other Linux distributions) with standard C++17+ compilers, but these are not regularly tested in CI.

> **Ubuntu 24.04 Clang**: Clang 14 are limited to C++17/C++20 on 24.04; for C++23, Clang 19 is used.
>
> **Windows ARM64**: Not currently covered by GitHub-hosted runners, requires self-hosted runner for testing.
>
> **MinGW**: MinGW-w64 provides full Windows API support including thread naming (Windows 10+).

> ⚠️ **Known Issue (Ubuntu 24.04)**: Older Clang versions with newer GCC libstdc++ may have compatibility issues. Use Clang 19 for best C++23 support on Ubuntu 24.04.

## Quick Start

### Installation

Add to your CMakeLists.txt using [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake):

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

**Other integration methods:** See [docs/INTEGRATION.md](docs/INTEGRATION.md) for FetchContent, Conan, system installation, and shared runtime option.

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

- Views do not own threads. Use `.get()` to pass a reference to APIs that expect `std::thread&` or (C++20) `std::jthread&`.
- Ownership stays with the original `std::thread`/`std::jthread` object.

```cpp
void configure(std::thread& t);

std::thread t([]{ /* work */ });
ThreadWrapperView v(t);
configure(v.get()); // non-owning reference
```

You can also pass threads directly to APIs that take views; the view is created implicitly (non-owning):

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

Opt-in registered threads with process-wide control, without imposing overhead on normal wrappers.

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

**For multi-DSO applications:** Use the shared runtime option (`THREADSCHEDULE_RUNTIME=ON`) to ensure a single process-wide registry. See [docs/REGISTRY.md](docs/REGISTRY.md) for detailed patterns.

Notes:
- Normal wrappers (`ThreadWrapper`, `JThreadWrapper`, `PThreadWrapper`) remain zero-overhead.
- The registry **requires control blocks** for all operations. Threads must be registered with control blocks to be controllable via the registry.
- Use `*Reg` wrappers (e.g., `ThreadWrapperReg`) or `AutoRegisterCurrentThread` for automatic control block creation and registration.

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

ThreadSchedule uses `threadschedule::expected<T, std::error_code>` (and `expected<void, std::error_code>`). When available, this aliases to `std::expected`, otherwise, a compatible fallback based on [P0323R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0323r3.pdf) is used. 

> Note: when building with `-fno-exceptions`, behavior is not standard-conforming because `value()`/`operator*` cannot throw `bad_expected_access` on error (exceptions are disabled). In that mode, always check `has_value()` or use `value_or()` before accessing the value. 

Recommended usage:

```cpp
auto r = worker.set_name("my_worker");
if (!r) {
    // Inspect r.error() (std::error_code)
}

auto value = pool.submit([]{ return 42; }); // standard future-based API remains unchanged
```

## API Overview

### Thread Wrappers

| Class | Description | Available On |
|-------|-------------|--------------|
| `ThreadWrapper` | Enhanced `std::thread` with naming, priority, affinity | Linux, Windows |
| `JThreadWrapper` | Enhanced `std::jthread` with cooperative cancellation (C++20) | Linux, Windows |
| `PThreadWrapper` | Modern C++ interface for POSIX threads | Linux only |

#### Passing wrappers into APIs expecting std::thread/std::jthread

- `std::thread` and `std::jthread` are move-only. When an API expects `std::thread&&` or `std::jthread&&`, pass the underlying thread via `release()` from the wrapper.
- Avoid relying on implicit conversions; `release()` clearly transfers ownership and prevents accidental selection of the functor constructor of `std::thread`.

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

| Class | Description | Available On |
|-------|-------------|--------------|
| `ThreadWrapperView` | View over an existing `std::thread` | Linux, Windows |
| `JThreadWrapperView` | View over an existing `std::jthread` (C++20) | Linux, Windows |
| `ThreadByNameView` | Locate and control a thread by its name | Linux only |

### Thread Pools

| Class | Use Case | Performance |
|-------|----------|-------------|
| `ThreadPool` | General-purpose, simple API | < 1k tasks/sec |
| `HighPerformancePool` | Work-stealing, optimized for throughput | 10k+ tasks/sec |
| `FastThreadPool` | Single-queue, minimal overhead | 1k-10k tasks/sec |

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

**For more details:** See the [Integration Guide](docs/INTEGRATION.md), [Registry Guide](docs/REGISTRY.md), and [CMake Reference](docs/CMAKE_REFERENCE.md) linked at the top of this README.

## Feature Status & Roadmap

### ✅ Implemented Features

#### Core Thread Management
- ✅ Enhanced thread wrappers (`ThreadWrapper`, `JThreadWrapper`, `PThreadWrapper`)
- ✅ Non-owning thread views for existing threads
- ✅ Thread naming, priority, CPU affinity control
- ✅ Scheduling policies (FIFO, RR, BATCH, IDLE)
- ✅ Process-wide thread registry with chainable query API
- ✅ Multi-DSO support (header-only + shared runtime)

#### Thread Pools
- ✅ Three pool types: `ThreadPool`, `FastThreadPool`, `HighPerformancePool`
- ✅ Work-stealing architecture (10k+ tasks/sec)
- ✅ Batch task submission
- ✅ Parallel for_each support
- ✅ Pool configuration (names, affinity, priority)
- ✅ Built-in performance statistics

#### Advanced Features
- ✅ **Scheduled Tasks** - Run tasks at specific times, after delays, or periodically
- ✅ **Error Handling** - Global and per-future error callbacks with detailed context
- ✅ Template-based scheduler (works with any pool type)
- ✅ Cancellable scheduled tasks
- ✅ Error statistics and tracking

### 🚧 In Progress / Planned Features

#### High Priority
- 📋 **Task Dependencies** - DAG-based task execution with dependencies
- 📋 **Priority Queues** - Task prioritization within pools
- 📋 **Resource Limits** - Max queue size, memory limits, task timeouts
- 📋 **Thread Watchdog** - Deadlock detection and thread health monitoring

#### Medium Priority
- 📋 **Structured Concurrency** - Task groups with cancel propagation
- 📋 **C++20 Coroutines** - co_await support for async tasks
- 📋 **Message Channels** - Thread-safe producer-consumer channels
- 📋 **Advanced Metrics** - Latency histograms (P50, P95, P99), historical data
- 📋 **Dynamic Pool Sizing** - Auto-scale thread pools based on load
- 📋 **Thread-Local Storage** - TLS management helpers

#### Low Priority / Future
- 📋 **NUMA-aware Scheduling** - Locality-aware work distribution
- 📋 **Real-time Deadline Scheduling** - SCHED_DEADLINE support
- 📋 **Async I/O Integration** - io_uring, IOCP integration
- 📋 **GPU/Accelerator Support** - CUDA/OpenCL task submission
- 📋 **Builder Pattern** - Fluent API for pool construction
- 📋 **Pipeline API** - Stream-processing patterns

### 💡 Feature Requests

Have an idea for a new feature? [Open an issue](https://github.com/Katze719/ThreadSchedule/issues) or contribute via pull request!

## Performance

ThreadSchedule provides comprehensive performance benchmarking with realistic real-world scenarios:

### Benchmark Coverage
- **7 Benchmark Suites** covering core performance, image processing, web servers, databases, and audio/video processing
- **Real-world scenarios** including image processing workload, HTTP APIs, database operations, and streaming
- **Platform testing** across Linux x86_64/ARM64, Windows MSVC/MinGW, and macOS

### Performance Characteristics

**HighPerformancePool** (Work-stealing architecture):
- **500k-2M+ tasks/second** throughput depending on workload complexity
- **< 20% work stealing ratio** indicates optimal load balancing
- **Cache-optimized** data structures for minimal memory access overhead

**FastThreadPool** (Single queue):
- **100k-1M tasks/second** for consistent, medium-complexity workloads
- **Lower memory overhead** than work-stealing pools
- **Predictable performance** for stable load patterns

**ThreadPool** (Simple general-purpose):
- **50k-500k tasks/second** for basic task distribution
- **Lowest memory footprint** and simplest debugging
- **Best for simple, predictable workloads**

### Benchmark Results
Performance varies by system configuration, workload characteristics, and task complexity. See [benchmarks/](benchmarks/) for detailed performance analysis, real-world scenario testing, and optimization recommendations.


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

**Note**: `PThreadWrapper` is Linux-only. Use `ThreadWrapper` or `JThreadWrapper` for cross-platform code.

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes with clear messages
4. Push to your branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- POSIX threads documentation
- Modern C++ threading best practices
- Linux kernel scheduling documentation
- C++20/23 concurrency improvements
