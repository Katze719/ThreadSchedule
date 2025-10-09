cmake_minimum_required(VERSION 3.14)
project(CompositeLibB VERSION 1.0.0 LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_library(composite_libB SHARED
    src/library_cb.cpp
)

target_include_directories(composite_libB
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(composite_libB
    PUBLIC
        ThreadSchedule::ThreadSchedule
)

target_compile_features(composite_libB PUBLIC cxx_std_17)

if(WIN32)
    target_compile_definitions(composite_libB PRIVATE BUILD_COMPOSITE_LIBB_SHARED)
endif()


