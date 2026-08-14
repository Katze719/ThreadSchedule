#pragma once

/**
 * @file thread_registry.hpp
 * @brief Portable registry for naming, inspecting, and configuring live threads.
 *
 * Snapshots are independent values and expose no backend records or native handles. `global_registry()` is a stable
 * facade, while `use_global_registry()` changes its backing storage for application injection and shared runtimes.
 * `auto_register_current_thread` accepts either that facade or an explicit `thread_registry&`.
 */

#include "detail/registry/thread_registry.hpp"
