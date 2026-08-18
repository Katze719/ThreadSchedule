#pragma once

/**
 * @file result.hpp
 * @brief Error-returning result type used by the portable ThreadSchedule API.
 */

#include "expected.hpp"

#include <system_error>

namespace threadschedule
{

/**
 * @brief Standard result type used by public APIs.
 * @tparam T Value type on success.
 *
 * On failure the error channel contains @c std::error_code.
 */
template <typename T>
using result = expected<T, std::error_code>;

} // namespace threadschedule
