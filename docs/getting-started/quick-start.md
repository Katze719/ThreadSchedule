# Quick Start Guide

Get started with ThreadSchedule in minutes.

## Basic Usage

### Named Threads

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    ThreadWrapper worker("WorkerThread", []() {
        // Your work here
    });
    
    worker.join();
    return 0;
}
```

### Thread Pools

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    HighPerformancePool pool(4);
    
    // Submit tasks and get results
    auto future = pool.submit([]() {
        return 42;
    });
    
    std::cout << future.get() << "\n";
    return 0;
}
```

### Scheduled Tasks

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    ScheduledPool pool(2);
    
    // Run after 5 seconds
    pool.schedule_after(std::chrono::seconds(5), []() {
        std::cout << "Delayed task executed\n";
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(6));
    return 0;
}
```

## Next Steps

- See the [User Guide](../user-guide/thread-wrappers.md) for detailed documentation
- Check [Integration Guide](../INTEGRATION.md) for installation and setup
- Review the [API Reference](../ThreadSchedule/annotated.md) for complete API details
