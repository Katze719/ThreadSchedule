#pragma once

/** @file detail/pool/shutdown_policy_backend.hpp
 *  @brief Internal queued-task shutdown behavior.
 */

enum class shutdown_policy_backend : uint8_t
{
  drain,
  drop_pending
};
