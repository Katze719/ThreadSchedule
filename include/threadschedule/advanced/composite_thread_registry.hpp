#pragma once

/**
 * @file advanced/composite_thread_registry.hpp
 * @brief Composite registry support for advanced multi-registry scenarios.
 */

#include "../detail/try_result.hpp"
#include "../thread_registry.hpp"

#include <mutex>
#include <vector>

namespace threadschedule::advanced
{

/**
 * @brief Non-owning facade that merges snapshots from multiple registries.
 *
 * Attached registries must outlive this object. Snapshot creation is safe to
 * call concurrently with `attach()` and with registration changes in the
 * attached registries. Duplicate entries are intentionally preserved.
 */
class composite_thread_registry
{
public:
  composite_thread_registry() = default;
  composite_thread_registry(composite_thread_registry const&) = delete;
  auto operator=(composite_thread_registry const&) -> composite_thread_registry& = delete;

  /** @brief Attach a registry without transferring ownership. */
  void
  attach(thread_registry& registry)
  {
    if (!registry.has_native())
      throw std::system_error(std::make_error_code(std::errc::operation_canceled),
                              "composite_thread_registry::attach: moved-from registry");
    implementation_.attach(&registry.native());
  }

  /** @brief Return one portable snapshot containing every attached registry. */
  [[nodiscard]] auto
  snapshot() const -> result<std::vector<registered_thread>>
  {
    return ::threadschedule::detail::try_result(
        [this]() -> result<std::vector<registered_thread>>
          {
            auto entries = implementation_.query().entries();
            std::vector<registered_thread> result_entries;
            result_entries.reserve(entries.size());
            for (auto const& entry : entries)
              result_entries.push_back({ detail::thread_id_access::make(static_cast<std::uint64_t>(entry.tid)),
                                         entry.std_id, entry.name, entry.component, entry.alive });
            return result_entries;
          });
  }

  /** @brief Number of entries in a newly merged snapshot. */
  [[nodiscard]] auto
  count() const -> std::size_t
  {
    return implementation_.count();
  }

  /** @brief Whether a newly merged snapshot contains no entries. */
  [[nodiscard]] auto
  empty() const -> bool
  {
    return implementation_.empty();
  }

private:
  ::threadschedule::detail::composite_thread_registry_backend implementation_;
};

} // namespace threadschedule::advanced
