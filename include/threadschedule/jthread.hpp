#pragma once

/**
 * @file jthread.hpp
 * @brief C++20 joining thread with stop-token injection and portable controls.
 *
 * `threadschedule::jthread` is available only when the standard library provides `std::jthread`. It preserves
 * stop-token injection, move-only arguments, and transactional configured startup without a C++17 emulation.
 */

#include "detail/thread/jthread.hpp"
