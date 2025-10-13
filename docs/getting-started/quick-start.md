# Quick Start Guide

This guide will get you up and running with ThreadSchedule in minutes.

## Basic Thread Management

### Creating Named Threads

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>

using namespace threadschedule;

int main() {
    // Create a named thread
    ThreadWrapper worker("WorkerThread", []() {
        std::cout << "Running in worker thread\n";
    });
    
    worker.join();
    return 0;
}
```

### Using JThread (C++20)

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    // JThreadWrapper with automatic stop support
    JThreadWrapper worker("CancelableWorker", [](std::stop_token stop) {
        while (!stop.stop_requested()) {
            // Do work
        }
    });
    
    // Thread automatically joins on destruction
    return 0;
}
```

## Thread Pools

### High-Performance Pool

Best for high-throughput workloads with many small tasks.

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>
#include <vector>

using namespace threadschedule;

int main() {
    // Create pool with 4 worker threads
    HighPerformancePool pool(4);
    pool.configure_threads("worker");
    
    // Submit tasks
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([i]() {
            return i * i;
        }));
    }
    
    // Get results
    for (auto& future : futures) {
        std::cout << future.get() << " ";
    }
    std::cout << "\n";
    
    return 0;
}
```

### Fast Thread Pool

Optimized for consistent task processing with minimal overhead.

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    FastThreadPool pool(4);
    
    // Submit tasks
    auto future1 = pool.submit([]() { return 42; });
    auto future2 = pool.submit([]() { return "Hello"; });
    
    std::cout << future1.get() << "\n";
    std::cout << future2.get() << "\n";
    
    return 0;
}
```

### Standard Thread Pool

General-purpose pool for mixed workloads.

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    ThreadPool pool(4);
    
    // Submit void tasks
    pool.submit([]() {
        std::cout << "Task executed\n";
    });
    
    // Submit tasks with return values
    auto future = pool.submit([]() {
        return "Result";
    });
    
    std::cout << future.get() << "\n";
    
    return 0;
}
```

## Scheduled Tasks

### Run After Delay

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>

using namespace threadschedule;

int main() {
    ScheduledPool pool(2);
    
    // Run after 5 seconds
    pool.schedule_after(std::chrono::seconds(5), []() {
        std::cout << "Executed after 5 seconds\n";
    });
    
    // Keep program running
    std::this_thread::sleep_for(std::chrono::seconds(6));
    
    return 0;
}
```

### Run at Specific Time

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>

using namespace threadschedule;

int main() {
    ScheduledPool pool(2);
    
    // Run at specific time
    auto target_time = std::chrono::system_clock::now() + std::chrono::seconds(10);
    pool.schedule_at(target_time, []() {
        std::cout << "Executed at scheduled time\n";
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(11));
    
    return 0;
}
```

### Periodic Tasks

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>

using namespace threadschedule;

int main() {
    ScheduledPool pool(2);
    
    // Run every second
    pool.schedule_every(std::chrono::seconds(1), []() {
        std::cout << "Periodic task executed\n";
    });
    
    // Keep running for 10 seconds
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    return 0;
}
```

## Thread Configuration

### Setting Priority

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    ThreadWrapper worker("HighPriorityWorker", []() {
        // Work here
    });
    
    // Set high priority (platform-specific)
    #ifdef _WIN32
    worker.set_priority(THREAD_PRIORITY_HIGHEST);
    #else
    worker.set_priority(10);  // Nice value on Linux
    #endif
    
    worker.join();
    return 0;
}
```

### CPU Affinity

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    ThreadWrapper worker("PinnedWorker", []() {
        // Work here
    });
    
    // Pin to CPU core 0
    worker.set_affinity({0});
    
    worker.join();
    return 0;
}
```

## Thread Registry

### List All Threads

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>

using namespace threadschedule;

int main() {
    // Create some threads
    ThreadWrapper w1("Worker1", []() { /* work */ });
    ThreadWrapper w2("Worker2", []() { /* work */ });
    
    // List all registered threads
    auto threads = ThreadRegistry::get_all_threads();
    for (const auto& info : threads) {
        std::cout << "Thread: " << info.name << "\n";
    }
    
    w1.join();
    w2.join();
    return 0;
}
```

### Find Thread by Name

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    ThreadWrapper worker("MyWorker", []() {
        std::this_thread::sleep_for(std::chrono::seconds(5));
    });
    
    // Find thread by name
    auto thread_id = ThreadRegistry::find_by_name("MyWorker");
    if (thread_id) {
        std::cout << "Found thread with ID: " << *thread_id << "\n";
    }
    
    worker.join();
    return 0;
}
```

## Error Handling

### With Error Callbacks

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>

using namespace threadschedule;

int main() {
    ThreadPoolWithErrors pool(4);
    
    // Set error callback
    pool.set_error_callback([](const std::exception& e, const ErrorContext& ctx) {
        std::cerr << "Error in task " << ctx.task_id << ": " << e.what() << "\n";
    });
    
    // Submit task that might throw
    pool.submit([]() {
        throw std::runtime_error("Task failed!");
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
```

### Using Expected Type

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>

using namespace threadschedule;

Expected<int, std::string> divide(int a, int b) {
    if (b == 0) {
        return Unexpected("Division by zero");
    }
    return a / b;
}

int main() {
    auto result = divide(10, 2);
    
    if (result) {
        std::cout << "Result: " << *result << "\n";
    } else {
        std::cerr << "Error: " << result.error() << "\n";
    }
    
    return 0;
}
```

## Next Steps

- Explore the [User Guide](../user-guide/thread-wrappers.md) for detailed feature documentation
- Learn about [Thread Registry](../REGISTRY.md) for process-wide thread management
- Check [Scheduled Tasks](../SCHEDULED_TASKS.md) for advanced scheduling
- Read [Error Handling](../ERROR_HANDLING.md) for robust error management
- Review the [API Reference](../ThreadSchedule/annotated.md) for complete API details
