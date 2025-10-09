cmake_minimum_required(VERSION 3.14)
project(RuntimeSingleRegistryScenario LANGUAGES CXX)

if(NOT THREADSCHEDULE_RUNTIME)
  message(STATUS "Skipping runtime_single integration because THREADSCHEDULE_RUNTIME is OFF")
  return()
endif()

include(CTest)
enable_testing()

add_subdirectory(libA)
add_subdirectory(libB)
add_subdirectory(main_app)


