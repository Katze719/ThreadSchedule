#pragma once

/**
 * @file result.hpp
 * @brief Error-returning result type used by the portable ThreadSchedule API.
 */

#include "expected.hpp"

#include <system_error>

namespace threadschedule
{

template <typename T>
using result = expected<T, std::error_code>;

} // namespace threadschedule
