# Examples CMakeLists.txt

# Basic thread wrapper example
add_executable(basic_example basic_example.cpp)
target_link_libraries(basic_example ThreadSchedule::ThreadSchedule) 
