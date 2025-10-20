#pragma once

#if defined(_WIN32) && defined(BUILD_RUNTIME_LIBA_SHARED)
#define RUNTIME_LIBA_API __declspec(dllexport)
#else
#define RUNTIME_LIBA_API
#endif

namespace runtime_libA
{
RUNTIME_LIBA_API void start_worker(char const* name);
RUNTIME_LIBA_API void wait_for_threads();
} // namespace runtime_libA
