# Benchmarks

The benchmark suite covers the canonical pool, the advanced pool backends,
submission overhead, throughput, memory behavior, resampling, and representative
web/database/audio-video workloads.

## Build

```bash
cmake -S . -B build-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=17 \
  -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON \
  -DTHREADSCHEDULE_BUILD_DOCS=OFF
cmake --build build-bench --parallel
```

Benchmarks intentionally use optimized native code and should be run on an idle
machine with a fixed CPU governor when comparing changes.

## Run

```bash
cmake --build build-bench --target run_quick_benchmarks
cmake --build build-bench --target run_core_benchmarks
cmake --build build-bench --target run_real_world_benchmarks
cmake --build build-bench --target run_all_benchmarks
```

Individual Google Benchmark executables can be filtered normally:

```bash
./build-bench/benchmarks/threadpool_basic_benchmarks \
  --benchmark_filter='BM_ComparePoolTypes.*'
```

`callable_std_benchmarks` may be compiled under C++17/20/23/26 to detect
compiler and standard-library optimization differences. ThreadSchedule's public
callable representation itself remains the same C++17 type in every mode.

The graph scripts consume Google Benchmark JSON and write SVGs under
`docs/benchmarks/`. Reflection graphs from pre-3.0 releases remain historical
artifacts and are no longer generated.
