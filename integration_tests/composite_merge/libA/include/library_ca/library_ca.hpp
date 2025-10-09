#pragma once
#include <threadschedule/thread_registry.hpp>

#if defined(_WIN32) && defined(BUILD_COMPOSITE_LIBA_SHARED)
#define COMPOSITE_LIBA_API __declspec(dllexport)
#else
#define COMPOSITE_LIBA_API
#endif

namespace composite_libA
{
COMPOSITE_LIBA_API void start_worker(char const* name);
COMPOSITE_LIBA_API void wait_for_threads();
COMPOSITE_LIBA_API auto get_registry() -> threadschedule::ThreadRegistry&;
} // namespace composite_libA
