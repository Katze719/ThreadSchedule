#pragma once

/** @file detail/registry/registration_guard_backend.hpp
 *  @brief Internal RAII registration used by worker implementations.
 */

namespace threadschedule::detail
{
/** @brief Internal movable RAII guard for worker registration. */
class registration_guard_backend
{
public:
  explicit registration_guard_backend(std::string const& name = std::string(),
                                      std::string const& component = std::string())
      : registry_(&runtime_registry())
  {
    auto block = thread_control_block::create_for_current_thread();
    tid_ = block->tid();
    (void)block->set_name(name);
    active_ = registry_->register_guard(block, name, component);
    control_ = std::move(block);
  }

  explicit registration_guard_backend(thread_registry_backend& reg, std::string const& name = std::string(),
                                      std::string const& component = std::string())
      : registry_(&reg)
  {
    auto block = thread_control_block::create_for_current_thread();
    tid_ = block->tid();
    (void)block->set_name(name);
    active_ = registry_->register_guard(block, name, component);
    control_ = std::move(block);
  }
  ~registration_guard_backend()
  {
    if (active_)
      registry_->unregister_thread(tid_, control_.get());
  }
  registration_guard_backend(registration_guard_backend const&) = delete;
  auto operator=(registration_guard_backend const&) -> registration_guard_backend& = delete;
  registration_guard_backend(registration_guard_backend&& other) noexcept
      : active_(other.active_), registry_(other.registry_), tid_(other.tid_), control_(std::move(other.control_))
  {
    other.active_ = false;
    other.registry_ = nullptr;
    other.tid_ = {};
  }
  auto
  operator=(registration_guard_backend&& other) noexcept -> registration_guard_backend&
  {
    if (this != &other)
      {
        if (active_)
          registry_->unregister_thread(tid_, control_.get());
        active_ = other.active_;
        registry_ = other.registry_;
        tid_ = other.tid_;
        control_ = std::move(other.control_);
        other.active_ = false;
        other.registry_ = nullptr;
        other.tid_ = {};
      }
    return *this;
  }

private:
  bool active_{ false };
  thread_registry_backend* registry_{ nullptr };
  native_thread_id tid_{};
  std::shared_ptr<thread_control_block> control_;
};

} // namespace threadschedule::detail
