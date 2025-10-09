cmake_minimum_required(VERSION 3.14)

# Project version
# Determine project version from VERSION file (single source of truth)
set(THREADSCHEDULE_VERSION "0.0.0")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" THREADSCHEDULE_VERSION)
    string(STRIP "${THREADSCHEDULE_VERSION}" THREADSCHEDULE_VERSION)
endif()

project(ThreadSchedule 
    VERSION ${THREADSCHEDULE_VERSION}
    DESCRIPTION "Modern C++ thread management library"
    HOMEPAGE_URL "https://github.com/Katze719/ThreadSchedule"
    LANGUAGES CXX
)

# Ensure MSVC uses the shared runtime (/MD, /MDd) to avoid CRT mismatches with dependencies
if(MSVC)
    foreach(flag_var CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL
                    CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        if(DEFINED ${flag_var})
            string(REGEX REPLACE "/MTd" "/MDd" ${flag_var} "${${flag_var}}")
            string(REGEX REPLACE "/MT" "/MD" ${flag_var} "${${flag_var}}")
            set(${flag_var} "${${flag_var}}" CACHE STRING "" FORCE)
        endif()
    endforeach()
endif()

# Only set CMAKE_CXX_STANDARD if not already set by parent project
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()

# Ensure minimum C++17
if(CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "ThreadSchedule requires at least C++17")
endif()

if(NOT DEFINED CMAKE_CXX_STANDARD_REQUIRED)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(NOT DEFINED CMAKE_CXX_EXTENSIONS)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Determine if ThreadSchedule is built as a subproject (using add_subdirectory) or standalone
set(THREADSCHEDULE_IS_TOPLEVEL_PROJECT OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(THREADSCHEDULE_IS_TOPLEVEL_PROJECT ON)
endif()

# Export compile commands for clangd and other tools (only for top-level project)
if(THREADSCHEDULE_IS_TOPLEVEL_PROJECT)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Options
option(THREADSCHEDULE_BUILD_EXAMPLES "Build examples" ${THREADSCHEDULE_IS_TOPLEVEL_PROJECT})
option(THREADSCHEDULE_BUILD_TESTS "Build tests" OFF)
option(THREADSCHEDULE_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(THREADSCHEDULE_INSTALL "Generate install target" ${THREADSCHEDULE_IS_TOPLEVEL_PROJECT})
option(THREADSCHEDULE_RUNTIME "Build shared runtime for global registry (non header-only)" OFF)

# CPM support (optional, download if building tests or benchmarks)
if(THREADSCHEDULE_BUILD_TESTS OR THREADSCHEDULE_BUILD_BENCHMARKS)
    # Download CPM.cmake if not already available
    if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake")
        file(
            DOWNLOAD
            https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.8/CPM.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
            EXPECTED_HASH SHA256=78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791
        )
    endif()
    include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)
endif()

# Add Google Test dependency (for tests)
if(THREADSCHEDULE_BUILD_TESTS)
    CPMAddPackage(
        NAME googletest
        GITHUB_REPOSITORY google/googletest
        VERSION 1.14.0
        OPTIONS
            "INSTALL_GTEST OFF"
            "BUILD_GMOCK OFF"
            "gtest_force_shared_crt ON"
    )
endif()

# Add Google Benchmark dependency (for benchmarks)
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

# Platform-specific requirements
find_package(Threads REQUIRED)

# Create the interface library target (header-only API)
add_library(ThreadSchedule INTERFACE)
add_library(ThreadSchedule::ThreadSchedule ALIAS ThreadSchedule)

# Set target properties based on selected C++ standard
if(CMAKE_CXX_STANDARD GREATER_EQUAL 23)
    set_target_properties(ThreadSchedule PROPERTIES
        INTERFACE_COMPILE_FEATURES cxx_std_23
    )
elseif(CMAKE_CXX_STANDARD GREATER_EQUAL 20)
    set_target_properties(ThreadSchedule PROPERTIES
        INTERFACE_COMPILE_FEATURES cxx_std_20
    )
else()
    set_target_properties(ThreadSchedule PROPERTIES
        INTERFACE_COMPILE_FEATURES cxx_std_17
    )
endif()

# Include directories
target_include_directories(ThreadSchedule INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link libraries
target_link_libraries(ThreadSchedule INTERFACE Threads::Threads)

# Windows: ensure modern API availability macros
if(WIN32)
    target_compile_definitions(ThreadSchedule INTERFACE
        $<BUILD_INTERFACE:_WIN32_WINNT=0x0A00>
    )
endif()

# Platform-specific libraries
if(UNIX AND NOT APPLE AND NOT WIN32)
    # Linux-specific libraries
    target_link_libraries(ThreadSchedule INTERFACE pthread rt)
endif()
# Windows (MSVC and MinGW) use Threads::Threads which is already linked above

# Compiler-specific warnings and flags (only for top-level project to avoid polluting parent)
if(THREADSCHEDULE_IS_TOPLEVEL_PROJECT)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ThreadSchedule INTERFACE
            $<BUILD_INTERFACE:-Wall -Wextra -Wpedantic>
        )
        # MinGW (GCC): allow permissive mode for broader compatibility when requested
        if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(ThreadSchedule INTERFACE
                $<BUILD_INTERFACE:-fpermissive>
            )
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(ThreadSchedule INTERFACE
            $<BUILD_INTERFACE:/W4 /permissive- /Zc:preprocessor /EHsc>
        )
    endif()
endif()

# Copy compile_commands.json to project root for clangd (single-config generators only)
if(THREADSCHEDULE_IS_TOPLEVEL_PROJECT AND CMAKE_EXPORT_COMPILE_COMMANDS AND NOT CMAKE_CONFIGURATION_TYPES)
    add_custom_target(copy-compile-commands ALL
        ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
        DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
        COMMENT "Copying compile_commands.json to project root"
    )
endif()

# Optional components
if(THREADSCHEDULE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(THREADSCHEDULE_BUILD_TESTS)
    enable_testing()
    if(TARGET gtest)
        add_subdirectory(tests)
    endif()
endif()

if(THREADSCHEDULE_BUILD_BENCHMARKS AND TARGET benchmark::benchmark)
    add_subdirectory(benchmarks)
endif()

# Optional shared runtime that exports a single global registry instance
if(THREADSCHEDULE_RUNTIME)
    add_library(ThreadScheduleRuntime SHARED
        src/runtime_registry.cpp
    )
    target_compile_definitions(ThreadScheduleRuntime PRIVATE THREADSCHEDULE_EXPORTS THREADSCHEDULE_RUNTIME)
    # Propagate the THREADSCHEDULE_RUNTIME define to consumers so headers call into the DLL
    target_compile_definitions(ThreadScheduleRuntime INTERFACE THREADSCHEDULE_RUNTIME)
    target_include_directories(ThreadScheduleRuntime
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(ThreadScheduleRuntime PUBLIC Threads::Threads)
    if(UNIX AND NOT APPLE AND NOT WIN32)
        target_link_libraries(ThreadScheduleRuntime PUBLIC pthread rt)
    endif()
    set_target_properties(ThreadScheduleRuntime PROPERTIES OUTPUT_NAME "threadschedule")
    add_library(ThreadSchedule::Runtime ALIAS ThreadScheduleRuntime)
endif()

# Installation
if(THREADSCHEDULE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    # Install headers
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
    )

    # Install targets
    install(TARGETS ThreadSchedule
        EXPORT ThreadScheduleTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    if(THREADSCHEDULE_RUNTIME)
        install(TARGETS ThreadScheduleRuntime
            EXPORT ThreadScheduleTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
    endif()

    # Export configuration
    install(EXPORT ThreadScheduleTargets
        FILE ThreadScheduleTargets.cmake
        NAMESPACE ThreadSchedule::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadSchedule
    )

    # Generate package config file
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThreadScheduleConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/ThreadScheduleConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadSchedule
    )

    # Generate package version file
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/ThreadScheduleConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    # Install package config files
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ThreadScheduleConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/ThreadScheduleConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadSchedule
    )

    # Export targets for build tree
    export(EXPORT ThreadScheduleTargets
        FILE ${CMAKE_CURRENT_BINARY_DIR}/ThreadScheduleTargets.cmake
        NAMESPACE ThreadSchedule::
    )
endif()
