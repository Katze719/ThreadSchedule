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

## Comparing thread costs

The standalone thread runners use the selected C++ standard (C++17 minimum)
and CMake build-type flags. Unlike the pool benchmark targets, they do not
force C++23 or `-march=native`. Use a Release build for published measurements:

```bash
CCACHE_DISABLE=1 cmake -S . -B build-thread-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=17 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON
CCACHE_DISABLE=1 cmake --build build-thread-bench \
  --target thread_benchmarks thread_allocation_benchmarks --parallel 2
./build-thread-bench/benchmarks/thread_benchmarks \
  --output build-thread-bench/thread-timing.json
./build-thread-bench/benchmarks/thread_allocation_benchmarks \
  --output build-thread-bench/thread-allocations.json
python3 benchmarks/generate_thread_report.py \
  --timing build-thread-bench/thread-timing.json \
  --allocations build-thread-bench/thread-allocations.json \
  --summary docs/benchmarks/thread_costs.json \
  --readme README.md
```

Run the two measurements sequentially after builds and tests have finished.
Do not compare a Debug or instrumented allocation build with Release timings.
No runner changes CPU affinity, scheduling priority, or the frequency governor.
System load, frequency scaling and OS scheduling can affect the results.
`run_thread_benchmarks` is an explicit convenience target that runs both
programs sequentially and writes their JSON under the build's `benchmarks/`
directory; `run_all_benchmarks` also includes it.

Both runners compare direct `std::thread`, direct `threadschedule::thread`,
and `thread::create()` with and without the name `ts-bench`. The configured
standard thread names itself via `this_thread::set_name()` inside its callable.
The factory path includes error checking and moving into an empty owner.
No registry, affinity or priority changes participate in the measurements.

The timing runner records four independent elapsed times from immediately
before creation: return/ownership transfer, the first timestamp in user code,
readiness after configuration, and completion of an explicit join. Entry can
precede constructor return. The benchmark adds no startup gate; it reads
worker timestamps only after joining. Clock calls themselves are included in
the measurement and are not subtracted.

Running-work measurements use the same out-of-line unsigned-integer kernel
for every variant. Each worker warms up the kernel before timing it; startup,
configuration and join are excluded. Checksums are verified for every sample.
Auto-calibration doubles the common iteration count until a standard-thread
sample takes at least 5 ms. This is one compute workload, not a claim about
every possible application.

Defaults are 200 warmup rounds and 2,000 startup samples per variant, with
rotating scenario order, plus 30 running-work samples. Override them with
`--warmup`, `--samples`, `--work-samples`, and `--work-iterations` (zero selects
auto-calibration). The allocation runner accepts the first two count options.
Statistics use the ordinary median and nearest-rank p95. Ratios compare
medians with the standard-thread baseline in the same configuration group.

`thread_allocation_benchmarks` alone replaces the C++17 allocation/deallocation
functions. Its counting window spans creation through join, including worker
exit and thread-state destruction, and uses the exact same one-pointer startup
callable as the timing runner. The allocation count and requested bytes are
cumulative `new` requests, not peak or retained storage. OS thread stacks,
allocator bookkeeping and direct C-library/OS allocations are excluded.
`sizeof`/`alignof` describe only each owning or view object, not its heap state.

JSON includes raw samples, compiler/standard-library/build metadata, and the
measurement host and timestamp. The report script validates the two inputs,
keeps the recorded host information, removes raw samples from the versioned
summary, and replaces only the marked README region. `--markdown PATH` also
writes a separate report. Failed scenarios remain visible and produce no
success statistics or ratios. Reports from incompatible builds/hosts are
rejected. No performance thresholds are enforced.

For a quick correctness check, both programs accept `--self-test`. Configure
with `THREADSCHEDULE_BUILD_TESTS=ON` to register self-tests, short smoke runs,
and report tests:

```bash
ctest --test-dir build-thread-bench --output-on-failure \
  -R '^Thread(Benchmark|Allocation)'
python3 benchmarks/test_thread_report.py
```

The thread benchmark CI runs these checks under C++17 and C++20 on Linux,
MSVC and MinGW. Its smoke timings are not published as performance results.
