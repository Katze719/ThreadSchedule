#pragma once

#include <threadschedule/thread_registry.hpp>

// Export macro for Windows DLL
#if defined(_WIN32) || defined(_WIN64)
#ifdef LIBRARY_A_EXPORTS
#define LIBRARY_A_API __declspec(dllexport)
#else
#define LIBRARY_A_API __declspec(dllimport)
#endif
#else
#define LIBRARY_A_API
#endif

namespace library_a
{

/**
 * @brief Get the registry used by library A
 */
LIBRARY_A_API auto get_registry() -> threadschedule::ThreadRegistry&;

/**
 * @brief Set an external registry for library A to use
 * @param reg Pointer to the external registry
 */
LIBRARY_A_API void set_registry(threadschedule::ThreadRegistry* reg);

/**
 * @brief Start a worker thread in library A
 * @param name Name for the thread
 */
LIBRARY_A_API void start_worker(char const* name);

/**
 * @brief Wait for all library A threads to complete
 */
LIBRARY_A_API void wait_for_threads();

/**
 * @brief Get the number of threads currently registered in library A
 */
LIBRARY_A_API auto get_thread_count() -> int;

} // namespace library_a
