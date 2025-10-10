# Scheduled Tasks

ThreadSchedule provides a powerful scheduling system for running tasks at specific times or periodically.

## Features

- ✅ **One-time scheduled tasks** - Run a task after a delay or at a specific time
- ✅ **Periodic tasks** - Run tasks repeatedly at fixed intervals
- ✅ **Cancellable tasks** - Cancel scheduled tasks before they execute
- ✅ **Flexible execution** - Choose from ThreadPool (default), HighPerformancePool, or FastThreadPool
- ✅ **Thread-safe** - Safe to use from multiple threads

## Quick Start

```cpp
#include <threadschedule/threadschedule.hpp>
using namespace threadschedule;

int main() {
    ScheduledThreadPool scheduler(4); // 4 worker threads
    
    // Run a task after 5 seconds
    auto handle = scheduler.schedule_after(std::chrono::seconds(5), []() {
        std::cout << "Task executed!\n";
    });
    
    // Cancel if needed
    ScheduledThreadPool::cancel(handle);
}
```

## API Reference

### Pool Types

ThreadSchedule provides three variants of the scheduled pool:

```cpp
// Default: Uses ThreadPool (< 1k tasks/sec, simple and efficient)
ScheduledThreadPool scheduler(4);

// High-performance: Uses HighPerformancePool (10k+ tasks/sec, work-stealing)
ScheduledHighPerformancePool scheduler_hp(4);

// Fast: Uses FastThreadPool (1k-10k tasks/sec, single queue)
ScheduledFastThreadPool scheduler_fast(4);

// Custom: Use any pool type
ScheduledThreadPoolT<MyCustomPool> scheduler_custom(4);
```

### Constructor

```cpp
ScheduledThreadPool(size_t worker_threads = std::thread::hardware_concurrency());
```

Create a scheduled thread pool with the specified number of worker threads.

### Scheduling Methods

#### schedule_after()

```cpp
auto schedule_after(Duration delay, Task task) -> ScheduledTaskHandle;
```

Schedule a task to run after a delay.

**Example:**
```cpp
auto handle = scheduler.schedule_after(std::chrono::seconds(10), []() {
    std::cout << "Executed after 10 seconds\n";
});
```

#### schedule_at()

```cpp
auto schedule_at(TimePoint time_point, Task task) -> ScheduledTaskHandle;
```

Schedule a task to run at a specific time point.

**Example:**
```cpp
auto future_time = std::chrono::steady_clock::now() + std::chrono::minutes(5);
auto handle = scheduler.schedule_at(future_time, []() {
    std::cout << "Executed at specific time\n";
});
```

#### schedule_periodic()

```cpp
auto schedule_periodic(Duration interval, Task task) -> ScheduledTaskHandle;
```

Schedule a task to run periodically at fixed intervals. The task runs immediately and then repeats.

**Example:**
```cpp
auto handle = scheduler.schedule_periodic(std::chrono::seconds(1), []() {
    std::cout << "Runs every second\n";
});
```

#### schedule_periodic_after()

```cpp
auto schedule_periodic_after(Duration initial_delay, Duration interval, Task task) 
    -> ScheduledTaskHandle;
```

Schedule a task to run periodically with an initial delay.

**Example:**
```cpp
auto handle = scheduler.schedule_periodic_after(
    std::chrono::seconds(5),    // Start after 5 seconds
    std::chrono::seconds(1),    // Then repeat every 1 second
    []() {
        std::cout << "Periodic task\n";
    }
);
```

### Cancellation

```cpp
static void cancel(ScheduledTaskHandle& handle);
```

Cancel a scheduled task before it executes. For periodic tasks, this stops future executions.

**Example:**
```cpp
auto handle = scheduler.schedule_periodic(std::chrono::seconds(1), task);
// Later...
ScheduledThreadPool::cancel(handle);
```

### Status Methods

```cpp
[[nodiscard]] auto scheduled_count() const -> size_t;
```

Get the number of scheduled tasks (including periodic tasks).

```cpp
[[nodiscard]] auto thread_pool() -> PoolType&;
```

Access the underlying thread pool for direct task submission. The type depends on the pool variant used.

### Configuration

```cpp
auto configure_threads(std::string const& name_prefix, 
                      SchedulingPolicy policy = SchedulingPolicy::OTHER,
                      ThreadPriority priority = ThreadPriority::normal()) 
    -> expected<void, std::error_code>;
```

Configure worker thread properties (names, scheduling policy, priority).

## Complete Example

```cpp
#include <threadschedule/threadschedule.hpp>
#include <chrono>
#include <iostream>

using namespace threadschedule;
using namespace std::chrono_literals;

int main() {
    // Use default ThreadPool (suitable for most use cases)
    ScheduledThreadPool scheduler(4);
    scheduler.configure_threads("scheduler");
    
    // One-time task after delay
    auto delayed_task = scheduler.schedule_after(2s, []() {
        std::cout << "Delayed task executed\n";
    });
    
    // One-time task at specific time
    auto future_time = std::chrono::steady_clock::now() + 5s;
    auto timed_task = scheduler.schedule_at(future_time, []() {
        std::cout << "Timed task executed\n";
    });
    
    // Periodic task (runs every second)
    int counter = 0;
    auto periodic = scheduler.schedule_periodic(1s, [&counter]() {
        counter++;
        std::cout << "Periodic task #" << counter << "\n";
        
        // Stop after 5 executions
        if (counter >= 5) {
            // Note: This is just for demo; in practice, store handle outside
        }
    });
    
    // Let tasks run
    std::this_thread::sleep_for(6s);
    
    // Cancel periodic task
    ScheduledThreadPool::cancel(periodic);
    
    // Cleanup
    scheduler.shutdown();
    
    return 0;
}
```

## Use Cases

### Periodic Monitoring

```cpp
auto monitor = scheduler.schedule_periodic(std::chrono::seconds(30), []() {
    // Check system health every 30 seconds
    check_system_health();
});
```

### Delayed Cleanup

```cpp
auto cleanup = scheduler.schedule_after(std::chrono::minutes(5), []() {
    // Clean up temporary files after 5 minutes
    cleanup_temp_files();
});
```

### Timed Reminders

```cpp
auto reminder_time = calculate_reminder_time();
auto reminder = scheduler.schedule_at(reminder_time, []() {
    send_notification("Time for your meeting!");
});
```

### Background Jobs

```cpp
// Database backup every day at 2 AM
auto backup = scheduler.schedule_periodic(std::chrono::hours(24), []() {
    backup_database();
});
```

## Choosing the Right Pool

### ThreadPool (Default)
- **Use when**: General-purpose scheduling (< 1k tasks/sec)
- **Pros**: Simple, low overhead, easy to debug
- **Best for**: Timers, periodic maintenance, background jobs

### HighPerformancePool
- **Use when**: High-frequency task scheduling (10k+ tasks/sec)
- **Pros**: Work-stealing, optimal for many small tasks
- **Best for**: Real-time systems, high-throughput scenarios

### FastThreadPool
- **Use when**: Medium-frequency scheduling (1k-10k tasks/sec)
- **Pros**: Single queue, balanced performance
- **Best for**: Batch processing, moderate workloads

**Example:**
```cpp
// For infrequent timers (default)
ScheduledThreadPool scheduler(2);

// For high-frequency event processing
ScheduledHighPerformancePool scheduler_hp(8);
```

## Performance Notes

- The scheduler uses a single scheduling thread and a worker pool for execution
- Task throughput depends on the chosen pool type
- Cancellation is lightweight and doesn't require task execution
- Periodic tasks automatically reschedule after execution

## Advanced Example: Multiple Pool Types

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>

using namespace threadschedule;
using namespace std::chrono_literals;

int main() {
    // Low-frequency maintenance tasks
    ScheduledThreadPool maintenance(2);
    maintenance.configure_threads("maintenance");
    
    auto cleanup = maintenance.schedule_periodic(1h, []() {
        cleanup_temp_files();
    });
    
    auto backup = maintenance.schedule_periodic(24h, []() {
        backup_database();
    });
    
    // High-frequency real-time tasks
    ScheduledHighPerformancePool realtime(8);
    realtime.configure_threads("realtime");
    realtime.thread_pool().distribute_across_cpus();
    
    auto monitor = realtime.schedule_periodic(100ms, []() {
        monitor_system_health();
    });
    
    auto metrics = realtime.schedule_periodic(1s, []() {
        collect_metrics();
    });
    
    // Both schedulers run independently
    std::this_thread::sleep_for(10s);
    
    // Shutdown
    ScheduledThreadPool::cancel(cleanup);
    ScheduledHighPerformancePool::cancel(monitor);
    
    maintenance.shutdown();
    realtime.shutdown();
}
```

## Thread Safety

All methods are thread-safe and can be called from multiple threads:

```cpp
ScheduledThreadPool scheduler(4);

// Thread 1
auto handle1 = scheduler.schedule_after(1s, task1);

// Thread 2
auto handle2 = scheduler.schedule_periodic(2s, task2);

// Thread 3
ScheduledThreadPool::cancel(handle1);
```

## See Also

- [Error Handling](ERROR_HANDLING.md) - Error handling for scheduled tasks
- [Thread Pools](../README.md#thread-pools) - Underlying execution pools

