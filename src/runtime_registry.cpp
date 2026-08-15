#include <threadschedule/runtime.hpp>
#include <threadschedule/thread_registry.hpp>

namespace threadschedule::detail
{

namespace
{
thread_registry_backend* external_registry = nullptr;
thread_registry_backend local_registry;
} // namespace

THREADSCHEDULE_API auto
runtime_registry() -> thread_registry_backend&
{
  return external_registry != nullptr ? *external_registry : local_registry;
}

THREADSCHEDULE_API void
runtime_set_external_registry(thread_registry_backend* value)
{
  external_registry = value;
}

THREADSCHEDULE_API auto
runtime_exchange_external_registry(thread_registry_backend* value) -> thread_registry_backend*
{
  auto* const previous = external_registry;
  external_registry = value;
  return previous;
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
