# ThreadSchedule Benchmarks Suite

This directory contains comprehensive Google Benchmark-based performance tests for all ThreadSchedule thread pool implementations, with realistic real-world scenarios.

## Overview

The benchmark suite includes **7 comprehensive benchmark suites** covering:

1. **Core ThreadPool Tests** - Basic performance characteristics
2. **High-Throughput Scenarios** - 10k+ tasks/second workloads
3. **Memory & Cache Performance** - Cache efficiency, NUMA, false sharing
4. **Image Processing** - Real-time video processing simulation (your specific use case)
5. **Web Server Scenarios** - HTTP requests, JSON APIs, file uploads
6. **Database Operations** - CRUD, queries, transactions, analytics
7. **Audio/Video Processing** - Encoding, filtering, real-time streaming

## Realistic Workloads

Each benchmark simulates real-world scenarios:

- **Image Processing**: Video frame resampling with producer-consumer patterns
- **Web Server**: JSON API processing, file uploads, real-time streaming
- **Database**: CRUD operations, complex queries, concurrent transactions
- **Audio/Video**: Encoding, filtering, pipeline processing, real-time streaming

## Building Benchmarks

```bash
# Configure with benchmarks enabled
cmake .. -DCMAKE_BUILD_TYPE=Release -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON

# Build all benchmarks
cmake --build .

# Or build specific benchmark suites
cmake --build . --target threadpool_basic_benchmarks
cmake --build . --target web_server_benchmarks
cmake --build . --target database_benchmarks
```

## Running Benchmarks

### Quick Development Tests
```bash
# Run quick benchmarks (0.5s per test) - use the run_benchmarks.sh script
./run_benchmarks.sh --quick

# Or run specific benchmark suites directly
./build/benchmarks/threadpool_basic_benchmarks --benchmark_min_time=0.5s
./build/benchmarks/web_server_benchmarks --benchmark_min_time=0.5s
./build/benchmarks/database_benchmarks --benchmark_min_time=0.5s

# Use cmake targets (only run when explicitly requested - they do NOT run during normal builds)
cmake --build build --target run_quick_benchmarks
```

### Full Performance Analysis
```bash
# Run all core benchmarks (2s per test, 3 repetitions) - use the run_benchmarks.sh script
./run_benchmarks.sh

# Or run specific benchmark suites with custom settings
./build/benchmarks/threadpool_basic_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
./build/benchmarks/web_server_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
./build/benchmarks/database_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3

# Use cmake targets (only run when explicitly requested - they do NOT run during normal builds)
cmake --build build --target run_core_benchmarks
cmake --build build --target run_real_world_benchmarks
cmake --build build --target run_all_benchmarks
```

### Specific Scenario Testing
```bash
# Compare pool performance for your image processing workload
./build/benchmarks/threadpool_resampling_benchmarks --benchmark_filter="BM_Resampling_PoolComparison"

# Test web server performance
./build/benchmarks/web_server_benchmarks --benchmark_filter="BM_WebServer_JSON_API_Processing"

# Test database performance
./build/benchmarks/database_benchmarks --benchmark_filter="BM_Database_CRUD_Operations"

# Test audio/video processing
./build/benchmarks/audio_video_benchmarks --benchmark_filter="BM_RealTime_Streaming_Processing"
```

## Benchmark Categories

### Core Benchmarks (`threadpool_basic_benchmarks`)
- **Basic Pool Comparison**: Direct performance comparison between ThreadPool, FastThreadPool, and HighPerformancePool
- **Scalability Testing**: How performance scales with thread count
- **Workload Types**: Minimal tasks, light CPU work, medium CPU work

### High-Throughput Benchmarks (`threadpool_throughput_benchmarks`)
- **10k+ Tasks/Second**: High-frequency task submission scenarios
- **Scalability Analysis**: Performance vs thread count relationships
- **Contention Testing**: Multiple submitter threads competing for pool access
- **Memory Patterns**: Sequential vs random memory access impact

### Memory Benchmarks (`threadpool_memory_benchmarks`)
- **Cache Efficiency**: Cache-friendly vs cache-unfriendly workloads
- **Memory Allocation**: Dynamic allocation overhead measurement
- **NUMA Awareness**: Multi-socket system performance characteristics
- **False Sharing**: Cache line contention detection and avoidance

### Image Processing Benchmarks (`threadpool_resampling_benchmarks`)
**Your specific use case!**
- **4-Core Optimized**: 1 producer + 3 workers (matches your hardware)
- **Real-Time Video**: 15-30fps processing with frame dropping simulation
- **Queue Management**: Buffer overflow handling and backpressure
- **Pool Comparison**: Which pool type works best for your image workload

### Web Server Benchmarks (`web_server_benchmarks`)
- **JSON API Processing**: Complex JSON construction and parsing
- **File Upload Handling**: Large file processing with hashing
- **Real-Time Streaming**: Live data aggregation and statistics
- **Session Management**: Thread-safe user session handling

### Database Benchmarks (`database_benchmarks`)
- **CRUD Operations**: Create, Read, Update, Delete with realistic data patterns
- **Analytical Queries**: Complex aggregations and joins simulation
- **Concurrent Transactions**: Transaction processing with rollback simulation
- **Mixed Workloads**: Realistic database server load patterns

### Audio/Video Benchmarks (`audio_video_benchmarks`)
- **Audio Encoding**: Psychoacoustic analysis and compression simulation
- **Video Encoding**: Motion estimation and entropy coding simulation
- **Pipeline Processing**: Multi-stage audio/video processing chains
- **Real-Time Streaming**: Live encoding with frame dropping simulation

## Interpreting Results

### Key Metrics

- **items_per_second**: Task processing rate
- **work_steal_ratio**: % of tasks stolen (HighPerformancePool only)
- **cache_efficiency**: Cache performance indicator (1.0 = perfect, 0.1 = poor)
- **latency_ms**: Average task processing latency
- **throughput_mbps**: Data processing rate for I/O bound tasks
- **compression_ratio**: Data compression effectiveness

### Performance Expectations by Pool Type

| Pool Type | Best For | Expected Throughput | Key Advantage |
|-----------|----------|-------------------|---------------|
| **ThreadPool** | Simple, predictable workloads | 50k-500k tasks/sec | Low memory overhead |
| **FastThreadPool** | Medium complexity, consistent load | 100k-1M tasks/sec | Single queue efficiency |
| **HighPerformancePool** | Variable, high-throughput workloads | 500k-2M+ tasks/sec | Work stealing, load balancing |

### Example Output Interpretation

```
BM_ImageProcessing_HighPerformancePool_4Core/512x512/20/threads:4/manual_time    45.2 ms
items_per_second=442.5 work_steal_ratio=12.3% avg_task_time_ms=2.26
```

This shows:
- Processing 20 images of 512x512 resolution
- 4 threads achieving 442 images/second
- Work stealing ratio of 12.3% (healthy)
- Average task time of 2.26ms per image

## Real-World Performance Analysis

### Image Processing (Your Use Case)
- **4-core optimization**: Best performance with 3 worker threads + 1 producer
- **Queue depth impact**: Buffer size of 5-10 frames prevents overflow
- **Pool recommendation**: HighPerformancePool for variable image sizes

### Web Server Performance
- **JSON API**: HighPerformancePool handles concurrent requests best
- **File uploads**: FastThreadPool for consistent upload processing
- **Real-time data**: ThreadPool for low-latency streaming requirements

### Database Performance
- **CRUD operations**: FastThreadPool for consistent transaction processing
- **Analytics**: HighPerformancePool for complex query workloads
- **Mixed loads**: HighPerformancePool for best overall performance

## Performance Tuning Guide

### For Your Image Processing Workload

1. **Optimal Thread Count**: Use 3 worker threads on 4-core systems
2. **Queue Management**: Keep input queue depth < 10 to prevent frame drops
3. **Pool Selection**: HighPerformancePool for best throughput with variable image sizes
4. **Batch Processing**: Submit multiple frames together for better efficiency

### General Optimization Tips

1. **Choose the Right Pool**:
   - ThreadPool: Simple, predictable workloads
   - FastThreadPool: Consistent, medium-complexity tasks
   - HighPerformancePool: Variable, high-throughput scenarios

2. **Thread Count Optimization**:
   - Start with `std::thread::hardware_concurrency() - 1` (reserve one for main thread)
   - Use scalability benchmarks to find the sweet spot
   - Monitor work_steal_ratio (keep < 20% for HighPerformancePool)

3. **Memory Access Patterns**:
   - Prefer sequential memory access over random access
   - Use cache-friendly data structures
   - Avoid false sharing with proper alignment

4. **Task Granularity**:
   - Smaller tasks benefit from work-stealing pools
   - Larger tasks benefit from simpler pool implementations
   - Find the balance that maximizes your specific throughput

## System Requirements

- **CPU**: Multi-core x86_64 or ARM64 (4+ cores recommended)
- **Memory**: 8GB+ for large-scale benchmarks
- **OS**: Linux (tested), Windows (MSVC/MinGW), macOS (limited testing)
- **Compiler**: GCC 12+ or Clang 15+ with C++23 support

## Advanced Usage

### Custom Benchmark Runs
```bash
# Filter by specific test
./threadpool_resampling_benchmarks --benchmark_filter="BM_Resampling_HighPerformancePool_4Core"

# Output detailed results
./web_server_benchmarks --benchmark_counters_tabular=true --benchmark_min_time=3.0

# Generate JSON for analysis
./database_benchmarks --benchmark_format=json --benchmark_out=results.json
```

### Performance Regression Testing
```bash
# Run benchmarks and compare against baseline
./threadpool_basic_benchmarks --benchmark_out=baseline.json
./threadpool_basic_benchmarks --benchmark_out_format=json --benchmark_out=current.json

# Use benchmark_compare.py to analyze differences
python benchmark/tools/compare.py benchmarks baseline.json current.json
```

## Troubleshooting

### Common Issues

**High variance in results:**
```bash
# Disable CPU scaling for consistent measurements
sudo cpupower frequency-set --governor performance

# Use longer benchmark times for stability
./benchmarks --benchmark_min_time=3.0 --benchmark_repetitions=5
```

**NUMA system performance:**
```bash
# Bind to specific NUMA node
numactl -N 0 ./benchmarks

# Check NUMA topology
lstopo
```

**Memory bandwidth limited:**
```bash
# Monitor memory usage during benchmarks
vmstat 1
htop
```

## Contributing

When adding new benchmarks:

1. **Follow Google Benchmark patterns** - Use `benchmark::State&` parameter
2. **Include realistic workloads** - Base on real-world usage patterns
3. **Add comprehensive metrics** - Include throughput, latency, efficiency counters
4. **Document expected performance** - Update this README with usage guidance
5. **Test on multiple platforms** - Ensure benchmarks work on Linux, Windows, ARM64

## Integration with CI/CD

The benchmarks are integrated into the GitHub Actions CI pipeline and run automatically on:

- Linux x86_64 (Ubuntu 22.04/24.04)
- Linux ARM64 (Ubuntu 24.04 ARM)
- Windows (MSVC 2022, MinGW-w64)

See `.github/workflows/` for the complete CI configuration. 
