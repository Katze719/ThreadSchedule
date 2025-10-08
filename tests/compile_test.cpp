#include "../include/threadschedule/threadschedule.hpp"
#include <iostream>

int main()
{
    std::cout << "ThreadSchedule library compilation test passed!" << std::endl;

    // Test basic type instantiation
    threadschedule::ThreadPriority priority;
    threadschedule::ThreadAffinity affinity;
    threadschedule::SchedulingPolicy policy = threadschedule::SchedulingPolicy::OTHER;

    std::cout << "Priority: " << priority.to_string() << std::endl;
    std::cout << "Affinity: " << affinity.to_string() << std::endl;
    std::cout << "Policy: " << threadschedule::to_string(policy) << std::endl;

    return 0;
}
