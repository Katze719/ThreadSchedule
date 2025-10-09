#include <chrono>
#include <threadschedule/registered_threads.hpp>
#include <threadschedule/thread_registry.hpp>

using namespace threadschedule;

int main()
{
    // App creates and injects a global registry
    ThreadRegistry appReg;
    set_external_registry(&appReg);

    ThreadWrapper t([] {
        AutoRegisterCurrentThread guard("ext-1", "ext");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    appReg.apply([](RegisteredThreadInfo const& e) { return e.componentTag == "ext"; },
                 [&](RegisteredThreadInfo const& e) { (void)appReg.set_priority(e.tid, ThreadPriority{0}); });

    t.join();
    return 0;
}
