#include <threadschedule/runtime.hpp>
#include <threadschedule/thread_registry.hpp>

namespace threadschedule::detail
{

namespace
{
std::shared_ptr<external_registry_binding_state> external_registry;
thread_registry_backend local_registry;
} // namespace

THREADSCHEDULE_API auto
runtime_registry() -> thread_registry_backend&
{
  return external_registry && external_registry->registry != nullptr ? *external_registry->registry : local_registry;
}

THREADSCHEDULE_API void
runtime_set_external_registry(thread_registry_backend* value)
{
  external_registry = value == nullptr ? nullptr : std::make_shared<external_registry_binding_state>(value);
}

THREADSCHEDULE_API auto
runtime_exchange_external_registry(thread_registry_backend* value) -> thread_registry_backend*
{
  auto* const previous = external_registry ? external_registry->registry : nullptr;
  runtime_set_external_registry(value);
  return previous;
}

THREADSCHEDULE_API void
runtime_set_external_registry_state(std::shared_ptr<external_registry_binding_state> value)
{
  external_registry = std::move(value);
}

THREADSCHEDULE_API auto
runtime_exchange_external_registry_state(std::shared_ptr<external_registry_binding_state> value)
    -> std::shared_ptr<external_registry_binding_state>
{
  auto previous = std::move(external_registry);
  external_registry = std::move(value);
  return previous;
}

THREADSCHEDULE_API auto
runtime_external_registry_state() -> std::shared_ptr<external_registry_binding_state>
{
  return external_registry;
}

} // namespace threadschedule::detail

namespace threadschedule
{

THREADSCHEDULE_API auto
current_build_mode() -> build_mode
{
  return build_mode::runtime;
}

} // namespace threadschedule
