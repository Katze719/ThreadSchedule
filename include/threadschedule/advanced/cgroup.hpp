#pragma once

/**
 * @file advanced/cgroup.hpp
 * @brief Linux cgroup thread attachment.
 */

#include "native_thread.hpp"

#ifndef _WIN32
#  include <cerrno>
#  include <fcntl.h>
#  include <string>
#  include <system_error>
#  include <unistd.h>

namespace threadschedule::advanced
{

/**
 * @brief Attach a native Linux thread ID to a cgroup v1 or v2 directory.
 * @return Success, or `operation_not_permitted` when no supported control file can be written.
 */
inline auto
cgroup_attach_tid(std::string const& cgroup_dir, native_thread_id tid) -> result<void>
{
  if (tid <= 0)
    return unexpected(std::make_error_code(std::errc::invalid_argument));

  char const* const candidates[] = { "cgroup.threads", "tasks" };
  std::string const value = std::to_string(tid) + "\n";
  for (auto const* file : candidates)
    {
      std::string const path = cgroup_dir + "/" + file;
      int const descriptor = open(path.c_str(), O_WRONLY | O_CLOEXEC);
      if (descriptor < 0)
        continue;

      std::size_t offset = 0;
      while (offset < value.size())
        {
          auto const written = write(descriptor, value.data() + offset, value.size() - offset);
          if (written > 0)
            {
              offset += static_cast<std::size_t>(written);
              continue;
            }
          if (written < 0 && errno == EINTR)
            continue;
          break;
        }
      (void)close(descriptor);
      if (offset == value.size())
        return {};
    }
  return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

} // namespace threadschedule::advanced
#endif
