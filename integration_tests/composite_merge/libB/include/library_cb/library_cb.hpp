#pragma once

#if defined(_WIN32) && defined(BUILD_COMPOSITE_LIBB_SHARED)
#define COMPOSITE_LIBB_API __declspec(dllexport)
#else
#define COMPOSITE_LIBB_API
#endif

namespace composite_libB
{
COMPOSITE_LIBB_API void start_worker(char const* name);
COMPOSITE_LIBB_API void wait_for_threads();
} // namespace composite_libB
