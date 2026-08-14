#pragma once

#include "../result.hpp"

#include <exception>
#include <new>
#include <system_error>
#include <utility>

namespace threadschedule::detail
{

[[nodiscard]] inline auto
current_exception_error_code() noexcept -> std::error_code
{
  try
    {
      throw;
    }
  catch (std::system_error const& error)
    {
      return error.code();
    }
  catch (std::bad_alloc const&)
    {
      return std::make_error_code(std::errc::not_enough_memory);
    }
  catch (...)
    {
      return std::make_error_code(std::errc::state_not_recoverable);
    }
}

template <typename Function>
[[nodiscard]] auto
try_result(Function&& function) -> decltype(std::forward<Function>(function)())
{
  try
    {
      return std::forward<Function>(function)();
    }
  catch (...)
    {
      return unexpected(current_exception_error_code());
    }
}

} // namespace threadschedule::detail
