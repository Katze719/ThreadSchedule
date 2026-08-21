#pragma once

/**
 * @file thread_registry.hpp
 * @brief Portable thread registry and RAII registration helpers.
 */

#include "detail/registry/backend.hpp"
#include "detail/thread/control.hpp"
#include "thread_config.hpp"
#include "thread_id.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace threadschedule
{
namespace advanced
{
class composite_thread_registry;
}

/**
 * @brief Snapshot entry returned by @ref thread_registry::snapshot.
 */
struct registered_thread
{
  /** @brief Stable logical thread id inside the registry. */
  thread_id id;
  /** @brief Native std::thread identifier. */
  std::thread::id std_id;
  /** @brief User-visible thread name if known. */
  std::string name;
  /** @brief Optional logical component/group label. */
  std::string component;
  /** @brief Whether thread is currently alive according to the registry. */
  bool alive{ false };
};

class thread_registry;
class auto_register_current_thread;
class global_registry_binding;

[[nodiscard]] auto global_registry() -> thread_registry&;

/**
 * @brief Registry for named/configurable threads.
 *
 * The global facade returned by @ref global_registry is backed by runtime
 * storage and can optionally be rebound to another registry via
 * @ref global_registry_binding.
 */
class thread_registry
{
public:
  /** @brief Construct an owning registry instance. */
  thread_registry() : owned_(std::make_shared<detail::thread_registry_backend>()) {}

  thread_registry(thread_registry&&) noexcept = default;
  auto
  operator=(thread_registry&& other) -> thread_registry&
  {
    if (this != &other)
      {
        bool const keep_global_proxy = global_;
        auto* const current = owned_.get();
        bool const is_external = current != nullptr && &detail::runtime_registry() == current;
        retired_.reserve(retired_.size() + (owned_ != nullptr ? 1u : 0u) + other.retired_.size());
        bindings_.reserve(bindings_.size() + other.bindings_.size());
        // A registration guard can outlive its entry (for example after an
        // explicit unregister) and still retain the backend address for its
        // destructor. Keep every replaced backend alive, even when it is
        // currently empty.
        if (owned_ != nullptr)
          retired_.push_back(std::move(owned_));
        for (auto& backend : other.retired_)
          retired_.push_back(std::move(backend));
        other.retired_.clear();
        for (auto& binding : other.bindings_)
          bindings_.push_back(std::move(binding));
        other.bindings_.clear();
        owned_ = std::move(other.owned_);
        retarget_bindings();
        if (keep_global_proxy)
          {
            // global_registry() is a permanent facade over registry(). A
            // public move-assignment must not turn that singleton into an
            // unrelated owning registry.
            global_ = true;
            if (other.global_)
              detail::runtime_set_external_registry(nullptr);
            else if (auto state = detail::runtime_external_registry_state())
              state->replace(owned_);
            else
              detail::runtime_set_external_registry_state(
                  std::make_shared<detail::external_registry_binding_state>(owned_));
          }
        else
          {
            if (is_external)
              {
                if (other.global_)
                  detail::runtime_set_external_registry(nullptr);
                else if (auto state = detail::runtime_external_registry_state())
                  state->replace(owned_);
              }
            global_ = other.global_;
          }
      }
    return *this;
  }
  thread_registry(thread_registry const&) = delete;
  auto operator=(thread_registry const&) -> thread_registry& = delete;

  /** @brief Create an owning registry without throwing. */
  static auto
  create() -> result<thread_registry>
  {
    return detail::try_result([]() -> result<thread_registry> { return thread_registry{}; });
  }

  /**
   * @brief Register the calling thread.
   * @param name Optional thread name.
   * @param component Optional component label.
   */
  auto
  register_current_thread(std::string name = {}, std::string component = {}) -> result<void>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<void>
          {
            auto control = detail::thread_control_block::create_for_current_thread();
            native().register_current_thread(control, std::move(name), std::move(component));
            return {};
          });
  }

  /** @brief Unregister the calling thread. */
  auto
  unregister_current_thread() -> result<void>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<void>
          {
            native().unregister_current_thread();
            return {};
          });
  }

  /** @brief Return number of tracked entries. */
  [[nodiscard]] auto
  count() const -> std::size_t
  {
    return has_native() ? native().count() : 0;
  }

  /** @brief Return whether registry has no tracked entries. */
  [[nodiscard]] auto
  empty() const -> bool
  {
    return !has_native() || native().empty();
  }

  /**
   * @brief Take a snapshot of all registry entries.
   * @return Vector of @ref registered_thread records.
   */
  [[nodiscard]] auto
  snapshot() const -> result<std::vector<registered_thread>>
  {
    if (!has_native())
      return std::vector<registered_thread>{};
    return detail::try_result(
        [&]() -> result<std::vector<registered_thread>>
          {
            auto entries = native().query().entries();
            std::vector<registered_thread> result_entries;
            result_entries.reserve(entries.size());
            for (auto const& entry : entries)
              {
                result_entries.push_back({ detail::thread_id_access::make(static_cast<std::uint64_t>(entry.tid)),
                                           entry.std_id, entry.name, entry.component, entry.alive });
              }
            return result_entries;
          });
  }

  /**
   * @brief Apply configuration to a registered thread.
   * @param id Registry thread id.
   * @param config Portable configuration to apply.
   */
  auto
  configure(thread_id id, thread_config const& config) -> result<void>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    auto const native_id = checked_native_id(id);
    if (!native_id)
      return unexpected(native_id.error());
    return detail::try_result([&]() -> result<void>
                                { return native().configure(native_id.value(), detail::to_native(config)); });
  }

  /** @brief Set portable priority preset for a registered thread. */
  auto
  set_priority(thread_id id, priority_level level) -> result<void>
  {
    return set_nice(id, nice_value{ static_cast<int>(level) });
  }

  /** @brief Set explicit nice value for a registered thread. */
  auto
  set_nice(thread_id id, nice_value value) -> result<void>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    auto const native_id = checked_native_id(id);
    if (!native_id)
      return unexpected(native_id.error());
    return detail::try_result(
        [&]() -> result<void>
          { return native().configure(native_id.value(), detail::native_schedule::posix_nice(value.value())); });
  }

  /** @brief Query portable priority preset for a registered thread. */
  [[nodiscard]] auto
  get_priority(thread_id id) const -> result<priority_level>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    auto const native_id = checked_native_id(id);
    if (!native_id)
      return unexpected(native_id.error());
    return detail::try_result(
        [&]() -> result<priority_level>
          {
            auto value = native().get_nice_value(native_id.value());
            if (!value)
              return unexpected(value.error());
            return detail::to_priority_level(value.value());
          });
  }

  /** @brief Query nice value for a registered thread. */
  [[nodiscard]] auto
  get_nice(thread_id id) const -> result<nice_value>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    auto const native_id = checked_native_id(id);
    if (!native_id)
      return unexpected(native_id.error());
    return detail::try_result(
        [&]() -> result<nice_value>
          {
            auto value = native().get_nice_value(native_id.value());
            return detail::portable_thread_control::get_nice(std::move(value));
          });
  }

private:
  [[nodiscard]] static auto
  checked_native_id(thread_id id) noexcept -> result<detail::native_thread_id>
  {
    auto const value = detail::thread_id_access::value(id);
    auto const maximum = static_cast<std::uint64_t>((std::numeric_limits<detail::native_thread_id>::max)());
    if (value > maximum)
      return unexpected(std::make_error_code(std::errc::invalid_argument));
    return static_cast<detail::native_thread_id>(value);
  }

  struct global_tag
  {
  };

  explicit thread_registry(global_tag /*unused*/) noexcept : global_(true) {}

  [[nodiscard]] auto
  has_native() const noexcept -> bool
  {
    return global_ || owned_ != nullptr;
  }

  [[nodiscard]] auto
  native() -> detail::thread_registry_backend&
  {
    return global_ ? detail::runtime_registry() : *owned_;
  }

  [[nodiscard]] auto
  native() const -> detail::thread_registry_backend const&
  {
    return global_ ? detail::runtime_registry() : *owned_;
  }

  std::shared_ptr<detail::thread_registry_backend> owned_;
  std::vector<std::shared_ptr<detail::thread_registry_backend>> retired_;
  std::vector<std::weak_ptr<detail::external_registry_binding_state>> bindings_;
  bool global_{ false };

  void
  retarget_bindings()
  {
    bindings_.erase(
        std::remove_if(bindings_.begin(), bindings_.end(), [](auto const& binding) { return binding.expired(); }),
        bindings_.end());
    for (auto const& binding : bindings_)
      if (auto state = binding.lock())
        state->replace(owned_);
  }

  void
  track_binding(std::shared_ptr<detail::external_registry_binding_state> const& state)
  {
    bindings_.erase(
        std::remove_if(bindings_.begin(), bindings_.end(), [](auto const& binding) { return binding.expired(); }),
        bindings_.end());
    bindings_.push_back(state);
  }

  friend auto global_registry() -> thread_registry&;
  friend class global_registry_binding;
  friend class auto_register_current_thread;
  friend class advanced::composite_thread_registry;
};

[[nodiscard]] inline auto
global_registry() -> thread_registry&
{
  static thread_registry value(thread_registry::global_tag{});
  return value;
}

/**
 * @brief Temporarily bind an owning registry as the global external registry.
 *
 * Restores the previous binding on destruction.
 *
 * Installing, moving, or destroying a binding, and move-assigning a bound
 * registry, must not run concurrently with operations through @ref
 * global_registry. Bindings are intended to be installed during application
 * startup and destroyed after worker threads have stopped using the global
 * registry.
 */
class global_registry_binding
{
public:
  /**
   * @brief Install external binding.
   * @param registry Owning registry instance.
   * @throws std::invalid_argument if @p registry is itself the global facade.
   */
  explicit global_registry_binding(thread_registry& registry)
  {
    if (registry.global_ || registry.owned_ == nullptr)
      throw std::invalid_argument("global registry facade cannot be bound as an external registry");
    owner_ = registry.owned_;
    state_ = std::make_shared<detail::external_registry_binding_state>(owner_);
    registry.track_binding(state_);
    previous_state_ = detail::runtime_exchange_external_registry_state(state_);
  }

  ~global_registry_binding()
  {
    reset();
  }

  global_registry_binding(global_registry_binding const&) = delete;
  auto operator=(global_registry_binding const&) -> global_registry_binding& = delete;

  global_registry_binding(global_registry_binding&& other) noexcept
      : owner_(std::move(other.owner_)), state_(std::move(other.state_)),
        previous_state_(std::move(other.previous_state_))
  {
  }

  auto
  operator=(global_registry_binding&& other) noexcept -> global_registry_binding&
  {
    if (this != &other)
      {
        reset();
        owner_ = std::move(other.owner_);
        state_ = std::move(other.state_);
        previous_state_ = std::move(other.previous_state_);
      }
    return *this;
  }

private:
  void
  reset() noexcept
  {
    if (state_)
      detail::runtime_set_external_registry_state(previous_state_);
    owner_.reset();
    state_.reset();
    previous_state_.reset();
  }

  std::shared_ptr<detail::thread_registry_backend> owner_;
  std::shared_ptr<detail::external_registry_binding_state> state_;
  std::shared_ptr<detail::external_registry_binding_state> previous_state_;
};

/** @brief RAII registration of the calling thread in a portable registry. */
class auto_register_current_thread
{
public:
  explicit auto_register_current_thread(std::string name = {}, std::string component = {})
      : auto_register_current_thread(global_registry(), std::move(name), std::move(component))
  {
  }

  explicit auto_register_current_thread(thread_registry& registry, std::string name = {}, std::string component = {})
  {
    if (!registry.has_native())
      throw std::system_error(std::make_error_code(std::errc::operation_canceled),
                              "auto_register_current_thread: moved-from registry");
    if (registry.global_)
      {
        if (auto state = detail::runtime_external_registry_state())
          registry_owner_ = state->owner;
      }
    else
      {
        registry_owner_ = registry.owned_;
      }
    registry_ = registry_owner_ ? registry_owner_.get() : &registry.native();
    auto control = detail::thread_control_block::create_for_current_thread();
    native_id_ = control->tid();
    (void)control->set_name(name);
    active_ = registry_->register_guard(control, name, component);
    control_ = std::move(control);
  }

  ~auto_register_current_thread()
  {
    reset();
  }

  auto_register_current_thread(auto_register_current_thread const&) = delete;
  auto operator=(auto_register_current_thread const&) -> auto_register_current_thread& = delete;

  auto_register_current_thread(auto_register_current_thread&& other) noexcept
      : active_(other.active_), registry_(other.registry_), native_id_(other.native_id_),
        control_(std::move(other.control_)), registry_owner_(std::move(other.registry_owner_))
  {
    other.active_ = false;
    other.registry_ = nullptr;
    other.native_id_ = {};
  }

  auto
  operator=(auto_register_current_thread&& other) noexcept -> auto_register_current_thread&
  {
    if (this != &other)
      {
        reset();
        active_ = other.active_;
        registry_ = other.registry_;
        native_id_ = other.native_id_;
        control_ = std::move(other.control_);
        registry_owner_ = std::move(other.registry_owner_);
        other.active_ = false;
        other.registry_ = nullptr;
        other.native_id_ = {};
      }
    return *this;
  }

private:
  void
  reset() noexcept
  {
    if (active_ && registry_ != nullptr)
      registry_->unregister_thread(native_id_, control_.get());
    active_ = false;
    registry_ = nullptr;
    registry_owner_.reset();
  }

  bool active_{ false };
  detail::thread_registry_backend* registry_{ nullptr };
  detail::native_thread_id native_id_{};
  std::shared_ptr<detail::thread_control_block> control_;
  std::shared_ptr<detail::thread_registry_backend> registry_owner_;
};

} // namespace threadschedule
