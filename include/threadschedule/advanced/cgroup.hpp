#pragma once

/**
 * @file advanced/cgroup.hpp
 * @brief Linux cgroup thread attachment.
 */

#include "native_thread.hpp"

#ifndef _WIN32
#  include <fstream>
#  include <string>
#  include <system_error>
#  include <vector>

namespace threadschedule::advanced
{

/**
 * @brief Attach a native Linux thread ID to a cgroup v1 or v2 directory.
 * @return Success, or `operation_not_permitted` when no supported control file can be written.
 */
inline auto
cgroup_attach_tid(std::string const& cgroup_dir, native_thread_id tid) -> result<void>
{
  std::vector<std::string> const candidates = { "cgroup.threads", "tasks", "cgroup.procs" };
  for (auto const& file : candidates)
    {
      std::ofstream out(cgroup_dir + "/" + file);
      if (!out)
        continue;
      out << tid;
      out.flush();
      if (out)
        return {};
    }
  return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}

} // namespace threadschedule::advanced
#endif
