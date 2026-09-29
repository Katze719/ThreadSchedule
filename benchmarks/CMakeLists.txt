# ThreadSchedule Benchmarks Suite
# Comprehensive benchmarking for all thread pool implementations with real-world scenarios

# Thread cost measurements keep the selected C++ standard (C++17 minimum) and
# ordinary build-type flags. Allocation interception is linked only into its
# separate executable, never into the timing benchmark.
set(thread_benchmark_flags "${CMAKE_CXX_FLAGS}")
foreach(configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(APPEND thread_benchmark_flags
        " $<$<CONFIG:${configuration}>:${CMAKE_CXX_FLAGS_${configuration}}>")
endforeach()
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/thread-benchmark-$<CONFIG>/thread_benchmark_build.hpp"
    CONTENT "#pragma once\n#define THREAD_BENCH_BUILD_TYPE \"$<CONFIG>\"\n#define THREAD_BENCH_COMPILER_FLAGS R\"tsflags(${thread_benchmark_flags})tsflags\"\n"
)

add_executable(thread_benchmarks thread_benchmarks.cpp)
add_executable(thread_allocation_benchmarks thread_allocation_benchmarks.cpp)
foreach(target thread_benchmarks thread_allocation_benchmarks)
    target_link_libraries(${target} PRIVATE ThreadSchedule::ThreadSchedule nlohmann_json::nlohmann_json)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/thread-benchmark-$<CONFIG>")
endforeach()

add_custom_target(run_thread_benchmarks
    COMMAND thread_benchmarks --output "${CMAKE_CURRENT_BINARY_DIR}/thread-timing.json"
    COMMAND thread_allocation_benchmarks --output "${CMAKE_CURRENT_BINARY_DIR}/thread-allocations.json"
    DEPENDS thread_benchmarks thread_allocation_benchmarks
    COMMENT "Measuring thread startup, running work, and allocations sequentially"
)

if(THREADSCHEDULE_BUILD_TESTS)
    add_test(NAME ThreadBenchmarkSelfTest COMMAND thread_benchmarks --self-test)
    add_test(NAME ThreadAllocationSelfTest COMMAND thread_allocation_benchmarks --self-test)
    add_test(NAME ThreadBenchmarkSmoke COMMAND thread_benchmarks
             --warmup 2 --samples 8 --work-samples 3 --work-iterations 10000)
    add_test(NAME ThreadAllocationSmoke COMMAND thread_allocation_benchmarks --warmup 2 --samples 8)
    set_tests_properties(ThreadBenchmarkSelfTest ThreadAllocationSelfTest ThreadBenchmarkSmoke ThreadAllocationSmoke
                         PROPERTIES TIMEOUT 60)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ThreadBenchmarkReportTests
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_thread_report.py)
    endif()
endif()

# Core thread pool benchmarks
add_executable(threadpool_basic_benchmarks threadpool_benchmarks.cpp)
add_executable(threadpool_throughput_benchmarks throughput_benchmarks.cpp)
add_executable(threadpool_memory_benchmarks memory_benchmarks.cpp)
add_executable(threadpool_resampling_benchmarks resampling_benchmarks.cpp)
add_executable(callable_benchmarks callable_benchmarks.cpp)
add_executable(callable_std_benchmarks callable_std_benchmarks.cpp)

# Real-world scenario benchmarks
add_executable(web_server_benchmarks web_server_benchmarks.cpp)
add_executable(database_benchmarks database_benchmarks.cpp)
add_executable(audio_video_benchmarks audio_video_benchmarks.cpp)

# All benchmark executables
set(ALL_BENCHMARK_TARGETS
    threadpool_basic_benchmarks
    threadpool_throughput_benchmarks
    threadpool_memory_benchmarks
    threadpool_resampling_benchmarks
    callable_benchmarks
    callable_std_benchmarks
    web_server_benchmarks
    database_benchmarks
    audio_video_benchmarks
)

# Link libraries for all benchmarks
foreach(target ${ALL_BENCHMARK_TARGETS})
    target_link_libraries(${target}
        PRIVATE
        ThreadSchedule::ThreadSchedule
        benchmark::benchmark
        nlohmann_json::nlohmann_json
    )

    # Set compiler features
    target_compile_features(${target} PRIVATE cxx_std_23)

    # Compiler flags for benchmarks (optimized for performance measurement)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -O3 -DNDEBUG
            -fno-omit-frame-pointer
            -march=native
        )
    elseif(MSVC)
        target_compile_options(${target} PRIVATE
            /O2 /DNDEBUG
            /fp:fast
        )
    endif()
endforeach()

# Register benchmarks with CTest (with longer runtime for accurate measurements)
add_test(NAME ThreadPoolBasicBenchmarks COMMAND threadpool_basic_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)
add_test(NAME ThreadPoolThroughputBenchmarks COMMAND threadpool_throughput_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)
add_test(NAME ThreadPoolMemoryBenchmarks COMMAND threadpool_memory_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)
add_test(NAME ThreadPoolResamplingBenchmarks COMMAND threadpool_resampling_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)
add_test(NAME CallableBenchmarks COMMAND callable_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)
add_test(NAME WebServerBenchmarks COMMAND web_server_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)
add_test(NAME DatabaseBenchmarks COMMAND database_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)
add_test(NAME AudioVideoBenchmarks COMMAND audio_video_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)

# Custom targets for different benchmark categories (these targets execute benchmarks, not just build them)
# Note: These targets only run when explicitly requested. They do NOT run during normal builds.

add_custom_target(run_core_benchmarks
    COMMAND echo "=== Core thread_pool_backend Benchmarks ==="
    COMMAND threadpool_basic_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    COMMAND echo "=== Throughput Benchmarks ==="
    COMMAND threadpool_throughput_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    COMMAND echo "=== Memory Benchmarks ==="
    COMMAND threadpool_memory_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    COMMAND echo "=== Callable Benchmarks ==="
    COMMAND callable_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    DEPENDS threadpool_basic_benchmarks threadpool_throughput_benchmarks threadpool_memory_benchmarks callable_benchmarks
    COMMENT "Running core ThreadSchedule benchmarks (explicit target, not part of normal build)"
)

add_custom_target(run_real_world_benchmarks
    COMMAND echo "=== Image Processing Benchmarks ==="
    COMMAND threadpool_resampling_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    COMMAND echo "=== Web Server Benchmarks ==="
    COMMAND web_server_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    COMMAND echo "=== Database Benchmarks ==="
    COMMAND database_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    COMMAND echo "=== Audio/Video Benchmarks ==="
    COMMAND audio_video_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    DEPENDS threadpool_resampling_benchmarks web_server_benchmarks database_benchmarks audio_video_benchmarks
    COMMENT "Running real-world scenario benchmarks (explicit target, not part of normal build)"
)

add_custom_target(run_all_benchmarks
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target run_thread_benchmarks
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target run_core_benchmarks
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target run_real_world_benchmarks
    DEPENDS ${ALL_BENCHMARK_TARGETS} thread_benchmarks thread_allocation_benchmarks
    COMMENT "Running complete ThreadSchedule benchmark suite (explicit target, not part of normal build)"
)

# Quick benchmarks (shorter runtime for development)
add_custom_target(run_quick_benchmarks
    COMMAND threadpool_basic_benchmarks --benchmark_min_time=0.5s --benchmark_repetitions=1
    COMMAND threadpool_resampling_benchmarks --benchmark_min_time=0.5s --benchmark_repetitions=1
    DEPENDS threadpool_basic_benchmarks threadpool_resampling_benchmarks
    COMMENT "Running quick benchmarks for development (explicit target, not part of normal build)"
)

# Performance comparison target
add_custom_target(compare_pools
    COMMAND echo "=== Pool Performance Comparison ==="
    COMMAND threadpool_basic_benchmarks --benchmark_filter="BM_ComparePoolTypes.*" --benchmark_min_time=2s
    DEPENDS threadpool_basic_benchmarks
    COMMENT "Comparing performance of different thread pool types (explicit target, not part of normal build)"
) 
