#pragma once

/**
 * @file thread.hpp
 * @brief Owning standard-style thread with portable scheduling controls.
 *
 * `threadschedule::thread` follows `std::thread` construction, ownership, and callable forwarding. Native-control
 * failures use `result<T>`, while explicit `*_or_throw` operations retain standard throwing semantics. Configured
 * construction releases the user callable only after every requested setting has been applied successfully.
 */

#include "detail/thread/thread.hpp"
