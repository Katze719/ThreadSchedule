cmake_minimum_required(VERSION 3.14)
project(RuntimeAbiCompatLibB VERSION 1.0.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(runtime_abi_libB SHARED
    src/library_rb.cpp
)

# Use headers from the old installed ThreadSchedule
target_include_directories(runtime_abi_libB
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        
    )
target_include_directories(runtime_abi_libB BEFORE
    PRIVATE
        ${OLD_TS_PREFIX}/include
)

# Ensure headers use runtime declarations
target_compile_definitions(runtime_abi_libB PRIVATE THREADSCHEDULE_RUNTIME)

target_compile_features(runtime_abi_libB PUBLIC cxx_std_17)

if(WIN32)
    target_compile_definitions(runtime_abi_libB PRIVATE BUILD_RUNTIME_LIBB_SHARED)
    # Import current runtime target for linking (provided via CMAKE_PREFIX_PATH)
    find_package(ThreadSchedule REQUIRED)
    target_link_libraries(runtime_abi_libB PRIVATE Threads::Threads ThreadSchedule::Runtime)
else()
    # On ELF platforms, allow unresolved registry() to be satisfied by the main app's runtime at run time
    target_link_libraries(runtime_abi_libB PRIVATE Threads::Threads)
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


