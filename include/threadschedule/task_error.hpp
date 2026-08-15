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

struct task_error
{
  std::exception_ptr exception;
  std::string task_description;
  std::thread::id std_id;
  std::chrono::steady_clock::time_point timestamp;

  [[nodiscard]] static auto
  capture(std::string description = {}) -> task_error
  {
    return { std::current_exception(), std::move(description), std::this_thread::get_id(),
             std::chrono::steady_clock::now() };
  }

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

  void
  rethrow() const
  {
    if (exception)
      std::rethrow_exception(exception);
  }
};

using error_callback = std::function<void(task_error const&)>;

} // namespace threadschedule
