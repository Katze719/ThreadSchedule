#include <threadschedule/abi.hpp>

namespace ts = threadschedule;

static_assert(ts::abi::is_abi_stable_v<ts::abi::registry_handle>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::pool_handle>);
static_assert(ts::abi::is_abi_stable_v<ts::abi::scheduling_request>);
static_assert(!ts::abi::is_abi_stable_v<ts::abi::registry_handle&>);
static_assert(!ts::abi::is_stable_signature_v<void(ts::abi::registry_handle&)>);

THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ts::abi::status, ts::abi::registry_handle);
THREADSCHEDULE_VALIDATE_STABLE_ABI_EXPORT(ts::abi::status,
                                          ts::abi::pool_handle,
                                          ts::abi::task_callback,
                                          void*);

int main()
{
#if defined(THREADSCHEDULE_RUNTIME)
    if (ts::abi::runtime_abi_version() != ts::abi::abi_version)
        return 1;
#endif

    ts::abi::scheduling_request request{};
    return request.version == ts::abi::abi_version ? 0 : 2;
}
