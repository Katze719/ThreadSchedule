# Examples CMakeLists.txt

# Basic thread wrapper example
add_executable(basic_example basic_example.cpp)
target_link_libraries(basic_example ThreadSchedule::ThreadSchedule) 

# Performance benchmark for high-performance ThreadPool
add_executable(performance_benchmark performance_benchmark.cpp)
target_link_libraries(performance_benchmark ThreadSchedule::ThreadSchedule) 
