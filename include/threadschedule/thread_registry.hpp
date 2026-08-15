#pragma once

#include "detail/registry/backend.hpp"
#include "detail/thread/control.hpp"
#include "thread_config.hpp"
#include "thread_id.hpp"

#include <cstdint>
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

struct registered_thread
{
  thread_id id;
  std::thread::id std_id;
  std::string name;
  std::string component;
  bool alive{ false };
};

class thread_registry;
class auto_register_current_thread;
class global_registry_binding;

[[nodiscard]] auto global_registry() -> thread_registry&;

class thread_registry
{
public:
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
        // A registration guard can outlive its entry (for example after an
        // explicit unregister) and still retain the backend address for its
        // destructor. Keep every replaced backend alive, even when it is
        // currently empty.
        if (owned_ != nullptr)
          retired_.push_back(std::move(owned_));
        for (auto& backend : other.retired_)
          retired_.push_back(std::move(backend));
        other.retired_.clear();
        owned_ = std::move(other.owned_);
        if (keep_global_proxy)
          {
            // global_registry() is a permanent facade over registry(). A
            // public move-assignment must not turn that singleton into an
            // unrelated owning registry.
            global_ = true;
            detail::runtime_set_external_registry(other.global_ ? nullptr : owned_.get());
          }
        else
          {
            if (is_external)
              detail::runtime_set_external_registry(other.global_ ? nullptr : owned_.get());
            global_ = other.global_;
          }
      }
    return *this;
  }
  thread_registry(thread_registry const&) = delete;
  auto operator=(thread_registry const&) -> thread_registry& = delete;

  static auto
  create() -> result<thread_registry>
  {
    return detail::try_result([]() -> result<thread_registry> { return thread_registry{}; });
  }

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

  [[nodiscard]] auto
  count() const -> std::size_t
  {
    return has_native() ? native().count() : 0;
  }

  [[nodiscard]] auto
  empty() const -> bool
  {
    return !has_native() || native().empty();
  }

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

  auto
  configure(thread_id id, thread_config const& config) -> result<void>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<void>
          {
            return native().configure(static_cast<detail::native_thread_id>(detail::thread_id_access::value(id)),
                                      detail::to_native(config));
          });
  }

  auto
  set_priority(thread_id id, priority_level level) -> result<void>
  {
    return set_nice(id, nice_value{ static_cast<int>(level) });
  }

  auto
  set_nice(thread_id id, nice_value value) -> result<void>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<void>
          {
            return native().configure(static_cast<detail::native_thread_id>(detail::thread_id_access::value(id)),
                                      detail::native_schedule::posix_nice(value.value()));
          });
  }

  [[nodiscard]] auto
  get_priority(thread_id id) const -> result<priority_level>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<priority_level>
          {
            auto value
                = native().get_nice_value(static_cast<detail::native_thread_id>(detail::thread_id_access::value(id)));
            if (!value)
              return unexpected(value.error());
            return detail::to_priority_level(value.value());
          });
  }

  [[nodiscard]] auto
  get_nice(thread_id id) const -> result<nice_value>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    return detail::try_result(
        [&]() -> result<nice_value>
          {
            auto value
                = native().get_nice_value(static_cast<detail::native_thread_id>(detail::thread_id_access::value(id)));
            return detail::portable_thread_control::get_nice(std::move(value));
          });
  }

private:
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
  bool global_{ false };

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

class global_registry_binding
{
public:
  explicit global_registry_binding(thread_registry& registry)
  {
    if (registry.global_ || registry.owned_ == nullptr)
      throw std::invalid_argument("global registry facade cannot be bound as an external registry");
    owner_ = registry.owned_;
    installed_ = owner_.get();
    previous_ = detail::runtime_exchange_external_registry(installed_);
  }

  ~global_registry_binding()
  {
    reset();
  }

  global_registry_binding(global_registry_binding const&) = delete;
  auto operator=(global_registry_binding const&) -> global_registry_binding& = delete;

  global_registry_binding(global_registry_binding&& other) noexcept
      : owner_(std::move(other.owner_)), installed_(std::exchange(other.installed_, nullptr)),
        previous_(std::exchange(other.previous_, nullptr))
  {
  }

  auto
  operator=(global_registry_binding&& other) noexcept -> global_registry_binding&
  {
    if (this != &other)
      {
        reset();
        owner_ = std::move(other.owner_);
        installed_ = std::exchange(other.installed_, nullptr);
        previous_ = std::exchange(other.previous_, nullptr);
      }
    return *this;
  }

private:
  void
  reset() noexcept
  {
    if (installed_ != nullptr)
      detail::runtime_set_external_registry(previous_);
    owner_.reset();
    installed_ = nullptr;
    previous_ = nullptr;
  }

  std::shared_ptr<detail::thread_registry_backend> owner_;
  detail::thread_registry_backend* installed_{ nullptr };
  detail::thread_registry_backend* previous_{ nullptr };
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
      : registry_(&registry.native())
  {
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
        control_(std::move(other.control_))
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
  }

  bool active_{ false };
  detail::thread_registry_backend* registry_{ nullptr };
  detail::native_thread_id native_id_{};
  std::shared_ptr<detail::thread_control_block> control_;
};

} // namespace threadschedule
