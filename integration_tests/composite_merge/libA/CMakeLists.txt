cmake_minimum_required(VERSION 3.14)
project(CompositeLibA VERSION 1.0.0 LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_library(composite_libA SHARED
    src/library_ca.cpp
)

target_include_directories(composite_libA
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(composite_libA
    PUBLIC
        ThreadSchedule::ThreadSchedule
)

target_compile_features(composite_libA PUBLIC cxx_std_17)

if(WIN32)
    target_compile_definitions(composite_libA PRIVATE BUILD_COMPOSITE_LIBA_SHARED)
endif()


