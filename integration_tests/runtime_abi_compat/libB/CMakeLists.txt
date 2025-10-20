cmake_minimum_required(VERSION 3.14)
project(RuntimeAbiCompatLibB VERSION 1.0.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

# Consume the previously installed ThreadSchedule (previous tag)
set(THREADSCHEDULE_OLD_CONFIG_DIR ${OLD_TS_PREFIX}/lib64/cmake/ThreadSchedule)
if(WIN32)
  set(THREADSCHEDULE_OLD_CONFIG_DIR ${OLD_TS_PREFIX}/lib/cmake/ThreadSchedule)
endif()

find_package(ThreadSchedule REQUIRED PATHS ${THREADSCHEDULE_OLD_CONFIG_DIR} NO_DEFAULT_PATH)

add_library(runtime_abi_libB SHARED
    src/library_rb.cpp
)

target_include_directories(runtime_abi_libB
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(runtime_abi_libB
    PUBLIC
        ThreadSchedule::ThreadSchedule
        ThreadSchedule::Runtime
)

target_compile_features(runtime_abi_libB PUBLIC cxx_std_17)

if(WIN32)
    target_compile_definitions(runtime_abi_libB PRIVATE BUILD_RUNTIME_LIBB_SHARED)
endif()

add_dependencies(runtime_abi_libB ThreadScheduleOld)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS runtime_abi_libB
    EXPORT RuntimeAbiLibBTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

install(EXPORT RuntimeAbiLibBTargets
    FILE RuntimeAbiLibBTargets.cmake
    NAMESPACE RuntimeAbi::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RuntimeAbiLibB
)


