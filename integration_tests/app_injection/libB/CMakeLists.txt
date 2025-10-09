cmake_minimum_required(VERSION 3.14)
project(AppInjLibB LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_library(appinj_libB SHARED src/libB.cpp)
target_include_directories(appinj_libB PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(appinj_libB PRIVATE ThreadSchedule::ThreadSchedule)

if(WIN32)
    target_compile_definitions(appinj_libB PRIVATE BUILD_APPINJ_LIBB_SHARED)
endif()


