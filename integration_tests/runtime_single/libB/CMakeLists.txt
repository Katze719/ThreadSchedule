cmake_minimum_required(VERSION 3.14)
project(RuntimeLibB VERSION 1.0.0 LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_library(runtime_libB SHARED
    src/library_rb.cpp
)

target_include_directories(runtime_libB
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(runtime_libB
    PUBLIC
        ThreadSchedule::ThreadSchedule
        ThreadSchedule::Runtime
)

target_compile_features(runtime_libB PUBLIC cxx_std_17)

if(WIN32)
    target_compile_definitions(runtime_libB PRIVATE BUILD_RUNTIME_LIBB_SHARED)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS runtime_libB
    EXPORT RuntimeLibBTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

install(EXPORT RuntimeLibBTargets
    FILE RuntimeLibBTargets.cmake
    NAMESPACE RuntimeLibB::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RuntimeLibB
)


