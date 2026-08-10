cmake_minimum_required(VERSION 3.14)

project(ThreadScheduleConanTest LANGUAGES CXX)

find_package(ThreadSchedule CONFIG REQUIRED)

add_executable(test_package test_package.cpp)
if(TEST_SHARED_RUNTIME)
    target_link_libraries(test_package PRIVATE ThreadSchedule::Runtime)
else()
    target_link_libraries(test_package
                          PRIVATE ThreadSchedule::ThreadSchedule)
endif()
