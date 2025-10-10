# ThreadSchedule

[![Tests](https://github.com/Katze719/ThreadSchedule/workflows/Tests/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/tests.yml)
[![Integration](https://github.com/Katze719/ThreadSchedule/workflows/Integration/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/integration.yml)
[![Code Quality](https://github.com/Katze719/ThreadSchedule/workflows/Code%20Quality/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/code-quality.yml)
[![Release](https://github.com/Katze719/ThreadSchedule/workflows/Release/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/release.yml)
[![Documentation](https://github.com/Katze719/ThreadSchedule/workflows/Documentation/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A modern C++ library for advanced thread management on Linux and Windows. ThreadSchedule provides enhanced wrappers for `std::thread`, `std::jthread`, and `pthread` with extended functionality including thread naming, priority management, CPU affinity, and high-performance thread pools.

Available as **header-only** or with optional **shared runtime** for multi-DSO applications.

## Key Features

- **Modern C++**: Full C++17, C++20, and C++23 support with automatic feature detection
- **Header-Only or Shared Runtime**: Choose based on your needs
- **Enhanced Wrappers**: Extend `std::thread`, `std::jthread`, and `pthread` with powerful features
- **Non-owning Views**: Zero-overhead views to configure existing threads or find by name (Linux)
- **Thread Naming**: Human-readable thread names for debugging
- **Priority & Scheduling**: Fine-grained control over thread priorities and scheduling policies
- **CPU Affinity**: Pin threads to specific CPU cores
- **Global Control Registry**: Process-wide registry to list and control running threads (affinity, priority, name)
- **High-Performance Pools**: Work-stealing thread pool optimized for 10k+ tasks/second
- **Performance Metrics**: Built-in statistics and monitoring
- **RAII & Exception Safety**: Automatic resource management
- **Multiple Integration Methods**: CMake, CPM, Conan, FetchContent

## Documentation

- **[Integration Guide](docs/INTEGRATION.md)** - CMake, Conan, FetchContent, system installation
- **[Thread Registry Guide](docs/REGISTRY.md)** - Process-wide thread control and multi-DSO patterns
- **[CMake Reference](docs/CMAKE_REFERENCE.md)** - Build options, targets, and troubleshooting

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

> **Ubuntu 24.04 Clang**: Clang 14 is limited to C++17 on 24.04; for C++20/23, Clang 19 is used.
>
> **Windows ARM64**: Not currently covered by GitHub-hosted runners, requires self-hosted runner for testing.
>
> **MinGW**: MinGW-w64 provides full Windows API support including thread naming (Windows 10+).

> ⚠️ **Known Issue (Ubuntu 24.04)**: Clang 18 with C++23 does not build reliably on Ubuntu 24.04 due to toolchain/libstdc++ incompatibilities. Use Clang 19 for C++23 on Ubuntu 24.04.

## Quick Start

### Installation

Add to your CMakeLists.txt using [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake):

```cmake
include(cmake/CPM.cmake)

CPMAddPackage(
    NAME ThreadSchedule
    GITHUB_REPOSITORY Katze719/ThreadSchedule
    GIT_TAG main  # or specific version tag
    OPTIONS "THREADSCHEDULE_BUILD_EXAMPLES OFF" "THREADSCHEDULE_BUILD_TESTS OFF"
)

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
    // Most configuration functions return expected<void, std::error_code>
    auto set_name_result = worker.set_name("my_worker");
    if (!set_name_result) {
        std::cerr << "set_name failed: " << set_name_result.error().message() << std::endl;
    }
    auto set_prio_result = worker.set_priority(ThreadPriority::normal());
    if (!set_prio_result) {
        std::cerr << "set_priority failed: " << set_prio_result.error().message() << std::endl;
    }
    
    // High-performance thread pool
    HighPerformancePool pool(4);
    auto cfg = pool.configure_threads("worker");
    if (!cfg) {
        std::cerr << "configure_threads failed: " << cfg.error().message() << std::endl;
    }
    auto dist = pool.distribute_across_cpus();
    if (!dist) {
        std::cerr << "distribute_across_cpus failed: " << dist.error().message() << std::endl;
    }
    
    auto future = pool.submit([]() { return 42; });
    std::cout << "Result: " << future.get() << std::endl;
    
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

    // Example: rename and set priority for all IO-tagged threads
    registry().apply(
        [](const RegisteredThreadInfo& e){ return e.componentTag=="io"; },
        [&](const RegisteredThreadInfo& e){
            (void)registry().set_name(e.tid, std::string("io-")+e.name);
            (void)registry().set_priority(e.tid, ThreadPriority{0});
        }
    );

    t.join();
}
```

**For multi-DSO applications:** Use the shared runtime option (`THREADSCHEDULE_RUNTIME=ON`) to ensure a single process-wide registry. See [docs/REGISTRY.md](docs/REGISTRY.md) for detailed patterns.

Notes:
- Normal wrappers (`ThreadWrapper`, `JThreadWrapper`, `PThreadWrapper`) remain zero-overhead.
- The registry uses weak references to control blocks (Windows/Linux) when available; otherwise it falls back to direct OS APIs by TID.

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

ThreadSchedule uses `threadschedule::expected<T, std::error_code>` (and `expected<void, std::error_code>`), which aliases to `std::expected` when available and otherwise uses a compatible fallback. Recommended usage:

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

## Performance

The `HighPerformancePool` achieves:

- **770k tasks/second** sustained throughput (12-core system)
- **< 1μs** average task latency
- **Work-stealing** for automatic load balancing
- **Cache-optimized** data structures

See [benchmarks/](benchmarks/) for detailed performance analysis.

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
