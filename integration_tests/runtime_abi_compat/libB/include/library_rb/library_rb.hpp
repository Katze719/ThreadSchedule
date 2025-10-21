#pragma once

#if defined(_WIN32) && defined(BUILD_RUNTIME_LIBB_SHARED)
#define RUNTIME_LIBB_API __declspec(dllexport)
#else
#define RUNTIME_LIBB_API
#endif

namespace runtime_libB
{
RUNTIME_LIBB_API void start_worker(char const* name);
RUNTIME_LIBB_API void wait_for_threads();
} // namespace runtime_libB
