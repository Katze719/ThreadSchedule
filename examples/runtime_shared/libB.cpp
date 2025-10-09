#include <chrono>
#include <threadschedule/thread_registry.hpp>
#include <threadschedule/thread_wrapper.hpp>

using namespace threadschedule;

extern "C"
#ifdef _WIN32
    __declspec(dllexport)
#endif
    void
    libB_start()
{
    ThreadWrapper t([] {
        AutoRegisterCurrentThread guard("rt-b1", "B");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
    t.detach();
}
