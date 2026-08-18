#pragma once

/**
 * @file export.hpp
 * @brief Export macros for ThreadSchedule's optional C++ runtime.
 */

/**
 * @def THREADSCHEDULE_API
 * @brief Symbol visibility/import-export marker for runtime builds.
 */

#if defined(_WIN32) || defined(_WIN64)
#  if defined(THREADSCHEDULE_EXPORTS)
#    define THREADSCHEDULE_API __declspec(dllexport)
#  else
#    define THREADSCHEDULE_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define THREADSCHEDULE_API __attribute__((visibility("default")))
#else
#  define THREADSCHEDULE_API
#endif

/**
 * @def THREADSCHEDULE_DEPRECATED
 * @brief Portable deprecation attribute macro.
 */
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(deprecated)
#    define THREADSCHEDULE_DEPRECATED(msg) [[deprecated(msg)]]
#  else
#    define THREADSCHEDULE_DEPRECATED(msg)
#  endif
#else
#  define THREADSCHEDULE_DEPRECATED(msg)
#endif
