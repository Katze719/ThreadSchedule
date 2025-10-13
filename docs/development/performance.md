# Performance Guide

This guide provides tips and best practices for optimizing ThreadSchedule performance in your applications.

## Pool Selection

Choose the right thread pool for your workload:

| Workload Type | Recommended Pool | Reason |
|---------------|------------------|--------|
| High throughput (10k+ tasks/sec) | `HighPerformancePool` | Work-stealing optimization |
| Consistent task processing | `FastThreadPool` | Minimal overhead |
| Mixed workload | `ThreadPool` | General purpose |
| Timed/periodic tasks | `ScheduledPool` | Timer-based execution |
| Error-prone operations | `ThreadPoolWithErrors` | Built-in error handling |

## Thread Count Optimization

### CPU-Bound Tasks

For CPU-intensive work, match hardware concurrency:

```cpp
auto num_threads = std::thread::hardware_concurrency();
HighPerformancePool pool(num_threads);
```

### I/O-Bound Tasks

For I/O operations, you can use more threads:

```cpp
auto num_threads = std::thread::hardware_concurrency() * 2;
HighPerformancePool pool(num_threads);
```

### Empirical Testing

Test different configurations for your specific workload:

```cpp
// Test various thread counts
for (int threads = 2; threads <= 16; threads *= 2) {
    HighPerformancePool pool(threads);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Run your workload
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10000; ++i) {
        futures.push_back(pool.submit([i]() { process(i); }));
    }
    
    for (auto& f : futures) {
        f.get();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << threads << " threads: " << duration.count() << "ms\n";
}
```

## Task Batching

### Problem: Too Many Small Tasks

```cpp
// Inefficient: 10,000 tiny tasks
for (int i = 0; i < 10000; ++i) {
    pool.submit([i]() {
        tiny_operation(i);  // 1ms of work
    });
}
```

### Solution: Batch Tasks

```cpp
// Efficient: 100 batched tasks
const int batch_size = 100;
for (int i = 0; i < 10000; i += batch_size) {
    pool.submit([i, batch_size]() {
        for (int j = i; j < i + batch_size; ++j) {
            tiny_operation(j);
        }
    });
}
```

**Result**: ~10x improvement in throughput by reducing task overhead.

## Avoiding Lock Contention

### Problem: Shared State

```cpp
std::mutex mtx;
std::vector<Result> results;

for (int i = 0; i < 1000; ++i) {
    pool.submit([&, i]() {
        auto result = process(i);
        std::lock_guard lock(mtx);  // Contention!
        results.push_back(result);
    });
}
```

### Solution 1: Thread-Local Accumulation

```cpp
std::vector<std::future<std::vector<Result>>> futures;
const int batch_size = 100;

for (int i = 0; i < 1000; i += batch_size) {
    futures.push_back(pool.submit([i, batch_size]() {
        std::vector<Result> local_results;  // No lock needed
        for (int j = i; j < i + batch_size; ++j) {
            local_results.push_back(process(j));
        }
        return local_results;
    }));
}

// Merge results (single-threaded)
std::vector<Result> results;
for (auto& future : futures) {
    auto batch = future.get();
    results.insert(results.end(), batch.begin(), batch.end());
}
```

### Solution 2: Lock-Free Structures

```cpp
#include <atomic>

std::atomic<int> counter{0};

for (int i = 0; i < 1000; ++i) {
    pool.submit([&]() {
        process();
        counter.fetch_add(1, std::memory_order_relaxed);
    });
}
```

## Memory Allocation

### Problem: Frequent Allocations

```cpp
pool.submit([]() {
    std::vector<int> temp(10000);  // Allocation per task
    process(temp);
});
```

### Solution: Reuse Memory

```cpp
// Thread-local storage
thread_local std::vector<int> temp;

pool.submit([]() {
    temp.resize(10000);
    process(temp);
});
```

Or use memory pools:

```cpp
class MemoryPool {
    std::vector<std::unique_ptr<Buffer>> pool_;
    std::mutex mutex_;
    
public:
    BufferPtr acquire() {
        std::lock_guard lock(mutex_);
        if (!pool_.empty()) {
            auto buffer = std::move(pool_.back());
            pool_.pop_back();
            return buffer;
        }
        return std::make_unique<Buffer>();
    }
    
    void release(BufferPtr buffer) {
        std::lock_guard lock(mutex_);
        pool_.push_back(std::move(buffer));
    }
};
```

## CPU Affinity

### Pin Threads to Cores

```cpp
HighPerformancePool pool(4);

// Pin worker threads to specific cores
pool.set_thread_affinity({0, 1, 2, 3});

// Or pin individually
for (int i = 0; i < 4; ++i) {
    pool.get_thread(i).set_affinity({i});
}
```

**Benefits**:
- Better cache locality
- Reduced context switching
- Predictable performance

### NUMA Awareness

On NUMA systems, keep memory and threads on the same node:

```cpp
// Allocate memory on specific NUMA node
void* memory = numa_alloc_onnode(size, node);

// Pin threads to same NUMA node
pool.set_thread_affinity(get_cpus_for_node(node));
```

## Priority Management

### High-Priority Tasks

```cpp
HighPerformancePool high_priority_pool(2);
high_priority_pool.set_thread_priority(HIGH_PRIORITY);

ThreadPool normal_pool(4);

// Submit critical tasks to high-priority pool
high_priority_pool.submit(critical_task);

// Submit normal tasks to normal pool
normal_pool.submit(normal_task);
```

### Real-Time Scheduling (Linux)

```cpp
#ifdef __linux__
PThreadWrapper worker("RTWorker", []() {
    // Real-time work
});

sched_param param{};
param.sched_priority = 50;
worker.set_scheduling_policy(SCHED_FIFO, param);
#endif
```

## Work Distribution

### Load Balancing

```cpp
// Good: Equal distribution
std::vector<std::vector<Item>> batches = create_equal_batches(items, num_threads);
for (auto& batch : batches) {
    pool.submit([batch]() {
        for (auto& item : batch) {
            process(item);
        }
    });
}

// Bad: Unequal distribution
// First thread gets all large items, others get small items
```

### Dynamic Work Stealing

`HighPerformancePool` automatically balances work:

```cpp
HighPerformancePool pool(4);  // Work-stealing enabled

// Submit variable-sized tasks
for (auto& item : items) {
    pool.submit([item]() {
        process(item);  // Variable execution time
    });
}

// Pool automatically redistributes work
```

## Benchmarking Your Application

### Basic Timing

```cpp
#include <chrono>

auto start = std::chrono::high_resolution_clock::now();

// Your workload
process_with_threadpool();

auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

std::cout << "Processing took: " << duration.count() << "ms\n";
```

### Pool Statistics

```cpp
HighPerformancePool pool(4);

// Run workload
process();

// Get statistics
auto stats = pool.get_statistics();
std::cout << "Tasks completed: " << stats.completed_tasks << "\n";
std::cout << "Tasks pending: " << stats.pending_tasks << "\n";
std::cout << "Average wait time: " << stats.avg_wait_time.count() << "ms\n";
std::cout << "Throughput: " << stats.tasks_per_second << " tasks/sec\n";
```

### Profiling

Use system profilers:

```bash
# Linux: perf
perf record -g ./your_app
perf report

# Linux: valgrind callgrind
valgrind --tool=callgrind ./your_app
kcachegrind callgrind.out.*

# Windows: Visual Studio Profiler
# macOS: Instruments
```

## Performance Anti-Patterns

### 1. Creating Pools in Loops

```cpp
// BAD: Creates new pool each iteration
for (auto& batch : batches) {
    HighPerformancePool pool(4);  // Expensive!
    pool.submit([batch]() { process(batch); });
}

// GOOD: Reuse pool
HighPerformancePool pool(4);
for (auto& batch : batches) {
    pool.submit([batch]() { process(batch); });
}
```

### 2. Excessive Synchronization

```cpp
// BAD: Lock for every operation
std::mutex mtx;
for (int i = 0; i < 1000; ++i) {
    pool.submit([&, i]() {
        std::lock_guard lock(mtx);
        process(i);
    });
}

// GOOD: Minimize lock scope
for (int i = 0; i < 1000; ++i) {
    pool.submit([i]() {
        auto result = process(i);  // No lock
        // Only lock when storing result
        {
            std::lock_guard lock(mtx);
            results[i] = result;
        }
    });
}
```

### 3. Ignoring False Sharing

```cpp
// BAD: False sharing
struct Counters {
    std::atomic<int> count1;  // Same cache line
    std::atomic<int> count2;  // Same cache line
};

// GOOD: Cache line padding
struct alignas(64) Counters {
    std::atomic<int> count1;
    char padding[64 - sizeof(std::atomic<int>)];
    std::atomic<int> count2;
};
```

### 4. Too Fine-Grained Tasks

```cpp
// BAD: Overhead > Work
for (int i = 0; i < 1000000; ++i) {
    pool.submit([i]() {
        result[i] = i * 2;  // Tiny task
    });
}

// GOOD: Batch tasks
const int batch = 10000;
for (int i = 0; i < 1000000; i += batch) {
    pool.submit([i, batch]() {
        for (int j = i; j < i + batch; ++j) {
            result[j] = j * 2;
        }
    });
}
```

## Real-World Optimizations

### Image Processing Pipeline

```cpp
HighPerformancePool pool(std::thread::hardware_concurrency());
pool.configure_threads("ImageWorker");
pool.set_thread_priority(HIGH_PRIORITY);

// Batch images for better throughput
const int batch_size = 10;
for (size_t i = 0; i < images.size(); i += batch_size) {
    auto batch_end = std::min(i + batch_size, images.size());
    
    pool.submit([&images, i, batch_end]() {
        for (size_t j = i; j < batch_end; ++j) {
            process_image(images[j]);
        }
    });
}
```

### Database Operations

```cpp
// Use connection pool pattern
class DatabasePool {
    HighPerformancePool thread_pool_;
    std::vector<DatabaseConnection> connections_;
    
public:
    template<typename Func>
    auto execute(Func&& query) {
        return thread_pool_.submit([this, query = std::forward<Func>(query)]() {
            auto conn = acquire_connection();
            auto result = query(conn);
            release_connection(conn);
            return result;
        });
    }
};
```

### Web Server Request Handling

```cpp
FastThreadPool pool(64);  // Many short-lived requests

void handle_request(Request req) {
    pool.submit([req = std::move(req)]() mutable {
        auto response = process_request(req);
        send_response(response);
    });
}
```

## Monitoring and Tuning

### Runtime Monitoring

```cpp
class PoolMonitor {
    HighPerformancePool& pool_;
    std::thread monitor_thread_;
    std::atomic<bool> running_{true};
    
public:
    PoolMonitor(HighPerformancePool& pool) : pool_(pool) {
        monitor_thread_ = std::thread([this]() {
            while (running_) {
                auto stats = pool_.get_statistics();
                log_metrics(stats);
                
                if (stats.queue_depth > 1000) {
                    alert("High queue depth: " + std::to_string(stats.queue_depth));
                }
                
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }
    
    ~PoolMonitor() {
        running_ = false;
        monitor_thread_.join();
    }
};
```

## Summary

**Key Performance Tips**:

1. Choose the right pool type for your workload
2. Match thread count to hardware and workload characteristics
3. Batch small tasks to reduce overhead
4. Avoid lock contention with thread-local storage
5. Pin threads to cores for consistent performance
6. Profile and measure before optimizing
7. Reuse thread pools instead of creating new ones
8. Monitor pool statistics in production

## Next Steps

- Run [benchmarks](../../benchmarks/README.md) to understand performance characteristics
- Review [thread pool documentation](../user-guide/thread-pools.md) for advanced usage
- Check the [API reference](../api/ThreadSchedule/index.md) for detailed information
