#pragma once

/**
 * @file detail/lifetime.hpp
 * @brief Optional compiler diagnostics for borrowed references and pointers.
 *
 * These annotations do not extend lifetimes or change storage or ownership.
 * noescape is also an optimizer contract: only use it when the function cannot
 * retain or return a reference derived from the parameter, or deallocate it.
 */

#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::lifetimebound)
#    define THREADSCHEDULE_LIFETIMEBOUND [[clang::lifetimebound]]
#  endif
#  if __has_cpp_attribute(gsl::Pointer)
#    define THREADSCHEDULE_POINTER [[gsl::Pointer]]
#  endif
#  if __has_cpp_attribute(gsl::Owner)
#    define THREADSCHEDULE_OWNER [[gsl::Owner]]
#  endif
#  if __has_cpp_attribute(clang::noescape)
#    define THREADSCHEDULE_NOESCAPE [[clang::noescape]]
#  endif
#endif

#ifndef THREADSCHEDULE_LIFETIMEBOUND
#  define THREADSCHEDULE_LIFETIMEBOUND
#endif

#ifndef THREADSCHEDULE_POINTER
#  define THREADSCHEDULE_POINTER
#endif
#ifndef THREADSCHEDULE_OWNER
#  define THREADSCHEDULE_OWNER
#endif
#ifndef THREADSCHEDULE_NOESCAPE
#  define THREADSCHEDULE_NOESCAPE
#endif
