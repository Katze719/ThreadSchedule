#pragma once

/**
 * @file scheduled_pool.hpp
 * @brief Portable delayed and periodic task scheduler.
 *
 * Fixed-rate periodic tasks never overlap with themselves; missed deadlines are skipped instead of building backlog.
 * Worker and scheduler configuration remains transactional at construction.
 */

#include "detail/scheduled/public_impl.hpp"
