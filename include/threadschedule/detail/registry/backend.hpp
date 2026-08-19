#pragma once

/**
 * @file thread_registry.hpp
 * @brief Process-wide thread registry, control blocks, and composite registry.
 */

#include "../../expected.hpp"
#include "../../export.hpp"
#include "../callable/copyable_function.hpp"
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
#  include "../windows_api.hpp"
#else
#  include <pthread.h>
#  include <sched.h>
#  include <sys/types.h>
#endif

// These fragments form a dependency chain; keep this order.
#include "composite_thread_registry_backend.hpp"
#include "query_facade_mixin.hpp"
#include "registration_guard_backend.hpp"
#include "thread_control_block.hpp"
#include "thread_registry_backend.hpp"
