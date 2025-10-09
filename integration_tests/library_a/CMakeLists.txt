cmake_minimum_required(VERSION 3.14)
project(LibraryA VERSION 1.0.0 LANGUAGES CXX)

# Find ThreadSchedule (header-only library)
find_package(ThreadSchedule REQUIRED)

# Create library A as a shared library
add_library(library_a SHARED
    src/library_a.cpp
)

target_include_directories(library_a
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(library_a
    PUBLIC ThreadSchedule::ThreadSchedule
)

# Set C++ standard
target_compile_features(library_a PUBLIC cxx_std_17)

# Export symbols for Windows
if(WIN32)
    target_compile_definitions(library_a PRIVATE LIBRARY_A_EXPORTS)
    target_compile_definitions(library_a INTERFACE LIBRARY_A_IMPORTS)
endif()

# Installation
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS library_a
    EXPORT LibraryATargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

install(EXPORT LibraryATargets
    FILE LibraryATargets.cmake
    NAMESPACE LibraryA::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibraryA
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/LibraryAConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/LibraryAConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibraryA
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LibraryAConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/LibraryAConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/LibraryAConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibraryA
)
