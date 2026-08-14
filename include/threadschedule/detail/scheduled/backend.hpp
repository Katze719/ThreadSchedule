#pragma once

/**
 * @file scheduled_pool.hpp
 * @brief Delayed and periodic task scheduling on top of any pool type.
 */

#include "../../expected.hpp"
#include "../pool/backend.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

// The scheduler owns task-state values declared by the first fragment.
#include "scheduled_pool_backend_base.hpp"
#include "scheduled_task_backend.hpp"
