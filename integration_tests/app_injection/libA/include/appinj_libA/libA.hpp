#pragma once
#include <threadschedule/thread_registry.hpp>

#if defined(_WIN32) && defined(BUILD_APPINJ_LIBA_SHARED)
#define APPINJ_LIBA_API __declspec(dllexport)
#else
#define APPINJ_LIBA_API
#endif

namespace appinj_libA
{
APPINJ_LIBA_API void set_registry(threadschedule::ThreadRegistry* reg);
APPINJ_LIBA_API void start_worker(char const* name);
APPINJ_LIBA_API void wait_for_threads();
} // namespace appinj_libA
