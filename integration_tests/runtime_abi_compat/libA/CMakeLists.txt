cmake_minimum_required(VERSION 3.14)
project(RuntimeAbiCompatLibA VERSION 1.0.0 LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_library(runtime_abi_libA SHARED
    src/library_ra.cpp
)

target_include_directories(runtime_abi_libA
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(runtime_abi_libA
    PUBLIC
        ThreadSchedule::ThreadSchedule
        ThreadSchedule::Runtime
)

target_compile_features(runtime_abi_libA PUBLIC cxx_std_17)

if(WIN32)
    target_compile_definitions(runtime_abi_libA PRIVATE BUILD_RUNTIME_LIBA_SHARED)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS runtime_abi_libA
    EXPORT RuntimeAbiLibATargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

install(EXPORT RuntimeAbiLibATargets
    FILE RuntimeAbiLibATargets.cmake
    NAMESPACE RuntimeAbi::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RuntimeAbiLibA
)


