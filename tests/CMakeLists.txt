# Tests CMakeLists.txt

# Simple test to verify compilation (keep for backwards compatibility)
add_executable(compile_test compile_test.cpp)
target_link_libraries(compile_test ThreadSchedule::ThreadSchedule)
add_test(NAME CompileTest COMMAND compile_test)

# Every documented header is compiled in a fresh translation unit so an
# accidental dependency on include order is caught immediately.
set(THREADSCHEDULE_SELF_CONTAINED_HEADERS
    threadschedule/expected.hpp
    threadschedule/result.hpp
    threadschedule/scheduling.hpp
    threadschedule/thread_affinity.hpp
    threadschedule/thread_config.hpp
    threadschedule/thread.hpp
    threadschedule/jthread.hpp
    threadschedule/thread_view.hpp
    threadschedule/this_thread.hpp
    threadschedule/thread_registry.hpp
    threadschedule/thread_pool.hpp
    threadschedule/scheduled_task.hpp
    threadschedule/scheduled_pool.hpp
    threadschedule/task_error.hpp
    threadschedule/shutdown_policy.hpp
    threadschedule/runtime.hpp
    threadschedule/core.hpp
    threadschedule/threadschedule.hpp
    threadschedule/advanced/cgroup.hpp
    threadschedule/advanced/chaos_controller.hpp
    threadschedule/advanced/composite_thread_registry.hpp
    threadschedule/advanced/cpu_topology.hpp
    threadschedule/advanced/error_handler.hpp
    threadschedule/advanced/futures.hpp
    threadschedule/advanced/native_thread.hpp
    threadschedule/advanced/pools.hpp
    threadschedule/advanced/thread_profile.hpp
    threadschedule/advanced/task_group.hpp
    threadschedule/advanced.hpp
)

foreach(header IN LISTS THREADSCHEDULE_SELF_CONTAINED_HEADERS)
    string(MAKE_C_IDENTIFIER "${header}" header_id)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/header_self_containment/${header_id}.cpp")
    file(GENERATE OUTPUT "${source}" CONTENT "#include <${header}>\n")
    add_library("header_compile_${header_id}" OBJECT "${source}")
    target_link_libraries("header_compile_${header_id}" PRIVATE ThreadSchedule::ThreadSchedule)
endforeach()

# Google Test based tests
if(TARGET gtest)
    include(GoogleTest)
    # Avoid parallel POST_BUILD discovery racing on a shared JSON file in
    # CMake 4.4. PRE_TEST also ensures Windows runtime dependencies are ready.
    set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

    # detail::thread_backend tests
    add_executable(callable_test callable_test.cpp)
    target_link_libraries(callable_test
        ThreadSchedule::ThreadSchedule
        gtest
        gtest_main
    )
    gtest_discover_tests(callable_test
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    add_executable(thread_backend_test thread_backend_test.cpp)
    target_link_libraries(thread_backend_test
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    gtest_discover_tests(thread_backend_test
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    # detail::thread_view_backend tests
    add_executable(thread_view_backend_test thread_view_backend_test.cpp)
    target_link_libraries(thread_view_backend_test
        ThreadSchedule::ThreadSchedule 
        gtest 
        gtest_main
    )
    gtest_discover_tests(thread_view_backend_test
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    # native_thread_config tests (Priority, Affinity, native_scheduling_policy)
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

    # thread_pool_backend tests
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

    add_executable(v3_api_test v3_api_test.cpp)
    target_link_libraries(v3_api_test
        ThreadSchedule::ThreadSchedule
        gtest
        gtest_main
    )
    gtest_discover_tests(v3_api_test
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    add_executable(advanced_api_test advanced_api_test.cpp)
    target_link_libraries(advanced_api_test
        ThreadSchedule::ThreadSchedule
        gtest
        gtest_main
    )
    gtest_discover_tests(advanced_api_test
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

    # thread_registry_backend tests
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

    # Internal pool backend tests
    add_executable(pool_backend_test pool_backend_test.cpp)
    target_link_libraries(pool_backend_test
        ThreadSchedule::ThreadSchedule
        gtest
        gtest_main
    )
    gtest_discover_tests(pool_backend_test
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    # Future combinator tests
    add_executable(futures_test futures_test.cpp)
    target_link_libraries(futures_test
        ThreadSchedule::ThreadSchedule
        gtest
        gtest_main
    )
    gtest_discover_tests(futures_test
        DISCOVERY_TIMEOUT 60
        PROPERTIES TIMEOUT 120
    )

    # Registry query API tests
    add_executable(registry_query_test registry_query_test.cpp)
    target_link_libraries(registry_query_test
        ThreadSchedule::ThreadSchedule
        gtest
        gtest_main
    )
    gtest_discover_tests(registry_query_test
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
