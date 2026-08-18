#pragma once

#include "../callable/copyable_function.hpp"

#include <chrono>
#include <thread>

namespace threadschedule::detail
{

using task_start_callback = copyable_function<void(std::chrono::steady_clock::time_point, std::thread::id)>;
using task_end_callback = copyable_function<void(std::chrono::steady_clock::time_point, std::thread::id,
                                                 std::chrono::microseconds elapsed)>;

using task_start_callback_storage = task_start_callback;
using task_end_callback_storage = task_end_callback;

} // namespace threadschedule::detail
