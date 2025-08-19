# Tests CMakeLists.txt

# Simple test to verify compilation
add_executable(compile_test compile_test.cpp)
target_link_libraries(compile_test ThreadSchedule::ThreadSchedule)

add_test(NAME CompileTest COMMAND compile_test) 
