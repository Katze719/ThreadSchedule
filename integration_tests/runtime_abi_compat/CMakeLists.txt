cmake_minimum_required(VERSION 3.14)
project(RuntimeAbiCompatibility LANGUAGES CXX)

if(NOT THREADSCHEDULE_RUNTIME)
  message(STATUS "Skipping runtime_abi_compat integration because THREADSCHEDULE_RUNTIME is OFF")
  return()
endif()

include(CTest)
enable_testing()

include(ExternalProject)

# Parameters to select the old version to test against
set(RUNTIME_ABI_OLD_REF "" CACHE STRING "Git ref/tag/commit for old ThreadSchedule to test against")
set(RUNTIME_ABI_OLD_OFFSET "" CACHE STRING "N-th latest tag to use if OLD_REF not set (1=latest, 2=previous, ...)")

# Determine repository URL for cloning the old ref
execute_process(
  COMMAND git config --get remote.origin.url
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE TS_REPO_URL
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(TS_OLD_REF "")

if(NOT RUNTIME_ABI_OLD_REF STREQUAL "")
  set(TS_OLD_REF ${RUNTIME_ABI_OLD_REF})
else()
  if(NOT RUNTIME_ABI_OLD_OFFSET STREQUAL "")
    # Collect tags sorted by creation date (newest first) and pick by offset
    execute_process(
      COMMAND git tag --sort=-creatordate
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE _TS_TAGS_RAW
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REPLACE "\n" ";" _TS_TAGS_LIST "${_TS_TAGS_RAW}")
    if(_TS_TAGS_LIST)
      math(EXPR _TS_IDX "${RUNTIME_ABI_OLD_OFFSET} - 1" OUTPUT_FORMAT DECIMAL)
      list(LENGTH _TS_TAGS_LIST _TS_TAGS_LEN)
      if(_TS_IDX GREATER_EQUAL 0 AND _TS_IDX LESS _TS_TAGS_LEN)
        list(GET _TS_TAGS_LIST ${_TS_IDX} TS_OLD_REF)
      endif()
    endif()
  endif()
  if(TS_OLD_REF STREQUAL "")
    # Fallback: use the latest tag
    execute_process(
      COMMAND git describe --tags --abbrev=0
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE TS_OLD_REF
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  endif()
endif()

if(TS_OLD_REF STREQUAL "")
  message(FATAL_ERROR "Could not determine old ref for ABI compat test (set RUNTIME_ABI_OLD_REF or RUNTIME_ABI_OLD_OFFSET)")
endif()

if(TS_REPO_URL STREQUAL "")
  set(TS_REPO_URL https://github.com/Katze719/ThreadSchedule.git)
endif()

set(OLD_TS_PREFIX ${CMAKE_BINARY_DIR}/old_ts_install)

ExternalProject_Add(ThreadScheduleOld
  GIT_REPOSITORY ${TS_REPO_URL}
  GIT_TAG ${TS_OLD_REF}
  UPDATE_DISCONNECTED 1
  SOURCE_SUBDIR .
  CMAKE_ARGS
    -DTHREADSCHEDULE_RUNTIME=ON
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_BUILD_TYPE=$<IF:$<CONFIG:>,${CMAKE_BUILD_TYPE},$<CONFIG>>
    -DCMAKE_INSTALL_PREFIX=${OLD_TS_PREFIX}
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<IF:$<CONFIG:>,${CMAKE_BUILD_TYPE},$<CONFIG>>
  INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config $<IF:$<CONFIG:>,${CMAKE_BUILD_TYPE},$<CONFIG>>
)

add_subdirectory(libA)
add_subdirectory(libB)
add_subdirectory(main_app)


