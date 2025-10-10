#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

int main()
{
    std::cout << "=== Error Handling Example ===\n\n";

    // Create a thread pool with error handling
    HighPerformancePoolWithErrors pool(4);
    pool.configure_threads("error_handler");

    // Add a global error callback
    std::cout << "1. Adding global error callback:\n";
    pool.add_error_callback([](TaskError const& error) {
        std::cout << "   [ERROR HANDLER] Caught exception in task: " << error.what() << "\n";
        std::cout << "   [ERROR HANDLER] Thread ID: " << error.thread_id << "\n";
        if (!error.task_description.empty())
        {
            std::cout << "   [ERROR HANDLER] Task description: " << error.task_description << "\n";
        }
    });

    // Submit a task that throws an exception
    std::cout << "\n2. Submitting task that throws std::runtime_error:\n";
    auto future1 = pool.submit([]() {
        std::cout << "   -> Task is running...\n";
        throw std::runtime_error("Something went wrong!");
        return 42;
    });

    try
    {
        future1.get();
    }
    catch (std::exception const& e)
    {
        std::cout << "   -> Exception caught in main: " << e.what() << "\n";
    }

    // Submit a task with a description
    std::cout << "\n3. Submitting task with description:\n";
    auto future2 = pool.submit_with_description("Database Query", []() {
        throw std::runtime_error("Connection timeout");
        return std::string("result");
    });

    try
    {
        future2.get();
    }
    catch (std::exception const& e)
    {
        std::cout << "   -> Exception caught in main: " << e.what() << "\n";
    }

    // Use per-future error callback
    std::cout << "\n4. Using per-future error callback:\n";
    auto future3_temp = pool.submit([]() {
        throw std::logic_error("Logic error occurred");
        return 100;
    });
    future3_temp.on_error([](std::exception_ptr const& eptr) {
        try
        {
            std::rethrow_exception(eptr);
        }
        catch (std::exception const& e)
        {
            std::cout << "   [FUTURE ERROR] Handled in future callback: " << e.what() << "\n";
        }
    });

    try
    {
        future3_temp.get();
    }
    catch (std::exception const& e)
    {
        std::cout << "   -> Exception caught in main: " << e.what() << "\n";
    }

    // Task that succeeds
    std::cout << "\n5. Submitting successful task (no error):\n";
    auto future4 = pool.submit([]() {
        std::cout << "   -> Task executed successfully!\n";
        return 42;
    });
    std::cout << "   -> Result: " << future4.get() << "\n";

    // Multiple tasks with errors
    std::cout << "\n6. Submitting multiple tasks with errors:\n";
    std::vector<FutureWithErrorHandler<int>> futures;
    futures.reserve(5);

    for (int i = 0; i < 5; i++)
    {
        futures.push_back(pool.submit([i]() {
            if (i % 2 == 0)
            {
                throw std::runtime_error("Task " + std::to_string(i) + " failed");
            }
            std::cout << "   -> Task " << i << " succeeded\n";
            return i * 10;
        }));
    }

    // Collect results
    std::cout << "\n7. Collecting results:\n";
    for (size_t i = 0; i < futures.size(); i++)
    {
        try
        {
            int result = futures[i].get();
            std::cout << "   -> Task " << i << " result: " << result << "\n";
        }
        catch (std::exception const& e)
        {
            std::cout << "   -> Task " << i << " failed: " << e.what() << "\n";
        }
    }

    // Show error statistics
    std::cout << "\n=== Error Statistics ===\n";
    std::cout << "Total errors caught: " << pool.error_count() << "\n";

    // Test with FastThreadPool
    std::cout << "\n8. Testing with FastThreadPoolWithErrors:\n";
    FastThreadPoolWithErrors fast_pool(2);

    fast_pool.add_error_callback(
        [](TaskError const& error) { std::cout << "   [FAST POOL ERROR] " << error.what() << "\n"; });

    auto fast_future = fast_pool.submit([]() { throw std::runtime_error("Fast pool error!"); });

    try
    {
        fast_future.get();
    }
    catch (...)
    {
        std::cout << "   -> Fast pool exception handled\n";
    }

    // Test with ThreadPoolWithErrors
    std::cout << "\n9. Testing with ThreadPoolWithErrors:\n";
    ThreadPoolWithErrors simple_pool(2);

    simple_pool.add_error_callback(
        [](TaskError const& error) { std::cout << "   [SIMPLE POOL ERROR] " << error.what() << "\n"; });

    auto simple_future = simple_pool.submit([]() { throw std::invalid_argument("Invalid argument!"); });

    try
    {
        simple_future.get();
    }
    catch (...)
    {
        std::cout << "   -> Simple pool exception handled\n";
    }

    // Using ErrorHandler directly
    std::cout << "\n10. Using ErrorHandler directly:\n";
    ErrorHandler handler;
    handler.add_callback([](TaskError const& error) { std::cout << "   [CUSTOM HANDLER] " << error.what() << "\n"; });

    TaskError custom_error;
    custom_error.exception = std::make_exception_ptr(std::runtime_error("Custom error"));
    custom_error.task_description = "Custom Task";
    custom_error.thread_id = std::this_thread::get_id();
    custom_error.timestamp = std::chrono::steady_clock::now();

    handler.handle_error(custom_error);

    std::cout << "\n=== Final Statistics ===\n";
    std::cout << "High-performance pool errors: " << pool.error_count() << "\n";
    std::cout << "Fast pool errors: " << fast_pool.error_count() << "\n";
    std::cout << "Simple pool errors: " << simple_pool.error_count() << "\n";
    std::cout << "Custom handler errors: " << handler.error_count() << "\n";

    std::cout << "\nShutting down...\n";
    pool.shutdown();
    fast_pool.shutdown();
    simple_pool.shutdown();

    std::cout << "Done!\n";
    return 0;
}
