#pragma once

/** @file detail/registry/composite_thread_registry_backend.hpp
 *  @brief Multi-registry snapshot composition.
 */

#include "thread_registry_backend.hpp"

#include <mutex>
#include <utility>
#include <vector>

namespace threadschedule
{
/**
 * @brief Aggregates multiple thread_registry_backend instances into a single
 * queryable view.
 *
 * composite_thread_registry_backend is useful when threads are spread across
 * several independent @ref thread_registry_backend instances (e.g. one per
 * shared library) and you want a unified query interface over all of them.
 *
 * @par Thread safety
 * All public methods are thread-safe.  The internal list of attached
 * registries is protected by a @c std::mutex.
 *
 * @par Copyability / movability
 * Not copyable and not movable (holds a @c std::mutex).
 *
 * @par Ownership
 * attach() stores **raw pointers** to the supplied registries.  The caller
 * is responsible for ensuring that every attached thread_registry_backend
 * outlives this composite_thread_registry_backend.  Violating this results in
 * undefined behaviour.
 *
 * @par Deduplication
 * No deduplication is performed.  If the same TID appears in multiple
 * attached registries, it will appear multiple times in the merged
 * query_view.
 *
 * @par Querying
 * query() iterates over every attached registry, calls its own query(), and
 * concatenates the results into a single @ref
 * thread_registry_backend::query_view snapshot. The same functional-style
 * helpers (filter, map, for_each, etc.) are inherited from the
 * @c detail::query_facade_mixin implementation.
 */
namespace detail
{

class composite_thread_registry_backend : public query_facade_mixin<composite_thread_registry_backend>
{
public:
  void
  attach(thread_registry_backend* reg)
  {
    if (reg == nullptr)
      return;
    std::lock_guard<std::mutex> lock(mutex_);
    registries_.push_back(reg);
  }

  [[nodiscard]] auto
  query() const -> thread_registry_backend::query_view
  {
    std::vector<registered_thread_info_backend> merged;
    std::vector<thread_registry_backend*> regs;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      regs = registries_;
    }
    for (auto* r : regs)
      {
        auto view = r->query();
        auto const& entries = view.entries();
        merged.insert(merged.end(), entries.begin(), entries.end());
      }
    return thread_registry_backend::query_view(std::move(merged));
  }

private:
  mutable std::mutex mutex_;
  std::vector<thread_registry_backend*> registries_;
};

} // namespace detail
} // namespace threadschedule
