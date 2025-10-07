# ThreadSchedule

[![CI](https://github.com/Katze719/ThreadSchedule/workflows/CI/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/ci.yml)
[![Linux Build](https://github.com/Katze719/ThreadSchedule/actions/workflows/ci.yml/badge.svg?event=push&job=linux-build)](https://github.com/Katze719/ThreadSchedule/actions/workflows/ci.yml)
[![Windows Build](https://github.com/Katze719/ThreadSchedule/actions/workflows/ci.yml/badge.svg?event=push&job=windows-build)](https://github.com/Katze719/ThreadSchedule/actions/workflows/ci.yml)
[![Tests](https://img.shields.io/github/actions/workflow/status/Katze719/ThreadSchedule/ci.yml?label=tests&logo=github)](https://github.com/Katze719/ThreadSchedule/actions/workflows/ci.yml)
[![Release](https://github.com/Katze719/ThreadSchedule/workflows/Release/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/release.yml)
[![Documentation](https://github.com/Katze719/ThreadSchedule/workflows/Documentation/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A modern C++ header-only library for advanced thread management on Linux and Windows systems. ThreadSchedule provides enhanced wrappers for `std::thread`, `std::jthread`, and `pthread` with extended functionality including thread naming, priority management, CPU affinity, and high-performance thread pools.

## Supported Platforms & Compilers

ThreadSchedule is continuously tested on the following configurations:

| Platform | Compiler | C++17 | C++20 | C++23 |
|----------|----------|:-----:|:-----:|:-----:|
| **Linux** | | | | |
| Ubuntu 22.04 | GCC 11 | ✅ | ✅ | ✅ |
| Ubuntu 22.04 | Clang 14 | ✅ | ✅ | ✅ |
| Ubuntu 24.04 | GCC 11 | ✅ | ✅ | ✅ |
| Ubuntu 24.04 | Clang 14 | ✅ | - | - |
| Ubuntu 24.04 | Clang 18 | - | ✅ | ✅ |
| **Windows** | | | | |
| Windows Server 2022 | MSVC 2022 | ✅ | ✅ | ✅ |

> **Note**: All configurations include full test suite execution and are verified on every commit via GitHub Actions CI/CD.
>
> **Ubuntu 24.04 Clang**: Clang 14 is limited to C++17 due to incompatibility with GCC 14's libstdc++. For C++20/23 on Ubuntu 24.04, Clang 18 is used.

## Key Features

- **Modern C++**: Full C++17, C++20, and C++23 support with automatic feature detection
- **Header-Only**: Zero compilation, just include and go
- **Enhanced Wrappers**: Extend `std::thread`, `std::jthread`, and `pthread` with powerful features
- **Thread Naming**: Human-readable thread names for debugging
- **Priority & Scheduling**: Fine-grained control over thread priorities and scheduling policies
- **CPU Affinity**: Pin threads to specific CPU cores
- **High-Performance Pools**: Work-stealing thread pool optimized for 10k+ tasks/second
- **Performance Metrics**: Built-in statistics and monitoring
- **RAII & Exception Safety**: Automatic resource management
- **Multiple Integration Methods**: CMake, CPM, Conan, FetchContent

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

**Other integration methods:** See [docs/INTEGRATION.md](docs/INTEGRATION.md) for FetchContent, Conan, system installation, and more.

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
    
    return 0;
}
```

**That's it!** 🎉 Header-only means zero compilation overhead.

## API Overview

### Thread Wrappers

| Class | Description | Available On |
|-------|-------------|--------------|
| `ThreadWrapper` | Enhanced `std::thread` with naming, priority, affinity | Linux, Windows |
| `JThreadWrapper` | Enhanced `std::jthread` with cooperative cancellation (C++20) | Linux, Windows |
| `PThreadWrapper` | Modern C++ interface for POSIX threads | Linux only |

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

## Documentation

- **[Integration Guide](docs/INTEGRATION.md)** - All integration methods (CPM, FetchContent, Conan, subdirectory, system install)
- **[CMake Reference](docs/CMAKE_REFERENCE.md)** - Complete CMake API and configuration options
- **[Examples](examples/)** - Working code examples
- **[Benchmarks](benchmarks/)** - Performance benchmarks and optimization guides

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
