# Tests CMakeLists.txt

# Simple test to verify compilation (keep for backwards compatibility)
add_executable(compile_test compile_test.cpp)
target_link_libraries(compile_test ThreadSchedule::ThreadSchedule)
add_test(NAME CompileTest COMMAND compile_test)

# Google Test based tests
if(TARGET gtest)
    # ThreadWrapper tests
    add_executable(thread_wrapper_test thread_wrapper_test.cpp)
    target_link_libraries(thread_wrapper_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    add_test(NAME ThreadWrapperTest COMMAND thread_wrapper_test)

    # ThreadWrapperView tests
    add_executable(thread_wrapper_view_test thread_wrapper_view_test.cpp)
    target_link_libraries(thread_wrapper_view_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    add_test(NAME ThreadWrapperViewTest COMMAND thread_wrapper_view_test)

    # ThreadConfig tests (Priority, Affinity, SchedulingPolicy)
    add_executable(thread_config_test thread_config_test.cpp)
    target_link_libraries(thread_config_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    add_test(NAME ThreadConfigTest COMMAND thread_config_test)

    # ThreadPool tests
    add_executable(thread_pool_test thread_pool_test.cpp)
    target_link_libraries(thread_pool_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    add_test(NAME ThreadPoolTest COMMAND thread_pool_test)

    # Expected tests
    add_executable(expected_test expected_test.cpp)
    target_link_libraries(expected_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    add_test(NAME ExpectedTest COMMAND expected_test)

    # ThreadRegistry tests
    add_executable(thread_registry_test thread_registry_test.cpp)
    target_link_libraries(thread_registry_test 
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    add_test(NAME ThreadRegistryTest COMMAND thread_registry_test)

    # Set test properties for better output
    set_tests_properties(ThreadWrapperTest ThreadWrapperViewTest ThreadConfigTest ThreadPoolTest
        PROPERTIES
        TIMEOUT 120
    )
endif() 
