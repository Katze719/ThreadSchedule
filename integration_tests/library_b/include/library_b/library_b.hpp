#pragma once

#include <threadschedule/thread_registry.hpp>

// Export macro for Windows DLL
#if defined(_WIN32) || defined(_WIN64)
#ifdef LIBRARY_B_EXPORTS
#define LIBRARY_B_API __declspec(dllexport)
#else
#define LIBRARY_B_API __declspec(dllimport)
#endif
#else
#define LIBRARY_B_API
#endif

namespace library_b
{

/**
 * @brief Get the registry used by library B
 */
LIBRARY_B_API auto get_registry() -> threadschedule::ThreadRegistry&;

/**
 * @brief Set an external registry for library B to use
 * @param reg Pointer to the external registry
 */
LIBRARY_B_API void set_registry(threadschedule::ThreadRegistry* reg);

/**
 * @brief Start a worker thread in library B
 * @param name Name for the thread
 */
LIBRARY_B_API void start_worker(char const* name);

/**
 * @brief Wait for all library B threads to complete
 */
LIBRARY_B_API void wait_for_threads();

/**
 * @brief Get the number of threads currently registered in library B
 */
LIBRARY_B_API auto get_thread_count() -> int;

} // namespace library_b
