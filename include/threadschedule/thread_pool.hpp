#pragma once

/**
 * @file thread_pool.hpp
 * @brief Portable error-returning thread pool.
 *
 * Queue and wait policies are intentionally hidden. Use `threadschedule/advanced/pools.hpp` only when an application
 * explicitly needs a specialized implementation.
 */

#include "detail/pool/public_impl.hpp"
