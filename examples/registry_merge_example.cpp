#include <chrono>
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;

int main()
{
    ThreadRegistry regA;
    ThreadRegistry regB;

    // Spawn threads that register into distinct registries
    ThreadWrapper t1([&] {
        AutoRegisterCurrentThread guard(regA, "a-1", "A");
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    });
    ThreadWrapper t2([&] {
        AutoRegisterCurrentThread guard(regB, "b-1", "B");
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Merge via composite view
    CompositeThreadRegistry composite;
    composite.attach(&regA);
    composite.attach(&regB);

    // Apply an operation across both
    composite.apply_all(
        [](const RegisteredThreadInfo &e) { return e.componentTag == "A" || e.componentTag == "B"; },
        [&](const RegisteredThreadInfo &e) { (void)registry().set_priority(e.tid, ThreadPriority{0}); });

    t1.join();
    t2.join();
    return 0;
}
