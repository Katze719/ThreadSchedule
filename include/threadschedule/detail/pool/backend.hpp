#pragma once

/**
 * @file thread_pool.hpp
 * @brief Thread pools: work_stealing_pool_backend, thread_pool_backend_base,
 * lightweight_pool_backend_base, and global_pool_backend.
 */

#include "../../expected.hpp"
#include "../callable/bind.hpp"
#include "../callable/storage.hpp"
#include "../registry/backend.hpp"
#include "../scheduling/native.hpp"
#include "../thread_backend.hpp"
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

namespace threadschedule::detail
{

// These implementation fragments form a dependency chain; keep this order.
// clang-format off
#include "worker_context.hpp"
#include "callable.hpp"
#include "work_stealing.hpp"
#include "wait_policy.hpp"
#include "shared_pool.hpp"
#include "lightweight.hpp"
#include "global.hpp"
// clang-format on

} // namespace threadschedule::detail
