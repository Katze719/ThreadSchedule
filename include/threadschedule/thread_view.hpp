#pragma once

/**
 * @file thread_view.hpp
 * @brief Non-owning portable control view over a standard thread.
 *
 * The referenced `std::thread` must outlive the view. The view can configure name, priority, nice, and affinity, but
 * never joins, detaches, or changes ownership.
 */

#include "detail/thread/thread_view.hpp"
