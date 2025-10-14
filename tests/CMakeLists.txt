# Tests CMakeLists.txt

# Simple test to verify compilation (keep for backwards compatibility)
add_executable(compile_test compile_test.cpp)
target_link_libraries(compile_test ThreadSchedule::ThreadSchedule)
add_test(NAME CompileTest COMMAND compile_test)

# Google Test based tests
if(TARGET gtest)
    include(GoogleTest)
    # ThreadWrapper tests
    add_executable(thread_wrapper_test thread_wrapper_test.cpp)
    target_link_libraries(thread_wrapper_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    gtest_discover_tests(thread_wrapper_test 
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    # ThreadWrapperView tests
    add_executable(thread_wrapper_view_test thread_wrapper_view_test.cpp)
    target_link_libraries(thread_wrapper_view_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    gtest_discover_tests(thread_wrapper_view_test 
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    # ThreadConfig tests (Priority, Affinity, SchedulingPolicy)
    add_executable(thread_config_test thread_config_test.cpp)
    target_link_libraries(thread_config_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    gtest_discover_tests(thread_config_test 
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    # ThreadPool tests
    add_executable(thread_pool_test thread_pool_test.cpp)
    target_link_libraries(thread_pool_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    gtest_discover_tests(thread_pool_test 
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    # Expected tests
    add_executable(expected_test expected_test.cpp)
    target_link_libraries(expected_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    gtest_discover_tests(expected_test 
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )
    add_executable(thread_registry_stress_test thread_registry_stress_test.cpp)
    target_link_libraries(thread_registry_stress_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    gtest_discover_tests(thread_registry_stress_test 
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    # ThreadRegistry tests
    add_executable(thread_registry_test thread_registry_test.cpp)
    target_link_libraries(thread_registry_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    gtest_discover_tests(thread_registry_test 
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    if(THREADSCHEDULE_RUNTIME)
        add_executable(runtime_registry_test runtime_registry_test.cpp)
        target_link_libraries(runtime_registry_test 
            ThreadSchedule::ThreadSchedule 
            gtest 
            gtest_main
            ThreadSchedule::Runtime
        )
        target_compile_definitions(runtime_registry_test PRIVATE THREADSCHEDULE_RUNTIME)
        gtest_discover_tests(runtime_registry_test 
            DISCOVERY_TIMEOUT 60
            DISCOVERY_MODE PRE_TEST
            WORKING_DIRECTORY $<TARGET_FILE_DIR:runtime_registry_test>
            PROPERTIES TIMEOUT 120
        )

        if(WIN32)
            add_custom_command(TARGET runtime_registry_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:ThreadScheduleRuntime>
                    $<TARGET_FILE_DIR:runtime_registry_test>
            )
        endif()
    endif()

    # Discovered tests already carry TIMEOUT=120 via PROPERTIES above
endif() 
