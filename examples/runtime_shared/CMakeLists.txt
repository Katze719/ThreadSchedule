cmake_minimum_required(VERSION 3.14)

if(NOT THREADSCHEDULE_RUNTIME)
  message(STATUS "Skipping runtime_shared examples because THREADSCHEDULE_RUNTIME is OFF")
  return()
endif()

add_library(runtime_libA SHARED libA.cpp)
target_link_libraries(runtime_libA PRIVATE ThreadSchedule::ThreadSchedule ThreadSchedule::Runtime)
target_include_directories(runtime_libA PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  add_custom_command(TARGET runtime_libA POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:ThreadScheduleRuntime>
      $<TARGET_FILE_DIR:runtime_libA>)
endif()

add_library(runtime_libB SHARED libB.cpp)
target_link_libraries(runtime_libB PRIVATE ThreadSchedule::ThreadSchedule ThreadSchedule::Runtime)
target_include_directories(runtime_libB PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  add_custom_command(TARGET runtime_libB POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:ThreadScheduleRuntime>
      $<TARGET_FILE_DIR:runtime_libB>)
endif()

add_executable(runtime_main main.cpp)
target_link_libraries(runtime_main PRIVATE ThreadSchedule::ThreadSchedule ThreadSchedule::Runtime runtime_libA runtime_libB)
if(WIN32)
  add_custom_command(TARGET runtime_main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:ThreadScheduleRuntime>
      $<TARGET_FILE_DIR:runtime_main>)
endif()

