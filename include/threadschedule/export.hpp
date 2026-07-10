#pragma once

/**
 * @file export.hpp
 * @brief Shared export and ABI feature macros for ThreadSchedule.
 */

#if defined(_WIN32) || defined(_WIN64)
#    if defined(THREADSCHEDULE_EXPORTS)
#        define THREADSCHEDULE_API __declspec(dllexport)
#    else
#        define THREADSCHEDULE_API __declspec(dllimport)
#    endif
#elif defined(__GNUC__) || defined(__clang__)
#    define THREADSCHEDULE_API __attribute__((visibility("default")))
#else
#    define THREADSCHEDULE_API
#endif

#if defined(__has_cpp_attribute)
#    if __has_cpp_attribute(deprecated)
#        define THREADSCHEDULE_DEPRECATED(msg) [[deprecated(msg)]]
#    else
#        define THREADSCHEDULE_DEPRECATED(msg)
#    endif
#else
#    define THREADSCHEDULE_DEPRECATED(msg)
#endif

#if defined(THREADSCHEDULE_STABLE_ABI_STRICT) && !defined(THREADSCHEDULE_STABLE_ABI)
#    define THREADSCHEDULE_STABLE_ABI 1
#endif

#if defined(THREADSCHEDULE_RUNTIME) && defined(THREADSCHEDULE_STABLE_ABI) && \
    !defined(THREADSCHEDULE_STABLE_ABI_STRICT) && !defined(THREADSCHEDULE_INTERNAL_RUNTIME_BUILD)
#    define THREADSCHEDULE_RUNTIME_ABI_UNSAFE_DEPRECATED(msg) THREADSCHEDULE_DEPRECATED(msg)
#else
#    define THREADSCHEDULE_RUNTIME_ABI_UNSAFE_DEPRECATED(msg)
#endif
