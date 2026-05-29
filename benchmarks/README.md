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

# Generate an HTML report with graphs + speedups for comparison benchmarks
./run_benchmark_graphs.sh
./run_benchmark_graphs.sh --quick

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

# Turn one or more Google Benchmark JSON files into a local HTML report
python3 benchmarks/generate_benchmark_report.py \
  --output build/benchmark-report.html \
  --title "Local benchmark comparison" \
  build/benchmarks/threadpool_comparisons.json \
  build/benchmarks/reflection_registry.json
```

## Graphs and Speedups

The repository now includes a local report generator that turns Google
Benchmark JSON output into a standalone HTML report with:

- Absolute timing bar charts
- Relative speedup annotations (for known comparison families)
- Automatically collected machine information
- Side-by-side tables for comparison-oriented benchmark groups

The current heuristics explicitly understand:

- `BM_ComparePoolTypes_LightWorkload`
- `BM_PostVsSubmit`
- `BM_QueryView_FilterMapName`
- `BM_QueryView_ReflectionWhereProjectName`
- `BM_QueryView_FindIf`
- `BM_QueryView_ReflectionFindBy`

This is enough to visualize both classic pool comparisons and the new
reflection registry speedups without extra dependencies such as matplotlib.

### Standalone SVG charts for the README

`generate_readme_graphs.py` turns the same Google Benchmark JSON into a few
self-contained SVG files (light background, dark text) that embed cleanly into
Markdown and render in both light and dark GitHub themes:

```bash
# Produce JSON from the comparison benchmarks
./build/benchmarks/threadpool_basic_benchmarks \
  --benchmark_filter="BM_ComparePoolTypes_LightWorkload|BM_ComparePoolWorkload|BM_PostVsSubmit" \
  --benchmark_format=json \
  --benchmark_out=build/threadpool_comparisons.json

# Optional: reflection query benchmarks (needs a C++26 + reflection build)
./build-reflection/benchmarks/reflection_registry_benchmarks \
  --benchmark_filter="BM_QueryView_.*" \
  --benchmark_format=json \
  --benchmark_out=build/reflection_registry.json

# Render the README charts (no matplotlib required)
python3 benchmarks/generate_readme_graphs.py \
  --output-dir docs/benchmarks \
  build/threadpool_comparisons.json \
  build/reflection_registry.json
```

The generator accepts any number of JSON files and emits the charts it can build
from the data it finds:

| SVG file                          | Source benchmark                                   |
| --------------------------------- | -------------------------------------------------- |
| `pool_throughput.svg`             | `BM_ComparePoolTypes_LightWorkload`                |
| `pool_comparison.svg`             | `BM_ComparePoolTypes_LightWorkload`                |
| `pool_workload.svg`               | `BM_ComparePoolWorkload`                           |
| `post_vs_submit.svg`              | `BM_PostVsSubmit`                                  |
| `reflection_query.svg`            | `BM_QueryView_FilterMapName` vs `...WhereProject`  |
| `reflection_lookup.svg`           | `BM_QueryView_FindIf` vs `BM_QueryView_ReflectionFindBy` |
| `callable_standards.svg`          | `callable_std_benchmarks` (`BM_MoveCallable_*`, one JSON per standard) |
| `callable_sbo.svg`                | `callable_std_benchmarks` (`BM_MoveCallable_*` vs `BM_Sbo_*`) |

All files are written into `docs/benchmarks/` and referenced from the top-level
`README.md`. The reflection charts require building `reflection_registry_benchmarks`,
which is only available on GCC 16.1+ with `-DCMAKE_CXX_STANDARD=26 -DTHREADSCHEDULE_ENABLE_REFLECTION=ON`:

```bash
cmake -S . -B build-reflection -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=26 -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON \
  -DTHREADSCHEDULE_ENABLE_REFLECTION=ON
cmake --build build-reflection --target reflection_registry_benchmarks
```

#### Cross-standard callable charts

`callable_std_benchmarks` isolates the cost of ThreadSchedule's task storage
(`detail::move_callable`, which is `std::function` on C++17/20 and
`std::move_only_function` on C++23+, versus the `SboCallable` small-buffer
callable used by `LightweightPool`). To compare standards, build the same source
under each one and feed the per-standard JSON (named `callable_cxx<NN>.json`, the
generator reads the standard from the file name) to the generator:

```bash
for std in 17 20 23 26; do
  g++ -std=c++$std -O3 -DNDEBUG -march=native -ffast-math -fno-omit-frame-pointer \
    -Iinclude -Ibuild/_deps/benchmark-src/include \
    benchmarks/callable_std_benchmarks.cpp \
    build/_deps/benchmark-build/src/libbenchmark.a -lpthread -o /tmp/callable_c$std
  /tmp/callable_c$std --benchmark_min_time=0.5s --benchmark_repetitions=3 \
    --benchmark_report_aggregates_only=true --benchmark_format=json \
    --benchmark_out=build/callable_cxx$std.json
done

python3 benchmarks/generate_readme_graphs.py --output-dir docs/benchmarks \
  build/callable_cxx17.json build/callable_cxx20.json \
  build/callable_cxx23.json build/callable_cxx26.json
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
