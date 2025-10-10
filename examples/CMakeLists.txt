# Examples CMakeLists.txt

# Basic thread wrapper example
add_executable(basic_example basic_example.cpp)
target_link_libraries(basic_example ThreadSchedule::ThreadSchedule) 

# Performance benchmark for high-performance ThreadPool
add_executable(performance_benchmark performance_benchmark.cpp)
target_link_libraries(performance_benchmark ThreadSchedule::ThreadSchedule) 

# Thread registry examples
add_executable(registry_example registry_example.cpp)
target_link_libraries(registry_example ThreadSchedule::ThreadSchedule)

add_executable(registry_merge_example registry_merge_example.cpp)
target_link_libraries(registry_merge_example ThreadSchedule::ThreadSchedule)

add_executable(external_registry_example external_registry_example.cpp)
target_link_libraries(external_registry_example ThreadSchedule::ThreadSchedule)

add_executable(registry_chainable_example registry_chainable_example.cpp)
target_link_libraries(registry_chainable_example ThreadSchedule::ThreadSchedule)

# Runtime shared registry example (requires THREADSCHEDULE_RUNTIME)
add_subdirectory(runtime_shared)

