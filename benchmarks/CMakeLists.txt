# ThreadSchedule Benchmarks Suite
# Comprehensive benchmarking for all thread pool implementations with real-world scenarios

# Core thread pool benchmarks
add_executable(threadpool_basic_benchmarks threadpool_benchmarks.cpp)
add_executable(threadpool_throughput_benchmarks throughput_benchmarks.cpp)
add_executable(threadpool_memory_benchmarks memory_benchmarks.cpp)
add_executable(threadpool_resampling_benchmarks resampling_benchmarks.cpp)

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
            -ffast-math
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
add_test(NAME WebServerBenchmarks COMMAND web_server_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)
add_test(NAME DatabaseBenchmarks COMMAND database_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)
add_test(NAME AudioVideoBenchmarks COMMAND audio_video_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3)

# Custom targets for different benchmark categories (these targets execute benchmarks, not just build them)
# Note: These targets only run when explicitly requested. They do NOT run during normal builds.

add_custom_target(run_core_benchmarks
    COMMAND echo "=== Core ThreadPool Benchmarks ==="
    COMMAND threadpool_basic_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    COMMAND echo "=== Throughput Benchmarks ==="
    COMMAND threadpool_throughput_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    COMMAND echo "=== Memory Benchmarks ==="
    COMMAND threadpool_memory_benchmarks --benchmark_min_time=2s --benchmark_repetitions=3
    DEPENDS threadpool_basic_benchmarks threadpool_throughput_benchmarks threadpool_memory_benchmarks
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
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target run_core_benchmarks
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target run_real_world_benchmarks
    DEPENDS ${ALL_BENCHMARK_TARGETS}
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
