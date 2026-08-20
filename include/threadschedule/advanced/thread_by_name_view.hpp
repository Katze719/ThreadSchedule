#pragma once

/**
 * @file advanced/thread_by_name_view.hpp
 * @brief Linux process-thread discovery and control by OS-visible name.
 */

#include "../detail/thread/control.hpp"
#include "../detail/thread/identity.hpp"
#include "../detail/try_result.hpp"
#include "../result.hpp"
#include "native_thread.hpp"

#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace threadschedule::advanced
{

/**
 * @brief Non-owning control view found by an exact process-thread name.
 *
 * Linux lookup scans `/proc/self/task` and compares the kernel-visible `comm`
 * name exactly. A view stores both the native thread ID and the thread start
 * time, so an exited thread or a recycled ID is rejected with
 * `std::errc::no_such_process`.
 *
 * Lookup is snapshot-based. Renaming the target does not retarget an existing
 * view, and a matching thread created after lookup does not affect it.
 *
 * @warning Linux exposes only numeric TIDs to these native control calls. A
 * thread can still exit in the small interval between the identity check and
 * the syscall. Registry-backed control is required when lifecycle coupling
 * must close that race.
 */
class thread_by_name_view
{
public:
  /**
   * @brief Find exactly one process thread with @p name.
   * @throws std::system_error for no match, ambiguous matches, invalid input,
   *         or an unsupported platform.
   */
  explicit thread_by_name_view(std::string_view name) : identity_(unique_identity_or_throw(name)) {}

  /** @brief Find exactly one named process thread without throwing. */
  [[nodiscard]] static auto
  create(std::string_view name) -> result<thread_by_name_view>
  {
    return detail::try_result(
        [name]() -> result<thread_by_name_view>
          {
            auto identity = unique_identity(name);
            if (!identity)
              return unexpected(identity.error());
            return thread_by_name_view(identity_tag{}, identity.value());
          });
  }

  /**
   * @brief Return every exact name match ordered by native thread ID.
   * @return An empty vector when no process thread has the requested name.
   */
  [[nodiscard]] static auto
  find_all(std::string_view name) -> result<std::vector<thread_by_name_view>>
  {
    return detail::try_result(
        [name]() -> result<std::vector<thread_by_name_view>>
          {
            auto identities = detail::find_native_threads_by_name(name);
            if (!identities)
              return unexpected(identities.error());

            std::vector<thread_by_name_view> views;
            views.reserve(identities->size());
            for (auto const& identity : identities.value())
              views.push_back(thread_by_name_view(identity_tag{}, identity));
            return views;
          });
  }

  /** @brief Return whether the original native thread identity is still live. */
  [[nodiscard]] auto
  alive() const noexcept -> bool
  {
    return detail::native_thread_is_alive(identity_);
  }

  /** @brief Return the platform-native process thread ID. */
  [[nodiscard]] auto
  native_id() const noexcept -> native_thread_id
  {
    return identity_.id;
  }

  /** @brief Apply a portable thread configuration to the target. */
  auto
  configure(thread_config const& config) -> result<void>
  {
    return detail::try_result(
        [this, &config]() -> result<void>
          {
            auto const native = detail::to_native(config);
            auto checked = with_live_identity([](auto /*id*/) -> result<void> { return {}; });
            if (!checked)
              return checked;
            if (native.name)
              {
                auto named = set_name(*native.name);
                if (!named)
                  return named;
              }
            if (native.scheduling)
              {
                auto scheduled = with_live_identity(
                    [&](auto id) { return detail::apply_scheduling_config(id, id, *native.scheduling); });
                if (!scheduled)
                  return scheduled;
              }
            if (native.affinity)
              return with_live_identity([&](auto id) { return detail::apply_affinity_checked(id, *native.affinity); });
            return {};
          });
  }

  /** @brief Set a portable priority preset on the target. */
  auto
  set_priority(priority_level level) -> result<void>
  {
    return with_live_identity(
        [&](auto id)
          {
            return detail::apply_scheduling_config(id, id,
                                                   detail::native_schedule::posix_nice(static_cast<int>(level)));
          });
  }

  /** @brief Set an explicit nice value on the target. */
  auto
  set_nice(nice_value value) -> result<void>
  {
    return with_live_identity(
        [&](auto id)
          { return detail::apply_scheduling_config(id, id, detail::native_schedule::posix_nice(value.value())); });
  }

  /** @brief Query the effective portable priority preset. */
  [[nodiscard]] auto
  get_priority() const -> result<priority_level>
  {
    auto value = with_live_identity([&](auto id) { return detail::read_effective_nice(id, id); });
    return detail::portable_thread_control::get_priority(std::move(value));
  }

  /** @brief Query the effective nice value. */
  [[nodiscard]] auto
  get_nice() const -> result<nice_value>
  {
    auto value = with_live_identity([&](auto id) { return detail::read_effective_nice(id, id); });
    return detail::portable_thread_control::get_nice(std::move(value));
  }

  /** @brief Set the OS-visible target name without changing view identity. */
  auto
  set_name(std::string const& name) -> result<void>
  {
    return with_live_identity([&](auto id) { return detail::apply_name(id, name); });
  }

  /** @brief Query the current OS-visible target name. */
  [[nodiscard]] auto
  get_name() const -> result<std::string>
  {
    return with_live_identity([](auto id) { return detail::read_name(id); });
  }

  /** @brief Set target CPU affinity with exact readback and rollback. */
  auto
  set_affinity(thread_affinity const& affinity) -> result<void>
  {
    return detail::try_result(
        [this, &affinity]() -> result<void>
          {
            auto const native = detail::to_native(affinity);
            return with_live_identity([&](auto id) { return detail::apply_affinity_checked(id, native); });
          });
  }

  /** @brief Query target CPU affinity. */
  [[nodiscard]] auto
  get_affinity() const -> result<thread_affinity>
  {
    auto native = with_live_identity([](auto id) { return detail::read_affinity(id); });
    return detail::portable_thread_control::get_affinity(std::move(native));
  }

private:
  struct identity_tag
  {
  };

  thread_by_name_view(identity_tag /*unused*/, detail::native_thread_identity identity) noexcept : identity_(identity)
  {
  }

  [[nodiscard]] static auto
  unique_identity(std::string_view name) -> result<detail::native_thread_identity>
  {
    auto identities = detail::find_native_threads_by_name(name);
    if (!identities)
      return unexpected(identities.error());
    if (identities->empty())
      return unexpected(std::make_error_code(std::errc::no_such_process));
    if (identities->size() != 1)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return identities->front();
  }

  [[nodiscard]] static auto
  unique_identity_or_throw(std::string_view name) -> detail::native_thread_identity
  {
    auto identity = unique_identity(name);
    if (!identity)
      throw std::system_error(identity.error(), "thread_by_name_view");
    return identity.value();
  }

  template <typename Function>
  [[nodiscard]] auto
  with_live_identity(Function&& function) const
      -> decltype(std::declval<Function>()(std::declval<detail::native_thread_id>()))
  {
    using result_type = decltype(std::declval<Function>()(std::declval<detail::native_thread_id>()));
    auto const inactive = [] { return result_type(unexpected(std::make_error_code(std::errc::no_such_process))); };
    if (!alive())
      return inactive();

    auto result_value = std::forward<Function>(function)(identity_.id);
    if (result_value && !alive())
      return inactive();
    return result_value;
  }

  detail::native_thread_identity identity_;
};

} // namespace threadschedule::advanced
