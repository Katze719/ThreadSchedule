# ThreadSchedule Benchmarks

This directory contains comprehensive Google Benchmark-based performance tests for all ThreadSchedule thread pool implementations.

## Overview

The benchmark suite tests three thread pool implementations:

1. **ThreadPool** - Simple general-purpose pool (< 1000 tasks/second)
2. **FastThreadPool** - Single queue with optimized locking
3. **HighPerformancePool** - Work-stealing architecture (10k+ tasks/second)

## Building Benchmarks

```bash
# Configure with benchmarks enabled (default)
cmake .. -DCMAKE_BUILD_TYPE=Release -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON

# Build all benchmarks
cmake --build .

# Or build specific benchmark suites
cmake --build . --target threadpool_basic_benchmarks
cmake --build . --target threadpool_throughput_benchmarks  
cmake --build . --target threadpool_memory_benchmarks
```

## Running Benchmarks

### Quick Performance Test
```bash
# Run basic benchmarks with short duration (for quick testing)
./benchmarks/threadpool_basic_benchmarks --benchmark_min_time=0.1s

# Run high-throughput benchmarks
./benchmarks/threadpool_throughput_benchmarks --benchmark_min_time=0.1s

# Run memory and cache benchmarks  
./benchmarks/threadpool_memory_benchmarks --benchmark_min_time=0.1s
```

### Full Benchmark Suite
```bash
# Run all benchmarks with default timing (more accurate, takes longer)
./benchmarks/threadpool_basic_benchmarks
./benchmarks/threadpool_throughput_benchmarks
./benchmarks/threadpool_memory_benchmarks

# Or use the custom target
make run_all_benchmarks
```

### Filtering Benchmarks
```bash
# Run only HighPerformancePool benchmarks
./benchmarks/threadpool_basic_benchmarks --benchmark_filter=".*HighPerformancePool.*"

# Run only specific thread counts
./benchmarks/threadpool_basic_benchmarks --benchmark_filter=".*/(4|8)/.*"

# Run comparison benchmarks only
./benchmarks/threadpool_basic_benchmarks --benchmark_filter="BM_ComparePoolTypes.*"
```

### Output Formats
```bash
# JSON output for analysis
./benchmarks/threadpool_basic_benchmarks --benchmark_format=json --benchmark_out=results.json

# CSV output  
./benchmarks/threadpool_basic_benchmarks --benchmark_format=csv --benchmark_out=results.csv

# Console output with more detail
./benchmarks/threadpool_basic_benchmarks --benchmark_counters_tabular=true
```

## Benchmark Categories

### Basic Benchmarks (`threadpool_basic_benchmarks`)
- **Minimal Tasks**: Pure overhead measurement with no-op tasks
- **Light Tasks**: CPU work simulation (web service style)
- **Pool Comparison**: Direct comparison between all three pool types

### Throughput Benchmarks (`threadpool_throughput_benchmarks`)  
- **High Throughput**: 10k+ tasks/second scenarios
- **Scalability**: Performance vs thread count relationships
- **Contention**: Multiple submitter thread scenarios
- **Memory Access**: Sequential vs random memory patterns

### Memory Benchmarks (`threadpool_memory_benchmarks`)
- **Cache Efficiency**: Cache-friendly vs cache-unfriendly workloads
- **Memory Allocation**: Dynamic allocation overhead
- **NUMA Awareness**: Performance on multi-socket systems
- **False Sharing**: Cache line contention avoidance

## Interpreting Results

### Key Metrics

- **Time**: Time per benchmark iteration
- **CPU**: CPU time used
- **Iterations**: Number of times benchmark was run
- **items_per_second**: Task processing rate
- **work_steal_ratio**: % of tasks stolen (HighPerformancePool only)
- **cache_efficiency**: Cache performance indicator (1.0 = perfect, 0.1 = poor)

### Performance Expectations

**ThreadPool (Simple)**
- Best for: < 1000 tasks/second, simple use cases
- Expected: 50k-500k tasks/second depending on task complexity
- Advantages: Low memory overhead, simple debugging

**FastThreadPool**  
- Best for: Medium load with predictable patterns
- Expected: 100k-1M tasks/second  
- Advantages: Consistent performance, lower complexity than work-stealing

**HighPerformancePool**
- Best for: 10k+ tasks/second, variable workloads
- Expected: 500k-2M+ tasks/second
- Advantages: Work stealing, excellent load balancing
- Monitor: Keep work_steal_ratio < 20% for optimal performance

### Example Output Interpretation

```
BM_HighPerformancePool_MinimalTasks/8/10000/manual_time    2.45 ms    8.12 ms    285 
items_per_second=4.08M/s work_steal_ratio=15.2% threads=8 tasks=10000
```

This shows:
- 8 threads processing 10,000 minimal tasks
- Total time: 2.45ms (wall clock)
- CPU time: 8.12ms (sum of all threads)  
- Rate: 4.08 million tasks/second
- Work stealing: 15.2% (healthy level)

## Performance Tuning Tips

### Based on Benchmark Results

1. **Choose the Right Pool**:
   - Use comparison benchmarks to select optimal pool for your workload
   - ThreadPool for simple cases, HighPerformancePool for high throughput

2. **Optimize Thread Count**:
   - Run scalability benchmarks to find optimal thread count
   - Usually `hardware_concurrency()` is optimal, but measure to confirm

3. **Monitor Work Stealing**:
   - In HighPerformancePool, keep work_steal_ratio < 20%
   - High ratios indicate load imbalance - consider smaller tasks

4. **Memory Access Patterns**:
   - Use memory benchmarks to understand cache impact
   - Sequential access is typically 3-10x faster than random

5. **Batch Processing**:
   - For many small tasks, batch submission is significantly faster
   - Use `submit_batch()` instead of individual `submit()` calls

## System Requirements

- **CPU**: Multi-core x86_64 or ARM64
- **Memory**: 4GB+ for large benchmarks  
- **OS**: Linux (tested), other POSIX systems should work
- **Compiler**: GCC 12+ or Clang 15+ with C++23 support

## Troubleshooting

### CPU Scaling Warning
If you see "CPU scaling is enabled" warnings:
```bash
# Disable CPU scaling for more consistent results (requires root)
sudo cpupower frequency-set --governor performance
```

### NUMA Systems
For multi-socket systems, consider:
- Running benchmarks with `numactl` for consistent placement
- Monitoring NUMA effects with `lstopo` or `numastat`

### High Load Systems
On busy systems:
- Run benchmarks in isolation
- Consider `nice -n -10` for higher priority
- Use `taskset` to isolate to specific cores 
