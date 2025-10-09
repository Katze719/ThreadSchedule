cmake_minimum_required(VERSION 3.14)
project(CompositeMain LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_executable(composite_main_app main.cpp)
target_link_libraries(composite_main_app PRIVATE
    ThreadSchedule::ThreadSchedule
    composite_libA
    composite_libB
)
target_compile_features(composite_main_app PRIVATE cxx_std_17)

add_dependencies(composite_main_app composite_libA composite_libB)
target_include_directories(composite_main_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../libA/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libB/include
)

enable_testing()
add_test(NAME CompositeMergeIntegrationTest COMMAND composite_main_app)


