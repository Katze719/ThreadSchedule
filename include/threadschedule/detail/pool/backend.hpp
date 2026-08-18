#pragma once

/**
 * @file thread_pool.hpp
 * @brief Thread pools: work_stealing_pool_backend, thread_pool_backend_base,
 * lightweight_pool_backend_base, and global_pool_backend.
 */

#include "../../expected.hpp"
#include "../callable/bind.hpp"
#include "../callable/move_only_function.hpp"
#include "../registry/backend.hpp"
#include "../scheduling/native.hpp"
#include "../thread_backend.hpp"
#include "callbacks.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <shared_mutex>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <vector>

// These implementation fragments form a dependency chain; keep this order.
#include "global_pool_backend.hpp"
#include "indefinite_wait.hpp"
#include "lightweight_pool_backend_base.hpp"
#include "polling_wait.hpp"
#include "thread_pool_backend_base.hpp"
#include "work_stealing_pool_backend.hpp"
#include "worker_context_guard.hpp"
