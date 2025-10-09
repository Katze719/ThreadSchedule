cmake_minimum_required(VERSION 3.14)

if(NOT THREADSCHEDULE_RUNTIME)
  message(STATUS "Skipping runtime_shared examples because THREADSCHEDULE_RUNTIME is OFF")
  return()
endif()

add_library(runtime_libA SHARED libA.cpp)
target_link_libraries(runtime_libA PRIVATE ThreadSchedule::ThreadSchedule ThreadSchedule::Runtime)
target_include_directories(runtime_libA PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(runtime_libB SHARED libB.cpp)
target_link_libraries(runtime_libB PRIVATE ThreadSchedule::ThreadSchedule ThreadSchedule::Runtime)
target_include_directories(runtime_libB PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(runtime_main main.cpp)
target_link_libraries(runtime_main PRIVATE ThreadSchedule::ThreadSchedule ThreadSchedule::Runtime runtime_libA runtime_libB)

