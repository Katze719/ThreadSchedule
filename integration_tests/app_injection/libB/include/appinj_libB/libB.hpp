#pragma once

#if defined(_WIN32) && defined(BUILD_APPINJ_LIBB_SHARED)
#define APPINJ_LIBB_API __declspec(dllexport)
#else
#define APPINJ_LIBB_API
#endif

namespace appinj_libB
{
APPINJ_LIBB_API void start_worker(char const* name);
APPINJ_LIBB_API void wait_for_threads();
} // namespace appinj_libB
