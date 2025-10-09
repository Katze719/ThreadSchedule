cmake_minimum_required(VERSION 3.14)
project(CompositeMergeScenario LANGUAGES CXX)

include(CTest)
enable_testing()

add_subdirectory(libA)
add_subdirectory(libB)
add_subdirectory(main_app)


