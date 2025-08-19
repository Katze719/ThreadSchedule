#include "../include/threadschedule/threadschedule.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace threadschedule;

void worker_task(int worker_id) {
  std::cout << "Worker " << worker_id << " starting" << std::endl;

  // Simulate some work
  std::this_thread::sleep_for(std::chrono::milliseconds(100 * worker_id));

  std::cout << "Worker " << worker_id << " completed" << std::endl;
}

int main() {
  std::cout << "=== ThreadSchedule Basic Example ===" << std::endl;
  std::cout << "Hardware concurrency: " << std::thread::hardware_concurrency()
            << std::endl;

  // Create thread wrappers with names
  std::vector<ThreadWrapper> workers;

  for (int i = 0; i < 4; ++i) {
    // Create thread with task
    ThreadWrapper worker(worker_task, i);

    // Set thread name
    std::string thread_name = "worker_" + std::to_string(i);
    if (worker.set_name(thread_name)) {
      std::cout << "Set thread name: " << thread_name << std::endl;
    }

    // Set priority (normal priority with slight variations)
    ThreadPriority priority(i - 2); // Range from -2 to 1
    if (worker.set_priority(priority)) {
      std::cout << "Set priority for " << thread_name << ": "
                << priority.to_string() << std::endl;
    }

    // Set CPU affinity (distribute across available CPUs)
    ThreadAffinity affinity(
        {static_cast<int>(i % std::thread::hardware_concurrency())});
    if (worker.set_affinity(affinity)) {
      std::cout << "Set affinity for " << thread_name << ": "
                << affinity.to_string() << std::endl;
    }

    workers.push_back(std::move(worker));
  }

  std::cout << "\nWaiting for all workers to complete...\n" << std::endl;

  // All threads will be automatically joined when destructors are called

  std::cout << "\n=== Factory Method Example ===" << std::endl;

  // Using factory method for preconfigured threads
  auto configured_thread = ThreadWrapper::create_with_config(
      "configured_worker", SchedulingPolicy::OTHER, ThreadPriority::normal(),
      []() {
        std::cout << "Configured thread running" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "Configured thread completed" << std::endl;
      });

  std::cout << "\n=== Example completed ===" << std::endl;

  return 0;
}
