cmake_minimum_required(VERSION 3.14)
project(RuntimeAbiCompatMain LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

add_executable(runtime_abi_main main.cpp)
target_link_libraries(runtime_abi_main PRIVATE
    ThreadSchedule::ThreadSchedule
    ThreadSchedule::Runtime
    runtime_abi_libA
    runtime_abi_libB
)
target_compile_features(runtime_abi_main PRIVATE cxx_std_17)

add_custom_command(TARGET runtime_abi_main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:ThreadSchedule::ThreadScheduleRuntime>
        $<TARGET_FILE_DIR:runtime_abi_main>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:runtime_abi_libA>
        $<TARGET_FILE_DIR:runtime_abi_main>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:runtime_abi_libB>
        $<TARGET_FILE_DIR:runtime_abi_main>
)

if(UNIX AND NOT APPLE)
    # Ensure loader finds the runtime next to the executable
    set_target_properties(runtime_abi_main PROPERTIES BUILD_RPATH "$ORIGIN")
endif()

enable_testing()
add_test(NAME RuntimeAbiCompatibilityTest COMMAND runtime_abi_main)

if(UNIX AND NOT APPLE)
    set_tests_properties(RuntimeAbiCompatibilityTest PROPERTIES
        ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:runtime_abi_main>")
endif()


