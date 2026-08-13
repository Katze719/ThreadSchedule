#pragma once

#include "../../thread_config.hpp"
#include "../thread/control.hpp"
#include "backend.hpp"

#include <cstdint>
#include <memory>
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
  std::uint64_t native_id{ 0 };
  std::thread::id id;
  std::string name;
  std::string component;
  bool alive{ false };
};

class thread_registry;
class auto_register_current_thread;

[[nodiscard]] auto global_registry() -> thread_registry&;

class thread_registry
{
public:
  thread_registry() : owned_(std::make_unique<detail::thread_registry_backend>()) {}

  ~thread_registry()
  {
    if (owned_ != nullptr && &detail::runtime_registry() == owned_.get())
      detail::runtime_set_external_registry(nullptr);
  }

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
    try
      {
        return thread_registry{};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  register_current_thread(std::string name = {}, std::string component = {}) -> result<void>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    try
      {
        auto control = detail::thread_control_block::create_for_current_thread();
        native().register_current_thread(control, std::move(name), std::move(component));
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  unregister_current_thread() -> result<void>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    try
      {
        native().unregister_current_thread();
        return {};
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
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
    try
      {
        auto entries = native().query().entries();
        std::vector<registered_thread> result_entries;
        result_entries.reserve(entries.size());
        for (auto const& entry : entries)
          {
            result_entries.push_back(
                { static_cast<std::uint64_t>(entry.tid), entry.std_id, entry.name, entry.component, entry.alive });
          }
        return result_entries;
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  configure(std::uint64_t native_id, thread_config const& config) -> result<void>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    try
      {
        return native().configure(static_cast<detail::native_thread_id>(native_id), detail::to_native(config));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  auto
  set_priority(std::uint64_t native_id, priority_level level) -> result<void>
  {
    return set_nice(native_id, static_cast<int>(level));
  }

  auto
  set_nice(std::uint64_t native_id, int nice_value) -> result<void>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    try
      {
        return native().configure(static_cast<detail::native_thread_id>(native_id),
                                  detail::native_schedule::posix_nice(nice_value));
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
  }

  [[nodiscard]] auto
  get_priority(std::uint64_t native_id) const -> result<priority_level>
  {
    if (!has_native())
      return unexpected(std::make_error_code(std::errc::operation_canceled));
    try
      {
        auto value = native().get_nice_value(static_cast<detail::native_thread_id>(native_id));
        if (!value)
          return unexpected(value.error());
        return detail::to_priority_level(value.value());
      }
    catch (...)
      {
        return unexpected(detail::current_exception_error_code());
      }
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

  std::unique_ptr<detail::thread_registry_backend> owned_;
  std::vector<std::unique_ptr<detail::thread_registry_backend>> retired_;
  bool global_{ false };

  friend auto global_registry() -> thread_registry&;
  friend void use_global_registry(thread_registry* value);
  friend class auto_register_current_thread;
  friend class advanced::composite_thread_registry;
};

[[nodiscard]] inline auto
global_registry() -> thread_registry&
{
  static thread_registry value(thread_registry::global_tag{});
  return value;
}

inline void
use_global_registry(thread_registry* value)
{
  detail::runtime_set_external_registry(value != nullptr && value->has_native() ? &value->native() : nullptr);
}

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
