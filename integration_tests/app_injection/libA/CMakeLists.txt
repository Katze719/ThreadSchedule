cmake_minimum_required(VERSION 3.14)
project(AppInjLibA LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_library(appinj_libA SHARED src/libA.cpp)
target_include_directories(appinj_libA PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(appinj_libA PRIVATE ThreadSchedule::ThreadSchedule)

if(WIN32)
    target_compile_definitions(appinj_libA PRIVATE BUILD_APPINJ_LIBA_SHARED)
endif()


