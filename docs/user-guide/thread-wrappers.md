# Thread Wrappers

ThreadSchedule provides enhanced wrappers around standard C++ threading primitives with additional functionality.

## ThreadWrapper

Enhanced wrapper for `std::thread` with naming, priority, and affinity control.

### Basic Usage

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

// Simple named thread
ThreadWrapper worker("MyWorker", []() {
    std::cout << "Worker thread running\n";
});

worker.join();
```

### Thread Configuration

```cpp
ThreadWrapper worker("ConfiguredWorker", []() {
    // Work here
});

// Set thread name
worker.set_name("NewWorkerName");

// Set CPU affinity (pin to cores 0 and 1)
worker.set_affinity({0, 1});

// Set priority
#ifdef _WIN32
worker.set_priority(THREAD_PRIORITY_ABOVE_NORMAL);
#else
worker.set_priority(5);  // Nice value on Linux
#endif

worker.join();
```

### Getting Thread Information

```cpp
ThreadWrapper worker("InfoWorker", []() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
});

// Get thread ID
auto tid = worker.get_id();
std::cout << "Thread ID: " << tid << "\n";

// Get thread name
auto name = worker.get_name();
std::cout << "Thread name: " << name << "\n";

// Check if joinable
if (worker.joinable()) {
    worker.join();
}
```

## JThreadWrapper (C++20)

Enhanced wrapper for `std::jthread` with automatic stop support.

### Basic Usage

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

// Create stoppable thread
JThreadWrapper worker("StoppableWorker", [](std::stop_token stop) {
    while (!stop.stop_requested()) {
        // Do work
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
});

// Request stop
worker.request_stop();

// Automatically joins on destruction
```

### Cooperative Cancellation

```cpp
JThreadWrapper worker("CooperativeWorker", [](std::stop_token stop) {
    for (int i = 0; i < 100 && !stop.stop_requested(); ++i) {
        std::cout << "Processing item " << i << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
});

// Let it run for a while
std::this_thread::sleep_for(std::chrono::seconds(2));

// Request graceful stop
worker.request_stop();

// Thread will finish current iteration and exit
```

### Stop Callbacks

```cpp
JThreadWrapper worker("CallbackWorker", [](std::stop_token stop) {
    std::stop_callback callback(stop, []() {
        std::cout << "Stop requested, cleaning up...\n";
    });
    
    while (!stop.stop_requested()) {
        // Work
    }
});
```

## PThreadWrapper (Linux Only)

Direct wrapper for POSIX threads with full pthread API access.

### Basic Usage

```cpp
#include <threadschedule/threadschedule.hpp>

#ifdef __linux__
using namespace threadschedule;

PThreadWrapper worker("PosixWorker", []() {
    std::cout << "POSIX thread running\n";
});

worker.join();
#endif
```

### Real-Time Scheduling

```cpp
#ifdef __linux__
PThreadWrapper worker("RTWorker", []() {
    // Real-time work
});

// Set real-time FIFO scheduling with priority 50
sched_param param{};
param.sched_priority = 50;
worker.set_scheduling_policy(SCHED_FIFO, param);

worker.join();
#endif
```

### Advanced pthread Features

```cpp
#ifdef __linux__
PThreadWrapper worker("AdvancedWorker", []() {
    // Work here
});

// Set stack size
size_t stack_size = 1024 * 1024;  // 1MB
worker.set_stack_size(stack_size);

// Set CPU affinity
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(0, &cpuset);
worker.set_affinity_native(cpuset);

// Get native pthread handle
pthread_t handle = worker.native_handle();

worker.join();
#endif
```

## Thread Views (Non-Owning)

Zero-overhead views to configure existing threads without ownership.

### ThreadView

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

// Create a standard thread
std::thread t([]() {
    std::this_thread::sleep_for(std::chrono::seconds(5));
});

// Create non-owning view
ThreadView view(t.get_id());

// Configure through view
view.set_name("ViewConfiguredThread");
view.set_priority(10);

// Original thread still owns the thread
t.join();
```

### JThreadView (C++20)

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

std::jthread t([](std::stop_token stop) {
    while (!stop.stop_requested()) {
        // Work
    }
});

// Create view
JThreadView view(t.get_id(), t.get_stop_token());

// Configure through view
view.set_name("JThreadViewed");

// Request stop through view
view.request_stop();
```

### PThreadView (Linux Only)

```cpp
#ifdef __linux__
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

pthread_t handle;
pthread_create(&handle, nullptr, [](void*) -> void* {
    // Work
    return nullptr;
}, nullptr);

// Create view
PThreadView view(handle);

// Configure through view
view.set_name("PThreadViewed");
view.set_scheduling_policy(SCHED_RR, {.sched_priority = 30});

pthread_join(handle, nullptr);
#endif
```

## Best Practices

### 1. Choose the Right Wrapper

- **ThreadWrapper**: General purpose, cross-platform
- **JThreadWrapper**: When you need cooperative cancellation (C++20)
- **PThreadWrapper**: Linux-only, when you need real-time scheduling
- **Views**: When you don't own the thread but need to configure it

### 2. Always Name Your Threads

```cpp
// Good
ThreadWrapper worker("DataProcessor", []() { /* ... */ });

// Bad
ThreadWrapper worker([]() { /* ... */ });
```

Thread names make debugging much easier!

### 3. Handle Join Properly

```cpp
// Good - explicit join
ThreadWrapper worker("Worker", []() { /* ... */ });
if (worker.joinable()) {
    worker.join();
}

// Also good - use JThreadWrapper for automatic join
JThreadWrapper worker("Worker", [](std::stop_token stop) { /* ... */ });
// Automatically joins on destruction
```

### 4. Set Affinity Early

```cpp
ThreadWrapper worker("Worker", []() {
    // Work here - already running on pinned CPU
});

// Set affinity before thread starts doing work
worker.set_affinity({2, 3});

worker.join();
```

### 5. Use Views for External Threads

```cpp
// When you don't own the thread
void configure_external_thread(std::thread::id tid) {
    ThreadView view(tid);
    view.set_name("ExternalWorker");
    view.set_priority(5);
}
```

## Platform Differences

### Windows vs Linux

| Feature | Windows | Linux |
|---------|---------|-------|
| Thread Naming | ✅ (Windows 10+) | ✅ |
| Priority Control | ✅ (Thread priority) | ✅ (Nice value) |
| CPU Affinity | ✅ (Affinity mask) | ✅ (CPU set) |
| Real-time Scheduling | ❌ | ✅ (POSIX) |
| PThreadWrapper | ❌ | ✅ |

### Priority Values

**Windows**:
```cpp
// Thread priority constants
THREAD_PRIORITY_IDLE
THREAD_PRIORITY_LOWEST
THREAD_PRIORITY_BELOW_NORMAL
THREAD_PRIORITY_NORMAL
THREAD_PRIORITY_ABOVE_NORMAL
THREAD_PRIORITY_HIGHEST
THREAD_PRIORITY_TIME_CRITICAL
```

**Linux**:
```cpp
// Nice values: -20 (highest) to 19 (lowest)
worker.set_priority(-5);  // High priority
worker.set_priority(0);   // Normal priority
worker.set_priority(10);  // Low priority
```

## Next Steps

- Learn about [Thread Pools](thread-pools.md) for managing multiple threads efficiently
- Explore [Thread Registry](../REGISTRY.md) for process-wide thread management
- Check [Error Handling](../ERROR_HANDLING.md) for robust error management
