#include <iostream>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;

extern "C" void libA_start();
extern "C" void libB_start();

int main()
{
    // All components link ThreadSchedule::Runtime -> single process-wide registry
    libA_start();
    libB_start();

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    int count = 0;
    registry().for_each([&](RegisteredThreadInfo const& e) {
        std::cout << "thread: " << e.name << " tag=" << e.componentTag << "\n";
        count++;
    });

    std::cout << "total=" << count << "\n";
    return 0;
}
