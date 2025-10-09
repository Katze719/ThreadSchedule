cmake_minimum_required(VERSION 3.14)
project(AppInjectionMain LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_executable(app_injection_main src/main.cpp)
target_link_libraries(app_injection_main PRIVATE ThreadSchedule::ThreadSchedule appinj_libA appinj_libB)
target_include_directories(app_injection_main PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../libA/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libB/include
)
target_compile_features(app_injection_main PRIVATE cxx_std_17)

enable_testing()
add_test(NAME AppInjectionIntegrationTest COMMAND app_injection_main)


