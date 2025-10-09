#include <threadschedule/thread_registry.hpp>

namespace threadschedule
{

static ThreadRegistry* g_external = nullptr;
static ThreadRegistry g_local;

THREADSCHEDULE_API auto registry() -> ThreadRegistry&
{
    return (g_external != nullptr) ? *g_external : g_local;
}

THREADSCHEDULE_API void set_external_registry(ThreadRegistry* reg)
{
    g_external = reg;
}

} // namespace threadschedule
