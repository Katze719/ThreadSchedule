cmake_minimum_required(VERSION 3.20)
project(StableAbiRuntimeIntegration LANGUAGES CXX)

find_package(ThreadSchedule REQUIRED)

if(NOT TARGET ThreadSchedule::StableAbi)
    if(TARGET ThreadSchedule::ThreadScheduleStableAbi)
        add_library(ThreadSchedule::StableAbi ALIAS ThreadSchedule::ThreadScheduleStableAbi)
    else()
        message(FATAL_ERROR "ThreadSchedule::StableAbi target is required. Configure ThreadSchedule with THREADSCHEDULE_RUNTIME=ON.")
    endif()
endif()

include(CTest)
enable_testing()

add_library(stable_abi_producer SHARED
    producer/producer.cpp
)
target_link_libraries(stable_abi_producer PRIVATE ThreadSchedule::StableAbi)
target_compile_features(stable_abi_producer PRIVATE cxx_std_17)
target_compile_definitions(stable_abi_producer PRIVATE STABLE_ABI_PRODUCER_BUILD=1)

add_executable(stable_abi_consumer
    consumer/main.cpp
)
target_link_libraries(stable_abi_consumer PRIVATE
    ThreadSchedule::StableAbi
    stable_abi_producer
)
target_compile_features(stable_abi_consumer PRIVATE cxx_std_23)

if(WIN32)
    add_custom_command(TARGET stable_abi_consumer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:stable_abi_producer>
            $<TARGET_FILE_DIR:stable_abi_consumer>
    )
    if(TARGET ThreadSchedule::ThreadScheduleRuntime)
        add_custom_command(TARGET stable_abi_consumer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ThreadSchedule::ThreadScheduleRuntime>
                $<TARGET_FILE_DIR:stable_abi_consumer>
        )
    endif()
endif()

add_test(NAME StableAbiRuntimeIntegrationTest COMMAND stable_abi_consumer)
