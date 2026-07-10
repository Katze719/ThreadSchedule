#include <threadschedule/abi.hpp>

#if defined(_WIN32)
#if defined(STABLE_ABI_PRODUCER_BUILD)
#define STABLE_ABI_PRODUCER_API __declspec(dllexport)
#else
#define STABLE_ABI_PRODUCER_API __declspec(dllimport)
#endif
#else
#define STABLE_ABI_PRODUCER_API
#endif

namespace ts_abi = threadschedule::abi;

extern "C"
{

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ts_abi::status, ts_abi::registry_handle);
STABLE_ABI_PRODUCER_API auto stable_abi_producer_register(ts_abi::registry_handle registry) noexcept -> ts_abi::status
{
    return ts_abi::register_current_thread(registry, "stable-abi-producer", "stable-abi-runtime");
}

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ts_abi::status, ts_abi::registry_handle);
STABLE_ABI_PRODUCER_API auto stable_abi_producer_unregister(ts_abi::registry_handle registry) noexcept -> ts_abi::status
{
    return ts_abi::unregister_current_thread(registry);
}

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ts_abi::status,
                                          ts_abi::registry_handle,
                                          ts_abi::thread_info_callback,
                                          void*);
STABLE_ABI_PRODUCER_API auto stable_abi_producer_enumerate(ts_abi::registry_handle registry,
                                                           ts_abi::thread_info_callback callback,
                                                           void* user_data) noexcept -> ts_abi::status
{
    return ts_abi::registry_for_each(registry, callback, user_data);
}

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ts_abi::pool_handle, ts_abi::pool_config const*);
STABLE_ABI_PRODUCER_API auto stable_abi_producer_pool_create(ts_abi::pool_config const* config) noexcept
    -> ts_abi::pool_handle
{
    return threadschedule_abi_pool_create(config);
}

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ts_abi::status,
                                          ts_abi::pool_handle,
                                          ts_abi::task_callback,
                                          void*,
                                          ts_abi::task_completion_callback,
                                          void*);
STABLE_ABI_PRODUCER_API auto stable_abi_producer_pool_post(ts_abi::pool_handle pool,
                                                           ts_abi::task_callback callback,
                                                           void* user_data,
                                                           ts_abi::task_completion_callback completion,
                                                           void* completion_user_data) noexcept -> ts_abi::status
{
    return ts_abi::pool_post(pool, callback, user_data, completion, completion_user_data);
}

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ts_abi::status, ts_abi::pool_handle);
STABLE_ABI_PRODUCER_API auto stable_abi_producer_pool_wait(ts_abi::pool_handle pool) noexcept -> ts_abi::status
{
    return ts_abi::pool_wait(pool);
}

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ts_abi::status, ts_abi::pool_handle, ts_abi::pool_stats_view*);
STABLE_ABI_PRODUCER_API auto stable_abi_producer_pool_stats(ts_abi::pool_handle pool,
                                                            ts_abi::pool_stats_view* stats) noexcept -> ts_abi::status
{
    return threadschedule_abi_pool_stats(pool, stats);
}

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ts_abi::status, ts_abi::pool_handle, ts_abi::shutdown_policy);
STABLE_ABI_PRODUCER_API auto stable_abi_producer_pool_shutdown(ts_abi::pool_handle pool,
                                                               ts_abi::shutdown_policy policy) noexcept -> ts_abi::status
{
    return ts_abi::pool_shutdown(pool, policy);
}

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(void, ts_abi::pool_handle);
STABLE_ABI_PRODUCER_API void stable_abi_producer_pool_destroy(ts_abi::pool_handle pool) noexcept
{
    ts_abi::destroy_pool(pool);
}

} // extern "C"
