#!/bin/bash

# ThreadSchedule Benchmark Runner
# Runs comprehensive benchmarks with optimal settings

set -e

# Script is now in project root, benchmarks are in build/benchmarks/
PROJECT_ROOT="$(dirname "$0")"
BUILD_DIR="${PROJECT_ROOT}/build"
BENCHMARK_DIR="${BUILD_DIR}/benchmarks"

echo "ThreadSchedule Comprehensive Benchmark Suite"
echo "============================================="
echo ""

# Check if build directory exists
if [[ ! -d "${BUILD_DIR}" ]]; then
    echo "Error: Build directory not found. Please build first:"
    echo "  mkdir -p build && cd build"
    echo "  cmake .. -DCMAKE_BUILD_TYPE=Release -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON"
    echo "  cmake --build ."
    exit 1
fi

# Check if benchmarks are built
if [[ ! -f "${BENCHMARK_DIR}/threadpool_basic_benchmarks" ]]; then
    echo "Error: Benchmarks not found. Please build first:"
    echo "  cd build && cmake --build . --target threadpool_basic_benchmarks"
    echo "  cd build && cmake --build . --target threadpool_throughput_benchmarks"
    echo "  cd build && cmake --build . --target threadpool_memory_benchmarks"
    exit 1
fi

# System information
echo "System Information:"
echo "  CPU cores: $(nproc)"
echo "  CPU model: $(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)"
if command -v cpupower >/dev/null 2>&1; then
    governor=$(cpupower frequency-info -p 2>/dev/null | grep -o 'performance\|powersave\|ondemand' || echo "unknown")
    echo "  CPU governor: $governor"
fi
echo ""

# Run benchmark suites
echo "1. Running Basic Benchmarks..."
echo "==============================="
"${BENCHMARK_DIR}/threadpool_basic_benchmarks" \
    --benchmark_min_time=1.0s \
    --benchmark_counters_tabular=true \
    --benchmark_filter="BM_ComparePoolTypes.*"

echo ""
echo "2. Running Throughput Benchmarks..."  
echo "===================================="
"${BENCHMARK_DIR}/threadpool_throughput_benchmarks" \
    --benchmark_min_time=1.0s \
    --benchmark_counters_tabular=true \
    --benchmark_filter="BM_HighThroughput.*"

echo ""
echo "3. Running Memory/Cache Benchmarks..."
echo "======================================"
"${BENCHMARK_DIR}/threadpool_memory_benchmarks" \
    --benchmark_min_time=1.0s \
    --benchmark_counters_tabular=true \
    --benchmark_filter="BM_CacheFriendly.*|BM_CacheUnfriendly.*"

echo ""
echo "Benchmark Suite Complete!"
echo "========================="
echo ""
echo "Key Findings:"
echo "- For quick tasks, HighPerformancePool typically performs best"  
echo "- Work stealing ratio should be < 20% for optimal performance"
echo "- Cache-friendly access patterns show 3-10x better performance"
echo ""
echo "To run specific benchmarks:"
echo "  ./build/benchmarks/threadpool_basic_benchmarks --benchmark_filter=\".*HighPerformancePool.*\""
echo "  ./build/benchmarks/threadpool_throughput_benchmarks --benchmark_filter=\".*Scalability.*\""  
echo "  ./build/benchmarks/threadpool_memory_benchmarks --benchmark_filter=\".*NUMA.*\"" 
