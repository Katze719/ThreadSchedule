# ThreadSchedule

A modern C++23 header-only library for advanced thread management on Linux systems. ThreadSchedule provides C++ wrappers for pthreads, std::thread, and std::jthread with extended functionality including thread naming, priority management, CPU affinity, and scheduling policies.

## Features

- 🚀 **Modern C++**: Leverages C++23 features with fallbacks for older compilers
- 🔧 **Header-only**: Easy integration, no separate compilation required
- 🧵 **Thread Wrappers**: Enhanced std::thread, std::jthread, and pthread interfaces
- 📛 **Thread Naming**: Set human-readable names for threads
- ⚡ **Priority Control**: Manage thread priorities and scheduling policies
- 🎯 **CPU Affinity**: Control which CPUs threads run on
- 🏊 **Thread Pool**: High-performance thread pool with advanced features
- 📊 **Monitoring**: Built-in statistics and thread information utilities
- 🔒 **RAII**: Automatic resource management with exception safety

## Requirements

- Linux operating system
- C++17 or later (C++23 recommended for full feature set)
- CMake 3.28+
- GCC 10+ or Clang 12+

## Quick Start

### Installation

```bash
git clone https://github.com/your-username/ThreadSchedule.git
cd ThreadSchedule
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Basic Usage

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    // Create a named thread with priority
    ThreadWrapper worker([]() {
        std::cout << "Worker thread running!" << std::endl;
    });
    
    // Configure the thread
    worker.set_name("my_worker");
    worker.set_priority(ThreadPriority::normal());
    worker.set_scheduling_policy(SchedulingPolicy::FIFO, ThreadPriority(5));
    
    // Set CPU affinity
    ThreadAffinity affinity({0, 1}); // Run on CPUs 0 and 1
    worker.set_affinity(affinity);
    
    return 0; // Thread is automatically joined
}
```

## Core Components

### Thread Wrappers

#### ThreadWrapper (std::thread)
Enhanced wrapper for `std::thread` with additional functionality:

```cpp
ThreadWrapper worker(task_function, arg1, arg2);
worker.set_name("worker_thread");
worker.set_priority(ThreadPriority(10));
```

#### JThreadWrapper (std::jthread) - C++20+
Enhanced wrapper for `std::jthread` with cooperative cancellation:

```cpp
JThreadWrapper worker(task_function);
worker.request_stop(); // Cooperative cancellation
```

#### PThreadWrapper
Modern C++ interface for POSIX threads:

```cpp
PThreadWrapper worker(task_function);
worker.set_cancel_state(true);
worker.set_cancel_type(false); // Deferred cancellation
```

### Thread Pool

High-performance thread pool with advanced scheduling:

```cpp
ThreadPool pool(8); // 8 worker threads
pool.configure_threads("worker", SchedulingPolicy::OTHER, ThreadPriority::normal());

// Submit tasks
auto future = pool.submit([]() { return 42; });
int result = future.get();

// Parallel execution
std::vector<int> data = {1, 2, 3, 4, 5};
pool.parallel_for_each(data.begin(), data.end(), [](int& x) {
    x *= 2;
});
```

### Scheduling Policies

```cpp
enum class SchedulingPolicy {
    OTHER,    // Standard round-robin time-sharing
    FIFO,     // First in, first out
    RR,       // Round-robin
    BATCH,    // For batch style execution
    IDLE,     // For very low priority background tasks
    DEADLINE  // Real-time deadline scheduling (if supported)
};
```

### Priority Management

```cpp
ThreadPriority priority(10);              // Custom priority
ThreadPriority::lowest();                 // Minimum priority
ThreadPriority::normal();                 // Default priority  
ThreadPriority::highest();                // Maximum priority
```

### CPU Affinity

```cpp
ThreadAffinity affinity;
affinity.add_cpu(0);        // Add CPU 0
affinity.add_cpu(2);        // Add CPU 2
affinity.remove_cpu(0);     // Remove CPU 0

// Or use constructor
ThreadAffinity affinity({1, 2, 3}); // CPUs 1, 2, and 3
```

## Advanced Features

### Factory Methods

Create preconfigured threads:

```cpp
auto thread = ThreadWrapper::create_with_config(
    "high_priority_worker",     // Name
    SchedulingPolicy::FIFO,     // Policy
    ThreadPriority(20),         // Priority
    task_function               // Function
);
```

### Thread Information

```cpp
// Get system information
unsigned int cores = ThreadInfo::hardware_concurrency();
pid_t thread_id = ThreadInfo::get_thread_id();

auto policy = ThreadInfo::get_current_policy();
auto priority = ThreadInfo::get_current_priority();
```

### Global Thread Pool

```cpp
// Use singleton thread pool
auto future = GlobalThreadPool::submit(task_function);

// Parallel algorithms
std::vector<int> data = {1, 2, 3, 4, 5};
parallel_for_each(data, [](int& x) { x *= 2; });
```

## Building Examples

```bash
cd build
make basic_example
./basic_example

make thread_pool_example  
./thread_pool_example

make pthread_example
./pthread_example
```

## Performance Considerations

- **RAII Management**: All threads are automatically joined on destruction
- **Exception Safety**: Thread creation failures throw exceptions with detailed messages
- **Lock-free Operations**: Minimal locking in thread pool implementation
- **CPU Distribution**: Built-in methods for optimal CPU distribution

## Thread Safety

ThreadSchedule is designed with thread safety in mind:

- All wrapper classes are move-only (non-copyable)
- Thread pool operations are fully thread-safe
- Statistics and monitoring functions use appropriate synchronization
- RAII ensures proper cleanup even in exception scenarios

## Platform Support

**Supported Platforms:**
- Linux x86_64 (fully tested)
- Linux ARM64 (should work, not extensively tested)
- Other Linux architectures (should work, untested)

**Features used:**
- `pthread_setname_np` for thread naming
- `sched_setscheduler` for scheduling policies  
- `pthread_setaffinity_np` for CPU affinity
- `setpriority`/`getpriority` for nice values

All these APIs are POSIX-compliant or Linux-standard and should be available across different CPU architectures. The library doesn't use architecture-specific assembly or intrinsics.

## Contributing

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- POSIX threads documentation
- Modern C++ threading best practices
- Linux kernel scheduling documentation 
