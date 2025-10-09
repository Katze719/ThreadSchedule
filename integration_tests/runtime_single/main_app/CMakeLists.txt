cmake_minimum_required(VERSION 3.14)
project(RuntimeMain LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_executable(runtime_main_app main.cpp)
target_link_libraries(runtime_main_app PRIVATE
    ThreadSchedule::ThreadSchedule
    ThreadSchedule::Runtime
    runtime_libA
    runtime_libB
)
target_compile_features(runtime_main_app PRIVATE cxx_std_17)

if(WIN32)
    add_custom_command(TARGET runtime_main_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ThreadSchedule::ThreadScheduleRuntime>
            $<TARGET_FILE_DIR:runtime_main_app>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:runtime_libA>
            $<TARGET_FILE_DIR:runtime_main_app>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:runtime_libB>
            $<TARGET_FILE_DIR:runtime_main_app>
    )
endif()

enable_testing()
add_test(NAME RuntimeSingleRegistryIntegrationTest COMMAND runtime_main_app)


