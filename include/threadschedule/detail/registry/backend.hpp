#pragma once

/**
 * @file thread_registry.hpp
 * @brief Process-wide thread registry, control blocks, and composite registry.
 */

#include "../../expected.hpp"
#include "../../export.hpp"
#include "../callable/storage.hpp"
#include "../scheduling/native.hpp"
#include "../thread_backend.hpp"
#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <pthread.h>
#  include <sched.h>
#  include <sys/types.h>
#endif

namespace threadschedule
{
class auto_register_current_thread;
}

namespace threadschedule::detail
{

#include "control_block.hpp"
#include "query.hpp"
#include "storage.hpp"

} // namespace threadschedule::detail

#include "composite.hpp"
#include "registration.hpp"
