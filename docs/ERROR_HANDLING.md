# Error Handling

ThreadSchedule provides comprehensive error handling for asynchronous tasks with support for error callbacks and better exception management.

## Features

- ✅ **Global error callbacks** - Handle all exceptions in one place
- ✅ **Per-future error callbacks** - Handle specific task errors
- ✅ **Error context** - Get detailed information about errors (task description, thread ID, timestamp)
- ✅ **Thread-safe** - Error handlers work correctly across threads
- ✅ **Non-intrusive** - Original thread pools remain unchanged

## Quick Start

```cpp
#include <threadschedule/threadschedule.hpp>
using namespace threadschedule;

int main() {
    HighPerformancePoolWithErrors pool(4);
    
    // Add global error callback
    pool.add_error_callback([](const TaskError& error) {
        std::cerr << "Task error: " << error.what() << "\n";
    });
    
    // Submit task that might throw
    auto future = pool.submit([]() {
        throw std::runtime_error("Something failed");
        return 42;
    });
    
    try {
        future.get();
    } catch (const std::exception& e) {
        // Exception still propagates, but callback was called
    }
}
```

## Thread Pool Variants

### HighPerformancePoolWithErrors

High-performance work-stealing pool with error handling.

```cpp
HighPerformancePoolWithErrors pool(4);

pool.add_error_callback([](const TaskError& error) {
    log_error(error.what());
});

auto future = pool.submit([]() {
    // Task that might throw
});
```

### FastThreadPoolWithErrors

Single-queue pool with error handling.

```cpp
FastThreadPoolWithErrors pool(4);

pool.add_error_callback([](const TaskError& error) {
    std::cerr << error.what() << "\n";
});
```

### ThreadPoolWithErrors

Simple general-purpose pool with error handling.

```cpp
ThreadPoolWithErrors pool(4);

pool.add_error_callback([](const TaskError& error) {
    handle_error(error);
});
```

## Error Callbacks

### Global Error Callbacks

Global callbacks are invoked for every task that throws an exception:

```cpp
pool.add_error_callback([](const TaskError& error) {
    std::cerr << "[ERROR] " << error.what() << "\n";
    std::cerr << "Thread: " << error.thread_id << "\n";
    std::cerr << "Time: " << error.timestamp.time_since_epoch().count() << "\n";
    
    if (!error.task_description.empty()) {
        std::cerr << "Task: " << error.task_description << "\n";
    }
});
```

### Per-Future Error Callbacks

Handle errors for specific tasks:

```cpp
auto future = pool.submit(risky_task)
    .on_error([](std::exception_ptr eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::runtime_error& e) {
            std::cerr << "Runtime error: " << e.what() << "\n";
        } catch (const std::logic_error& e) {
            std::cerr << "Logic error: " << e.what() << "\n";
        }
    });

try {
    future.get();
} catch (...) {
    // Exception propagates after callback
}
```

## Task Descriptions

Add descriptions to tasks for better error messages:

```cpp
auto future = pool.submit_with_description(
    "Database Query",
    []() {
        return query_database();
    }
);
```

Error output will include:
```
[ERROR] Connection timeout
Thread: 139745814427328
Task: Database Query
```

## TaskError Structure

```cpp
struct TaskError {
    std::exception_ptr exception;           // The caught exception
    std::string task_description;           // Optional task description
    std::thread::id thread_id;              // Thread that caught the exception
    std::chrono::steady_clock::time_point timestamp;  // When exception occurred
    
    // Get exception message (if std::exception)
    [[nodiscard]] auto what() const -> std::string;
    
    // Rethrow the exception
    void rethrow() const;
};
```

## FutureWithErrorHandler

Extended future with error callback support:

```cpp
template <typename T>
class FutureWithErrorHandler {
    // Attach error callback
    auto on_error(std::function<void(std::exception_ptr)> callback) 
        -> FutureWithErrorHandler&;
    
    // Get result (calls error callback on exception)
    auto get() -> T;
    
    // Standard future methods
    void wait() const;
    template<typename Rep, typename Period>
    auto wait_for(std::chrono::duration<Rep, Period> const&) const;
    template<typename Clock, typename Duration>
    auto wait_until(std::chrono::time_point<Clock, Duration> const&) const;
    [[nodiscard]] auto valid() const -> bool;
};
```

## Error Statistics

Track error counts:

```cpp
std::cout << "Errors: " << pool.error_count() << "\n";

// Reset counter
pool.reset_error_count();

// Clear all callbacks
pool.clear_error_callbacks();
```

## Complete Example

```cpp
#include <threadschedule/threadschedule.hpp>
#include <iostream>
#include <fstream>

using namespace threadschedule;

class ErrorLogger {
public:
    void log(const TaskError& error) {
        std::ofstream file("errors.log", std::ios::app);
        file << "[" << error.timestamp.time_since_epoch().count() << "] ";
        file << error.what();
        if (!error.task_description.empty()) {
            file << " (Task: " << error.task_description << ")";
        }
        file << "\n";
    }
};

int main() {
    HighPerformancePoolWithErrors pool(4);
    ErrorLogger logger;
    
    // Global error handler
    pool.add_error_callback([&logger](const TaskError& error) {
        logger.log(error);
        std::cerr << "Error logged: " << error.what() << "\n";
    });
    
    // Submit various tasks
    std::vector<FutureWithErrorHandler<int>> futures;
    
    for (int i = 0; i < 10; i++) {
        futures.push_back(
            pool.submit_with_description(
                "Task " + std::to_string(i),
                [i]() {
                    if (i % 3 == 0) {
                        throw std::runtime_error("Task " + std::to_string(i) + " failed");
                    }
                    return i * 10;
                }
            )
        );
    }
    
    // Collect results
    int successful = 0;
    int failed = 0;
    
    for (auto& future : futures) {
        try {
            int result = future.get();
            std::cout << "Success: " << result << "\n";
            successful++;
        } catch (const std::exception& e) {
            std::cerr << "Failed: " << e.what() << "\n";
            failed++;
        }
    }
    
    std::cout << "\n=== Summary ===\n";
    std::cout << "Successful: " << successful << "\n";
    std::cout << "Failed: " << failed << "\n";
    std::cout << "Total errors: " << pool.error_count() << "\n";
    
    pool.shutdown();
    return 0;
}
```

## Advanced Usage

### Custom Error Handler

```cpp
class CustomErrorHandler : public ErrorHandler {
public:
    void log_to_syslog(const TaskError& error) {
        // Log to system logger
    }
    
    void send_alert(const TaskError& error) {
        // Send alert to monitoring system
    }
};

auto handler = std::make_shared<CustomErrorHandler>();
handler->add_callback([&handler](const TaskError& error) {
    handler->log_to_syslog(error);
    
    // Send alert for critical errors
    if (error.what().find("CRITICAL") != std::string::npos) {
        handler->send_alert(error);
    }
});
```

### Error Classification

```cpp
pool.add_error_callback([](const TaskError& error) {
    try {
        error.rethrow();
    } catch (const std::system_error& e) {
        handle_system_error(e);
    } catch (const std::runtime_error& e) {
        handle_runtime_error(e);
    } catch (const std::logic_error& e) {
        handle_logic_error(e);
    } catch (...) {
        handle_unknown_error();
    }
});
```

### Retry Logic

```cpp
template <typename F>
auto retry_on_error(HighPerformancePoolWithErrors& pool, F&& func, int max_retries = 3) {
    for (int attempt = 0; attempt < max_retries; attempt++) {
        try {
            return pool.submit(std::forward<F>(func)).get();
        } catch (const std::exception& e) {
            if (attempt == max_retries - 1) {
                throw;  // Last attempt failed
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
        }
    }
    throw std::runtime_error("All retries exhausted");
}
```

## Best Practices

1. **Always add error callbacks** - Don't let exceptions go unnoticed
2. **Use task descriptions** - Make debugging easier
3. **Log errors** - Keep track of what went wrong
4. **Handle exceptions in `.get()`** - Callbacks don't prevent exception propagation
5. **Clean up callbacks** - Call `clear_error_callbacks()` when done

## See Also

- [Scheduled Tasks](SCHEDULED_TASKS.md) - Scheduling tasks with error handling
- [Thread Pools](../README.md#thread-pools) - Original thread pool documentation

