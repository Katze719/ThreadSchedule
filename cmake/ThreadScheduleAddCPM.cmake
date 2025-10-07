# ThreadSchedule CPM Integration
# This file makes it easy to use ThreadSchedule with CPM.cmake

include_guard()

# Download CPM.cmake if not already available
if(NOT DEFINED CPM_DOWNLOAD_VERSION)
    set(CPM_DOWNLOAD_VERSION 0.40.8)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake")
    file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791
    )
endif()

include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# Add ThreadSchedule using CPM
# Usage: include(path/to/ThreadScheduleAddCPM.cmake)
CPMAddPackage(
    NAME ThreadSchedule
    GITHUB_REPOSITORY Katze719/ThreadSchedule
    VERSION 1.0.0
    GIT_TAG v1.0.0
    OPTIONS
        "THREADSCHEDULE_BUILD_EXAMPLES OFF"
        "THREADSCHEDULE_BUILD_TESTS OFF"
        "THREADSCHEDULE_BUILD_BENCHMARKS OFF"
)
