cmake_minimum_required(VERSION 3.14)
project(RuntimeMain LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_executable(runtime_main_app main.cpp)
target_link_libraries(runtime_main_app PRIVATE
    ThreadSchedule::ThreadSchedule
    ThreadSchedule::Runtime
    runtime_libA
    runtime_libB
)
target_compile_features(runtime_main_app PRIVATE cxx_std_17)

enable_testing()
add_test(NAME RuntimeSingleRegistryIntegrationTest COMMAND runtime_main_app)


