# ThreadSchedule CPM Integration
# This file makes it easy to use ThreadSchedule with CPM.cmake

include_guard()

# Download CPM.cmake if not already available
if(NOT DEFINED CPM_DOWNLOAD_VERSION)
    set(CPM_DOWNLOAD_VERSION 0.40.8)
endif()

if(NOT COMMAND CPMAddPackage)
    if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake")
        file(
            DOWNLOAD
            https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
            EXPECTED_HASH SHA256=78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791
        )
    endif()

    include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)
endif()

# Keep the helper synchronized with the checkout that provides it.
set(_threadschedule_version_file "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
if(NOT EXISTS "${_threadschedule_version_file}")
    message(FATAL_ERROR "ThreadSchedule VERSION file was not found")
endif()
file(READ "${_threadschedule_version_file}" _threadschedule_version)
string(STRIP "${_threadschedule_version}" _threadschedule_version)

# Add ThreadSchedule using CPM
# Usage: include(path/to/ThreadScheduleAddCPM.cmake)
CPMAddPackage(
    NAME ThreadSchedule
    GITHUB_REPOSITORY Katze719/ThreadSchedule
    VERSION ${_threadschedule_version}
    GIT_TAG v${_threadschedule_version}
    OPTIONS
        "THREADSCHEDULE_BUILD_EXAMPLES OFF"
        "THREADSCHEDULE_BUILD_TESTS OFF"
        "THREADSCHEDULE_BUILD_BENCHMARKS OFF"
)

unset(_threadschedule_version)
unset(_threadschedule_version_file)
