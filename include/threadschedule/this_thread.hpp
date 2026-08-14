#pragma once

/**
 * @file this_thread.hpp
 * @brief Portable configuration operations for the calling thread.
 *
 * The functions mirror the control operations on `thread` and report permission, range, and platform failures through
 * `result<T>`.
 */

#include "detail/thread/this_thread.hpp"
