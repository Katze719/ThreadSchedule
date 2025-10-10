#include <chrono>
#include <iostream>
#include <thread>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;
using namespace std::chrono_literals;

int main()
{
    std::cout << "=== Scheduled Thread Pool Example ===\n\n";

    // Create a scheduled thread pool with 4 worker threads
    ScheduledThreadPool scheduler(4);
    scheduler.configure_threads("scheduler");

    std::cout << "1. Schedule a one-time task after 2 seconds:\n";
    auto handle1 = scheduler.schedule_after(2s, []() { std::cout << "   -> Task executed after 2 seconds!\n"; });

    std::cout << "2. Schedule a task at a specific time (3 seconds from now):\n";
    auto future_time = std::chrono::steady_clock::now() + 3s;
    auto handle2 = scheduler.schedule_at(future_time, []() { std::cout << "   -> Task executed at specific time!\n"; });

    std::cout << "3. Schedule a periodic task every 1 second:\n";
    int counter = 0;
    auto handle3 = scheduler.schedule_periodic(1s, [&counter]() {
        counter++;
        std::cout << "   -> Periodic task #" << counter << " executed\n";
    });

    std::cout << "4. Schedule a periodic task with initial delay (starts after 2s, runs every 500ms):\n";
    int delayed_counter = 0;
    auto handle4 = scheduler.schedule_periodic_after(2s, 500ms, [&delayed_counter]() {
        delayed_counter++;
        std::cout << "   -> Delayed periodic task #" << delayed_counter << " executed\n";
    });

    std::cout << "\nWaiting for tasks to execute...\n\n";
    std::this_thread::sleep_for(5s);

    std::cout << "\n5. Cancelling periodic task:\n";
    ScheduledThreadPool::cancel(handle3);
    std::cout << "   -> Main periodic task cancelled\n";

    std::cout << "\nWaiting 2 more seconds...\n";
    std::this_thread::sleep_for(2s);

    std::cout << "\n6. Cancelling delayed periodic task:\n";
    ScheduledThreadPool::cancel(handle4);
    std::cout << "   -> Delayed periodic task cancelled\n";

    std::cout << "\n7. Scheduled task count: " << scheduler.scheduled_count() << "\n";

    // Schedule a batch of tasks
    std::cout << "\n8. Scheduling multiple one-time tasks:\n";
    for (int i = 0; i < 3; i++)
    {
        scheduler.schedule_after(std::chrono::milliseconds(100 * i),
                                 [i]() { std::cout << "   -> Batch task #" << i << " executed\n"; });
    }

    std::this_thread::sleep_for(1s);

    // You can also submit tasks directly to the underlying thread pool
    std::cout << "\n9. Direct task submission to worker pool:\n";
    auto future = scheduler.thread_pool().submit([]() {
        std::cout << "   -> Direct task executed immediately\n";
        return 42;
    });
    std::cout << "   -> Direct task result: " << future.get() << "\n";

    // Statistics
    auto stats = scheduler.thread_pool().get_statistics();
    std::cout << "\n=== Statistics ===\n";
    std::cout << "Worker threads: " << stats.total_threads << "\n";
    std::cout << "Completed tasks: " << stats.completed_tasks << "\n";
    std::cout << "Pending tasks: " << stats.pending_tasks << "\n";

    std::cout << "\nShutting down...\n";
    scheduler.shutdown();

    std::cout << "Done!\n";
    return 0;
}
