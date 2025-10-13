# Thread Pools

ThreadSchedule provides multiple thread pool implementations optimized for different workloads.

## Pool Types Overview

| Pool Type | Best For | Key Features |
|-----------|----------|--------------|
| `HighPerformancePool` | High throughput, many small tasks | Work-stealing, 10k+ tasks/sec |
| `FastThreadPool` | Consistent task processing | Optimized queue, minimal overhead |
| `ThreadPool` | General purpose | Simple, reliable, mixed workloads |
| `ScheduledPool` | Timed/periodic tasks | Timer-based execution |
| `ThreadPoolWithErrors` | Error-prone tasks | Built-in error handling callbacks |

## HighPerformancePool

Work-stealing thread pool optimized for maximum throughput.

### Basic Usage

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main() {
    // Create pool with 4 worker threads
    HighPerformancePool pool(4);
    
    // Submit tasks
    auto future = pool.submit([]() {
        return 42;
    });
    
    std::cout << "Result: " << future.get() << "\n";
    return 0;
}
```

### Batch Task Submission

```cpp
HighPerformancePool pool(8);
pool.configure_threads("worker");

std::vector<std::future<int>> futures;

// Submit 10000 tasks
for (int i = 0; i < 10000; ++i) {
    futures.push_back(pool.submit([i]() {
        return i * i;
    }));
}

// Collect results
for (auto& future : futures) {
    int result = future.get();
    // Process result
}
```

### Thread Configuration

```cpp
HighPerformancePool pool(4);

// Configure all worker threads
pool.configure_threads("HPWorker");

// Set priority for all workers
#ifdef _WIN32
pool.set_thread_priority(THREAD_PRIORITY_ABOVE_NORMAL);
#else
pool.set_thread_priority(-5);  // High priority on Linux
#endif

// Set CPU affinity (cores 0-3)
pool.set_thread_affinity({0, 1, 2, 3});
```

### Performance Monitoring

```cpp
HighPerformancePool pool(4);

// Submit tasks
for (int i = 0; i < 1000; ++i) {
    pool.submit([]() { /* work */ });
}

// Get statistics
auto stats = pool.get_statistics();
std::cout << "Tasks completed: " << stats.completed_tasks << "\n";
std::cout << "Tasks pending: " << stats.pending_tasks << "\n";
std::cout << "Active workers: " << stats.active_threads << "\n";
```

## FastThreadPool

Optimized for consistent task processing with minimal overhead.

### Basic Usage

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

FastThreadPool pool(4);

// Submit tasks
auto future1 = pool.submit([]() { return 42; });
auto future2 = pool.submit([]() { return "Hello"; });

std::cout << future1.get() << " " << future2.get() << "\n";
```

### Task Queuing

```cpp
FastThreadPool pool(4);

// Efficient task queuing
for (int i = 0; i < 100; ++i) {
    pool.submit([i]() {
        // Fast, consistent processing
        process_item(i);
    });
}

// Wait for completion
pool.wait_for_all();
```

## ThreadPool

General-purpose thread pool for mixed workloads.

### Basic Usage

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

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
```

### Lambda Captures

```cpp
ThreadPool pool(4);

int value = 42;
std::string message = "Hello";

// Capture by value
auto f1 = pool.submit([value]() {
    return value * 2;
});

// Capture by reference (be careful with lifetime!)
auto f2 = pool.submit([&message]() {
    return message + " World";
});

std::cout << f1.get() << " " << f2.get() << "\n";
```

### Task Dependencies

```cpp
ThreadPool pool(4);

// Task 1
auto future1 = pool.submit([]() {
    return load_data();
});

// Task 2 depends on Task 1
auto future2 = pool.submit([&future1]() {
    auto data = future1.get();
    return process_data(data);
});

// Task 3 depends on Task 2
auto future3 = pool.submit([&future2]() {
    auto result = future2.get();
    return finalize(result);
});

auto final_result = future3.get();
```

## ScheduledPool

Thread pool with timer-based task scheduling.

### Run After Delay

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

ScheduledPool pool(2);

// Run after 5 seconds
pool.schedule_after(std::chrono::seconds(5), []() {
    std::cout << "Delayed task executed\n";
});
```

### Run at Specific Time

```cpp
ScheduledPool pool(2);

// Calculate target time (10 seconds from now)
auto target = std::chrono::system_clock::now() + std::chrono::seconds(10);

// Schedule task
pool.schedule_at(target, []() {
    std::cout << "Executed at scheduled time\n";
});
```

### Periodic Tasks

```cpp
ScheduledPool pool(2);

// Run every second
pool.schedule_every(std::chrono::seconds(1), []() {
    std::cout << "Periodic task: " << std::chrono::system_clock::now() << "\n";
});

// Cancel after 10 seconds
std::this_thread::sleep_for(std::chrono::seconds(10));
pool.cancel_all_scheduled();
```

### Cron-Style Scheduling

```cpp
ScheduledPool pool(2);

// Run every day at midnight
auto schedule_daily = [&pool]() {
    auto now = std::chrono::system_clock::now();
    auto midnight = /* calculate next midnight */;
    
    pool.schedule_at(midnight, []() {
        perform_daily_maintenance();
    });
};

schedule_daily();
```

## ThreadPoolWithErrors

Thread pool with built-in error handling and callbacks.

### Basic Error Handling

```cpp
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

ThreadPoolWithErrors pool(4);

// Set global error callback
pool.set_error_callback([](const std::exception& e, const ErrorContext& ctx) {
    std::cerr << "Task " << ctx.task_id << " failed: " << e.what() << "\n";
});

// Submit task that might fail
pool.submit([]() {
    if (random_condition()) {
        throw std::runtime_error("Task failed!");
    }
    return 42;
});
```

### Per-Task Error Handling

```cpp
ThreadPoolWithErrors pool(4);

// Submit with custom error handler
pool.submit_with_error_handler(
    []() {
        // Task that might throw
        return risky_operation();
    },
    [](const std::exception& e) {
        // Custom error handling for this task
        std::cerr << "Custom handler: " << e.what() << "\n";
        return default_value();
    }
);
```

### Error Context

```cpp
ThreadPoolWithErrors pool(4);

pool.set_error_callback([](const std::exception& e, const ErrorContext& ctx) {
    std::cerr << "Error details:\n";
    std::cerr << "  Task ID: " << ctx.task_id << "\n";
    std::cerr << "  Thread: " << ctx.thread_id << "\n";
    std::cerr << "  Time: " << ctx.timestamp << "\n";
    std::cerr << "  Message: " << e.what() << "\n";
    
    // Log to file, send alert, etc.
    log_error(ctx, e);
});
```

## Pool Selection Guide

### Use HighPerformancePool When:
- Processing 1000+ tasks per second
- Tasks are small and uniform
- Need maximum throughput
- Work-stealing benefits your workload

**Example**: Image processing pipeline, data transformation, batch processing

### Use FastThreadPool When:
- Need consistent, predictable performance
- Tasks have similar execution time
- Want minimal overhead
- Don't need work-stealing complexity

**Example**: File uploads, API request handling, consistent data processing

### Use ThreadPool When:
- General-purpose workload
- Mixed task types and sizes
- Simple requirements
- Don't need specialized optimizations

**Example**: Background tasks, mixed application workload, utility operations

### Use ScheduledPool When:
- Need timer-based execution
- Periodic/delayed tasks
- Time-based workflows
- Cron-like scheduling

**Example**: Maintenance tasks, periodic polling, scheduled reports, cache cleanup

### Use ThreadPoolWithErrors When:
- Tasks frequently fail
- Need error tracking
- Want automatic error handling
- Error logging is important

**Example**: Network operations, file I/O, external API calls, data validation

## Best Practices

### 1. Size Your Pool Appropriately

```cpp
// CPU-bound tasks: use hardware concurrency
auto num_threads = std::thread::hardware_concurrency();
HighPerformancePool pool(num_threads);

// I/O-bound tasks: can use more threads
HighPerformancePool pool(num_threads * 2);

// Mixed workload: start with hardware concurrency
ThreadPool pool(num_threads);
```

### 2. Reuse Pools

```cpp
// Good - single pool for application lifetime
class Application {
    HighPerformancePool pool_{std::thread::hardware_concurrency()};
    
public:
    void process_batch(const std::vector<Item>& items) {
        for (const auto& item : items) {
            pool_.submit([item]() { process(item); });
        }
    }
};

// Bad - creating pools repeatedly
void process_batch(const std::vector<Item>& items) {
    HighPerformancePool pool(4);  // Expensive!
    // ...
}
```

### 3. Configure Threads Early

```cpp
HighPerformancePool pool(4);

// Configure before submitting tasks
pool.configure_threads("worker");
pool.set_thread_priority(high_priority);
pool.set_thread_affinity({0, 1, 2, 3});

// Now submit tasks
for (auto& task : tasks) {
    pool.submit(task);
}
```

### 4. Handle Futures Properly

```cpp
// Good - store futures and get results
std::vector<std::future<Result>> futures;
for (auto& item : items) {
    futures.push_back(pool.submit([item]() { return process(item); }));
}

// Process results
for (auto& future : futures) {
    try {
        auto result = future.get();
        handle_result(result);
    } catch (const std::exception& e) {
        handle_error(e);
    }
}

// Bad - ignoring futures
for (auto& item : items) {
    pool.submit([item]() { process(item); });  // Lost future!
}
```

### 5. Graceful Shutdown

```cpp
class Service {
    HighPerformancePool pool_{4};
    
public:
    ~Service() {
        // Wait for all tasks to complete
        pool_.wait_for_all();
        
        // Or set timeout
        pool_.wait_for_all(std::chrono::seconds(30));
    }
};
```

## Performance Tips

### 1. Batch Small Tasks

```cpp
// Instead of submitting many tiny tasks:
for (int i = 0; i < 10000; ++i) {
    pool.submit([i]() { tiny_operation(i); });
}

// Batch them:
const int batch_size = 100;
for (int i = 0; i < 10000; i += batch_size) {
    pool.submit([i, batch_size]() {
        for (int j = i; j < i + batch_size; ++j) {
            tiny_operation(j);
        }
    });
}
```

### 2. Avoid Lock Contention

```cpp
// Good - thread-local results
pool.submit([]() {
    std::vector<Result> local_results;
    for (auto& item : my_items) {
        local_results.push_back(process(item));
    }
    return local_results;
});

// Bad - shared results with lock
std::mutex mtx;
std::vector<Result> shared_results;
pool.submit([&]() {
    for (auto& item : my_items) {
        std::lock_guard lock(mtx);  // Contention!
        shared_results.push_back(process(item));
    }
});
```

### 3. Monitor Performance

```cpp
auto stats = pool.get_statistics();

if (stats.queue_depth > 1000) {
    // Too many pending tasks, consider adding threads
}

if (stats.idle_threads > stats.total_threads * 0.5) {
    // Many idle threads, workload might be too small
}
```

## Next Steps

- Learn about [Scheduled Tasks](../SCHEDULED_TASKS.md) for advanced scheduling
- Explore [Error Handling](../ERROR_HANDLING.md) for robust error management
- Check [Thread Registry](../REGISTRY.md) for process-wide thread management
- Review the [API Reference](../api/ThreadSchedule/index.md) for complete details
