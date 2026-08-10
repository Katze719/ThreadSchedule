cmake_minimum_required(VERSION 3.14)

project(ThreadScheduleGettingStarted LANGUAGES CXX)

find_package(ThreadSchedule 3 CONFIG REQUIRED)

add_executable(getting_started main.cpp)
target_link_libraries(getting_started
                      PRIVATE ThreadSchedule::ThreadSchedule)

enable_testing()
add_test(NAME ThreadScheduleGettingStarted COMMAND getting_started)
