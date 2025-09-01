# Benchmarks for ThreadSchedule

# Create separate benchmark executables to avoid multiple definition issues
add_executable(threadpool_basic_benchmarks threadpool_benchmarks.cpp)
add_executable(threadpool_throughput_benchmarks throughput_benchmarks.cpp)  
add_executable(threadpool_memory_benchmarks memory_benchmarks.cpp)
add_executable(threadpool_resampling_benchmarks resampling_benchmarks.cpp)

# Link libraries for all benchmarks
foreach(target threadpool_basic_benchmarks threadpool_throughput_benchmarks threadpool_memory_benchmarks threadpool_resampling_benchmarks)
    target_link_libraries(${target} 
        PRIVATE
        ThreadSchedule::ThreadSchedule
        benchmark::benchmark
    )
    
    # Set compiler features
    target_compile_features(${target} PRIVATE cxx_std_23)
    
    # Compiler flags for benchmarks
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE 
            -O3 -DNDEBUG 
            -fno-omit-frame-pointer 
            -march=native
        )
    endif()
endforeach()

# Register benchmarks with CTest
add_test(NAME ThreadPoolBasicBenchmarks COMMAND threadpool_basic_benchmarks --benchmark_min_time=1.0)
add_test(NAME ThreadPoolThroughputBenchmarks COMMAND threadpool_throughput_benchmarks --benchmark_min_time=1.0)
add_test(NAME ThreadPoolMemoryBenchmarks COMMAND threadpool_memory_benchmarks --benchmark_min_time=1.0)
add_test(NAME ThreadPoolResamplingBenchmarks COMMAND threadpool_resampling_benchmarks --benchmark_min_time=1.0)

# Create a custom target to run all benchmarks
add_custom_target(run_all_benchmarks
    COMMAND threadpool_basic_benchmarks --benchmark_min_time=1.0
    COMMAND threadpool_throughput_benchmarks --benchmark_min_time=1.0  
    COMMAND threadpool_memory_benchmarks --benchmark_min_time=1.0
    COMMAND threadpool_resampling_benchmarks --benchmark_min_time=1.0
    DEPENDS threadpool_basic_benchmarks threadpool_throughput_benchmarks threadpool_memory_benchmarks threadpool_resampling_benchmarks
    COMMENT "Running all ThreadSchedule benchmarks"
) 
