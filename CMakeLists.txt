cmake_minimum_required(VERSION 3.28)
project(ThreadSchedule VERSION 1.0.0 LANGUAGES CXX)

# C++23 Standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clangd and other tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.8/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# Add Google Benchmark dependency (only if benchmarks are enabled)
if(THREADSCHEDULE_BUILD_BENCHMARKS)
    CPMAddPackage(
      NAME benchmark
      GITHUB_REPOSITORY google/benchmark
      VERSION 1.9.4
      OPTIONS
        "BENCHMARK_ENABLE_TESTING OFF"
        "BENCHMARK_ENABLE_GTEST_TESTS OFF"
        "BENCHMARK_ENABLE_INSTALL OFF"
        "BENCHMARK_DOWNLOAD_DEPENDENCIES ON"
    )
endif()

# Compiler-specific flags for modern C++
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=thread,undefined")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
endif()

# Linux-specific requirements
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    set(LINUX_THREAD_LIBS pthread rt)
endif()

# Header-only library target
add_library(ThreadSchedule INTERFACE)
add_library(ThreadSchedule::ThreadSchedule ALIAS ThreadSchedule)

# Include directories
target_include_directories(ThreadSchedule INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link libraries
target_link_libraries(ThreadSchedule INTERFACE 
    Threads::Threads
    ${LINUX_THREAD_LIBS}
)

# Compiler features
target_compile_features(ThreadSchedule INTERFACE cxx_std_23)

# Copy compile_commands.json to project root for clangd
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(copy-compile-commands ALL
        ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
        DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
        COMMENT "Copying compile_commands.json to project root"
    )
endif()

# Optional components (OFF by default for easy integration)
option(THREADSCHEDULE_BUILD_EXAMPLES "Build examples" OFF)
option(THREADSCHEDULE_BUILD_TESTS "Build tests" OFF)
option(THREADSCHEDULE_BUILD_BENCHMARKS "Build benchmarks" OFF)

# Only build optional components if this is the main project or explicitly requested
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    # This is the main project, enable examples by default
    set(THREADSCHEDULE_BUILD_EXAMPLES ON CACHE BOOL "Build examples" FORCE)
endif()

if(THREADSCHEDULE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(THREADSCHEDULE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(THREADSCHEDULE_BUILD_BENCHMARKS AND TARGET benchmark::benchmark)
    add_subdirectory(benchmarks)
endif()

# Install configuration (only if this is the main project)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
endif()

# Install configuration (only if this is the main project)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    # Install headers
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    # Install targets
    install(TARGETS ThreadSchedule
        EXPORT ThreadScheduleTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Export configuration
    install(EXPORT ThreadScheduleTargets
        FILE ThreadScheduleTargets.cmake
        NAMESPACE ThreadSchedule::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadSchedule
    )

    # Package config
    configure_package_config_file(
        cmake/ThreadScheduleConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/ThreadScheduleConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadSchedule
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/ThreadScheduleConfigVersion.cmake
        COMPATIBILITY AnyNewerVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ThreadScheduleConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/ThreadScheduleConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadSchedule
    )
endif() 
