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
- **High-Performance Pools**: Work-stealing thread pool optimized for 10k+ tasks/second
- **Scheduled Tasks**: Run tasks at specific times, after delays, or periodically
- **Error Handling**: Comprehensive exception handling with error callbacks and context
- **Performance Metrics**: Built-in statistics and monitoring
- **RAII & Exception Safety**: Automatic resource management
- **Multiple Integration Methods**: CMake, CPM, Conan, FetchContent

## Quick Start

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

### Basic Usage

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    // Create high-performance thread pool
    HighPerformancePool pool(4);
    pool.configure_threads("worker");
    
    // Submit tasks and get futures
    auto future = pool.submit([]() {
        return 42;
    });
    
    std::cout << "Result: " << future.get() << std::endl;
    
    // Use ThreadWrapper for named threads
    ThreadWrapper worker("MyWorker", []() {
        // Thread work here
    });
    
    worker.join();
    return 0;
}
```

## Documentation

Explore the comprehensive documentation:

- **[Getting Started](getting-started/installation.md)** - Installation and first steps
- **[Integration Guide](INTEGRATION.md)** - CMake, Conan, FetchContent, system installation
- **[Thread Registry Guide](REGISTRY.md)** - Process-wide thread control and multi-DSO patterns
- **[Scheduled Tasks Guide](SCHEDULED_TASKS.md)** - Timer and periodic task scheduling
- **[Error Handling Guide](ERROR_HANDLING.md)** - Exception handling with callbacks
- **[CMake Reference](CMAKE_REFERENCE.md)** - Build options, targets, and troubleshooting
- **[API Reference](api/ThreadSchedule/index.md)** - Complete API documentation

## Platform Support

ThreadSchedule is continuously tested on:

- **Linux** (x86_64, ARM64): Ubuntu 22.04/24.04 with GCC 11 and Clang 14/19
- **Windows**: Server 2022/2025 with MSVC 2022 and MinGW-w64

All platforms support C++17, C++20, and C++23 standards.

## Features

### Thread Management
- Enhanced thread wrappers (`ThreadWrapper`, `JThreadWrapper`, `PThreadWrapper`)
- Thread naming for easy debugging
- CPU affinity control
- Priority and scheduling policies
- Non-owning thread views

### Thread Pools
- High-performance work-stealing pools
- Fast thread pools with optimized task queues
- Standard thread pools for general use
- Scheduled task execution

### Advanced Features
- Process-wide thread registry
- Error handling with callbacks
- Performance metrics and monitoring
- RAII and exception safety
- Multi-DSO support

## License

This project is licensed under the MIT License - see the [LICENSE](https://github.com/Katze719/ThreadSchedule/blob/main/LICENSE) file for details.

## Contributing

Contributions are welcome! Check out our [Contributing Guide](development/contributing.md) to get started.
