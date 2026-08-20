#pragma once

/**
 * @file detail/thread/identity.hpp
 * @brief Native process-thread identity and Linux name lookup helpers.
 */

#include "../../result.hpp"
#include "../scheduling/native.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#  include <cerrno>
#  include <dirent.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

namespace threadschedule::detail
{

struct native_thread_identity
{
  native_thread_id id{};
  std::uint64_t start_time{};
};

#ifndef _WIN32
[[nodiscard]] inline auto
read_thread_start_time(native_thread_id id) noexcept -> std::optional<std::uint64_t>
{
  try
    {
      std::ifstream input("/proc/self/task/" + std::to_string(id) + "/stat");
      std::string line;
      if (!std::getline(input, line))
        return std::nullopt;
      auto const command_end = line.rfind(')');
      if (command_end == std::string::npos || command_end + 2 >= line.size())
        return std::nullopt;

      std::istringstream fields(line.substr(command_end + 2));
      char state = '\0';
      if (!(fields >> state) || state == 'Z' || state == 'X' || state == 'x')
        return std::nullopt;
      std::string ignored;
      for (int field = 4; field < 22; ++field)
        if (!(fields >> ignored))
          return std::nullopt;
      std::uint64_t value = 0;
      if (!(fields >> value))
        return std::nullopt;
      return value;
    }
  catch (...)
    {
      return std::nullopt;
    }
}
#endif

[[nodiscard]] inline auto
native_thread_is_alive(native_thread_identity const& identity) noexcept -> bool
{
#ifdef _WIN32
  (void)identity;
  return false;
#else
  errno = 0;
  if (syscall(SYS_tgkill, getpid(), identity.id, 0) != 0 && errno != EPERM)
    return false;
  auto const current = read_thread_start_time(identity.id);
  return current.has_value() && current.value() == identity.start_time;
#endif
}

[[nodiscard]] inline auto
find_native_threads_by_name(std::string_view name) -> result<std::vector<native_thread_identity>>
{
  if (name.empty() || name.size() > 15)
    return unexpected(std::make_error_code(std::errc::invalid_argument));

#ifdef _WIN32
  return unexpected(std::make_error_code(std::errc::function_not_supported));
#else
  struct directory_deleter
  {
    void
    operator()(DIR* directory) const noexcept
    {
      if (directory != nullptr)
        (void)closedir(directory);
    }
  };

  std::unique_ptr<DIR, directory_deleter> directory(opendir("/proc/self/task"));
  if (!directory)
    return unexpected(std::error_code(errno, std::generic_category()));

  std::vector<native_thread_identity> matches;
  while (true)
    {
      errno = 0;
      auto* const entry = readdir(directory.get());
      if (entry == nullptr)
        {
          if (errno != 0)
            return unexpected(std::error_code(errno, std::generic_category()));
          break;
        }

      std::string_view const candidate(entry->d_name);
      native_thread_id id{};
      auto const parsed = std::from_chars(candidate.data(), candidate.data() + candidate.size(), id);
      if (parsed.ec != std::errc{} || parsed.ptr != candidate.data() + candidate.size() || id <= 0)
        continue;

      std::ifstream comm("/proc/self/task/" + std::to_string(id) + "/comm");
      std::string current_name;
      if (!std::getline(comm, current_name) || current_name != name)
        continue;

      auto const start_time = read_thread_start_time(id);
      if (start_time.has_value())
        matches.push_back({ id, start_time.value() });
    }

  std::sort(matches.begin(), matches.end(), [](auto const& lhs, auto const& rhs) { return lhs.id < rhs.id; });
  return matches;
#endif
}

} // namespace threadschedule::detail
