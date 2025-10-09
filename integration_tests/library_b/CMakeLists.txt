cmake_minimum_required(VERSION 3.14)
project(LibraryB VERSION 1.0.0 LANGUAGES CXX)

# Find ThreadSchedule (header-only library)
find_package(ThreadSchedule REQUIRED)

# Create library B as a shared library
add_library(library_b SHARED
    src/library_b.cpp
)

target_include_directories(library_b
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(library_b
    PUBLIC ThreadSchedule::ThreadSchedule
)

# Set C++ standard
target_compile_features(library_b PUBLIC cxx_std_17)

# Export symbols for Windows
if(WIN32)
    target_compile_definitions(library_b PRIVATE LIBRARY_B_EXPORTS)
    target_compile_definitions(library_b INTERFACE LIBRARY_B_IMPORTS)
endif()

# Installation
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS library_b
    EXPORT LibraryBTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

install(EXPORT LibraryBTargets
    FILE LibraryBTargets.cmake
    NAMESPACE LibraryB::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibraryB
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/LibraryBConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/LibraryBConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibraryB
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LibraryBConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/LibraryBConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/LibraryBConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibraryB
)
