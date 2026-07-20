#include <threadschedule/thread_registry.hpp>

namespace threadschedule
{

namespace
{
thread_registry_backend* external_registry = nullptr;
thread_registry_backend local_registry;
} // namespace

THREADSCHEDULE_API auto
detail::runtime_registry() -> thread_registry_backend&
{
  return external_registry != nullptr ? *external_registry : local_registry;
}

THREADSCHEDULE_API void
detail::runtime_set_external_registry(thread_registry_backend* value)
{
  external_registry = value;
}

THREADSCHEDULE_API auto
registry() -> thread_registry_backend&
{
  return detail::runtime_registry();
}

THREADSCHEDULE_API void
set_external_registry(thread_registry_backend* value)
{
  detail::runtime_set_external_registry(value);
}

THREADSCHEDULE_API auto
current_build_mode() -> build_mode
{
  return build_mode::runtime;
}

} // namespace threadschedule
