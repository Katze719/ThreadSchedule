#pragma once

/**
 * @file task_error.hpp
 * @brief Portable task failure reporting.
 */

#include <chrono>
#include <exception>
#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace threadschedule
{

/**
 * @brief Captured information about a task failure.
 */
struct task_error
{
  /** @brief Original exception object captured from the task. */
  std::exception_ptr exception;
  /** @brief Optional textual description of the failing task. */
  std::string task_description;
  /** @brief std::thread id where the failure occurred. */
  std::thread::id std_id;
  /** @brief Capture timestamp using @c steady_clock. */
  std::chrono::steady_clock::time_point timestamp;

  /**
   * @brief Capture failure context for the current exception state.
   * @param description Optional task description string.
   */
  [[nodiscard]] static auto
  capture(std::string description = {}) -> task_error
  {
    return { std::current_exception(), std::move(description), std::this_thread::get_id(),
             std::chrono::steady_clock::now() };
  }

  /**
   * @brief Return a printable error message if exception derives from std::exception.
   * @return Exception message, "Unknown exception", or "No exception".
   */
  [[nodiscard]] auto
  what() const -> std::string
  {
    try
      {
        if (exception)
          std::rethrow_exception(exception);
      }
    catch (std::exception const& error)
      {
        return error.what();
      }
    catch (...)
      {
        return "Unknown exception";
      }
    return "No exception";
  }

  /**
   * @brief Rethrow captured exception if present.
   */
  void
  rethrow() const
  {
    if (exception)
      std::rethrow_exception(exception);
  }
};

/** @brief Callback signature for asynchronous task error reporting. */
using error_callback = std::function<void(task_error const&)>;

} // namespace threadschedule
