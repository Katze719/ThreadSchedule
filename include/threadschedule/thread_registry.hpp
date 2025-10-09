#pragma once

#include "expected.hpp"
#include "scheduler_policy.hpp"
#include "thread_wrapper.hpp" // for ThreadInfo, ThreadAffinity
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <sys/types.h>
#endif

namespace threadschedule
{

#ifdef _WIN32
using Tid = unsigned long; // DWORD thread id
#else
using Tid = pid_t; // Linux TID via gettid()
#endif

struct RegisteredThreadInfo
{
    Tid tid{};
    std::thread::id stdId;
    std::string name;         // logical name/tag as provided during registration
    std::string componentTag; // optional grouping label
    bool alive{true};
    std::weak_ptr<class ThreadControlBlock> control; // optional control view
};

class ThreadControlBlock
{
  public:
    ThreadControlBlock() = default;
    ThreadControlBlock(const ThreadControlBlock &) = delete;
    auto operator=(const ThreadControlBlock &) -> ThreadControlBlock & = delete;
    ThreadControlBlock(ThreadControlBlock &&) = delete;
    auto operator=(ThreadControlBlock &&) -> ThreadControlBlock & = delete;

    ~ThreadControlBlock()
    {
#ifdef _WIN32
        if (handle_)
        {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
#endif
    }

    [[nodiscard]] auto tid() const noexcept -> Tid
    {
        return tid_;
    }
    [[nodiscard]] auto std_id() const noexcept -> std::thread::id
    {
        return stdId_;
    }
    [[nodiscard]] auto name() const noexcept -> const std::string &
    {
        return name_;
    }
    [[nodiscard]] auto component_tag() const noexcept -> const std::string &
    {
        return componentTag_;
    }

    [[nodiscard]] auto set_affinity(const ThreadAffinity &affinity) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        if (!handle_)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        using SetThreadGroupAffinityFn = BOOL(WINAPI *)(HANDLE, const GROUP_AFFINITY *, PGROUP_AFFINITY);
        HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
        if (hMod)
        {
            auto set_group_affinity = reinterpret_cast<SetThreadGroupAffinityFn>(
                reinterpret_cast<void *>(GetProcAddress(hMod, "SetThreadGroupAffinity")));
            if (set_group_affinity && affinity.has_any())
            {
                GROUP_AFFINITY ga{};
                ga.Mask = static_cast<KAFFINITY>(affinity.get_mask());
                ga.Group = affinity.get_group();
                if (set_group_affinity(handle_, &ga, nullptr) != 0)
                    return {};
                return unexpected(std::make_error_code(std::errc::operation_not_permitted));
            }
        }
        DWORD_PTR mask = static_cast<DWORD_PTR>(affinity.get_mask());
        if (SetThreadAffinityMask(handle_, mask) != 0)
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
#else
        if (pthread_setaffinity_np(pthreadHandle_, sizeof(cpu_set_t), &affinity.native_handle()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto set_priority(ThreadPriority priority) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        if (!handle_)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        int win_priority;
        int prio_val = priority.value();
        if (prio_val <= -10)
            win_priority = THREAD_PRIORITY_IDLE;
        else if (prio_val <= -5)
            win_priority = THREAD_PRIORITY_LOWEST;
        else if (prio_val < 0)
            win_priority = THREAD_PRIORITY_BELOW_NORMAL;
        else if (prio_val == 0)
            win_priority = THREAD_PRIORITY_NORMAL;
        else if (prio_val <= 5)
            win_priority = THREAD_PRIORITY_ABOVE_NORMAL;
        else if (prio_val <= 10)
            win_priority = THREAD_PRIORITY_HIGHEST;
        else
            win_priority = THREAD_PRIORITY_TIME_CRITICAL;
        if (SetThreadPriority(handle_, win_priority) != 0)
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
#else
        const int policy = SCHED_OTHER;
        auto params_result = SchedulerParams::create_for_policy(SchedulingPolicy::OTHER, priority);
        if (!params_result.has_value())
            return unexpected(params_result.error());
        if (pthread_setschedparam(pthreadHandle_, policy, &params_result.value()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto set_scheduling_policy(SchedulingPolicy policy,
                                                                        ThreadPriority priority) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        return set_priority(priority);
#else
        const int policy_int = static_cast<int>(policy);
        auto params_result = SchedulerParams::create_for_policy(policy, priority);
        if (!params_result.has_value())
            return unexpected(params_result.error());
        if (pthread_setschedparam(pthreadHandle_, policy_int, &params_result.value()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto set_name(const std::string &name) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        if (!handle_)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        using SetThreadDescriptionFn = HRESULT(WINAPI *)(HANDLE, PCWSTR);
        HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
        if (!hMod)
            return unexpected(std::make_error_code(std::errc::function_not_supported));
        auto set_desc = reinterpret_cast<SetThreadDescriptionFn>(
            reinterpret_cast<void *>(GetProcAddress(hMod, "SetThreadDescription")));
        if (!set_desc)
            return unexpected(std::make_error_code(std::errc::function_not_supported));
        std::wstring wide(name.begin(), name.end());
        if (SUCCEEDED(set_desc(handle_, wide.c_str())))
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
#else
        if (name.length() > 15)
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        if (pthread_setname_np(pthreadHandle_, name.c_str()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    static auto create_for_current_thread(const std::string &name,
                                                                         const std::string &componentTag) -> std::shared_ptr<ThreadControlBlock>
    {
        auto block = std::make_shared<ThreadControlBlock>();
        block->tid_ = ThreadInfo::get_thread_id();
        block->stdId_ = std::this_thread::get_id();
        block->name_ = name;
        block->componentTag_ = componentTag;
#ifdef _WIN32
        HANDLE realHandle = nullptr;
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &realHandle,
                        THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, 0);
        block->handle_ = realHandle;
#else
        block->pthreadHandle_ = pthread_self();
#endif
        return block;
    }

  private:
    Tid tid_{};
    std::thread::id stdId_;
    std::string name_;
    std::string componentTag_;
#ifdef _WIN32
    HANDLE handle_ = nullptr;
#else
    pthread_t pthreadHandle_{};
#endif
};

class ThreadRegistry
{
  public:
    ThreadRegistry() = default;
    ThreadRegistry(const ThreadRegistry &) = delete;
    auto operator=(const ThreadRegistry &) -> ThreadRegistry & = delete;

    // Register/unregister the CURRENT thread (to be called inside the running thread)
    void register_current_thread(std::string name = std::string(), std::string componentTag = std::string())
    {
        const Tid tid = ThreadInfo::get_thread_id();
        RegisteredThreadInfo info;
        info.tid = tid;
        info.stdId = std::this_thread::get_id();
        info.name = std::move(name);
        info.componentTag = std::move(componentTag);
        info.alive = true;

        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            threads_[tid] = std::move(info);
        }
    }

    void register_current_thread(const std::shared_ptr<ThreadControlBlock>& controlBlock)
    {
        if (!controlBlock)
            return;
        RegisteredThreadInfo info;
        info.tid = controlBlock->tid();
        info.stdId = controlBlock->std_id();
        info.name = controlBlock->name();
        info.componentTag = controlBlock->component_tag();
        info.alive = true;
        info.control = controlBlock;
        std::unique_lock<std::shared_mutex> lock(mutex_);
        threads_[info.tid] = std::move(info);
    }

    void unregister_current_thread()
    {
        const Tid tid = ThreadInfo::get_thread_id();
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = threads_.find(tid);
        if (it != threads_.end())
        {
            it->second.alive = false;
            threads_.erase(it);
        }
    }

    // Lookup
    [[nodiscard]] auto get(Tid tid) const -> std::optional<RegisteredThreadInfo>
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = threads_.find(tid);
        if (it == threads_.end())
            return std::nullopt;
        return it->second;
    }

    template <typename Fn>
    void for_each(Fn &&fn) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (const auto &kv : threads_)
        {
            fn(kv.second);
        }
    }

    // Bulk apply with predicate
    template <typename Predicate, typename Fn>
    void apply(Predicate &&pred, Fn &&fn) const
    {
        std::vector<RegisteredThreadInfo> snapshot;
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            snapshot.reserve(threads_.size());
            for (const auto &kv : threads_)
            {
                if (pred(kv.second))
                    snapshot.push_back(kv.second);
            }
        }
        for (const auto &entry : snapshot)
        {
            fn(entry);
        }
    }

    // Control operations (by Tid)
    [[nodiscard]] auto set_affinity(Tid tid, const ThreadAffinity &affinity) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        if (auto blk = lock_block_(tid))
            return blk->set_affinity(affinity);
        HANDLE h = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, static_cast<DWORD>(tid));
        if (!h)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        using SetThreadGroupAffinityFn = BOOL(WINAPI *)(HANDLE, const GROUP_AFFINITY *, PGROUP_AFFINITY);
        HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
        if (hMod)
        {
            auto set_group_affinity = reinterpret_cast<SetThreadGroupAffinityFn>(
                reinterpret_cast<void *>(GetProcAddress(hMod, "SetThreadGroupAffinity")));
            if (set_group_affinity && affinity.has_any())
            {
                GROUP_AFFINITY ga{};
                ga.Mask = static_cast<KAFFINITY>(affinity.get_mask());
                ga.Group = affinity.get_group();
                BOOL ok = set_group_affinity(h, &ga, nullptr);
                CloseHandle(h);
                if (ok)
                    return {};
                return unexpected(std::make_error_code(std::errc::operation_not_permitted));
            }
        }
        DWORD_PTR mask = static_cast<DWORD_PTR>(affinity.get_mask());
        BOOL ok = SetThreadAffinityMask(h, mask);
        CloseHandle(h);
        if (ok)
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
#else
        if (auto blk = lock_block(tid))
            return blk->set_affinity(affinity);
        if (tid <= 0)
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        if (sched_setaffinity(tid, sizeof(cpu_set_t), &affinity.native_handle()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto set_priority(Tid tid, ThreadPriority priority) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        if (auto blk = lock_block_(tid))
            return blk->set_priority(priority);
        HANDLE h = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, static_cast<DWORD>(tid));
        if (!h)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        int win_priority;
        int prio_val = priority.value();
        if (prio_val <= -10)
            win_priority = THREAD_PRIORITY_IDLE;
        else if (prio_val <= -5)
            win_priority = THREAD_PRIORITY_LOWEST;
        else if (prio_val < 0)
            win_priority = THREAD_PRIORITY_BELOW_NORMAL;
        else if (prio_val == 0)
            win_priority = THREAD_PRIORITY_NORMAL;
        else if (prio_val <= 5)
            win_priority = THREAD_PRIORITY_ABOVE_NORMAL;
        else if (prio_val <= 10)
            win_priority = THREAD_PRIORITY_HIGHEST;
        else
            win_priority = THREAD_PRIORITY_TIME_CRITICAL;
        BOOL ok = SetThreadPriority(h, win_priority);
        CloseHandle(h);
        if (ok)
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
#else
        if (auto blk = lock_block(tid))
            return blk->set_priority(priority);
        if (tid <= 0)
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        const int policy = SCHED_OTHER;
        auto params_result = SchedulerParams::create_for_policy(SchedulingPolicy::OTHER, priority);
        if (!params_result.has_value())
            return unexpected(params_result.error());
        if (sched_setscheduler(tid, policy, &params_result.value()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto set_scheduling_policy(Tid tid, SchedulingPolicy policy,
                                                                        ThreadPriority priority) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        if (auto blk = lock_block_(tid))
            return blk->set_scheduling_policy(policy, priority);
        return set_priority(tid, priority);
#else
        if (auto blk = lock_block(tid))
            return blk->set_scheduling_policy(policy, priority);
        if (tid <= 0)
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        int policy_int = static_cast<int>(policy);
        auto params_result = SchedulerParams::create_for_policy(policy, priority);
        if (!params_result.has_value())
            return unexpected(params_result.error());
        if (sched_setscheduler(tid, policy_int, &params_result.value()) == 0)
            return {};
        return unexpected(std::error_code(errno, std::generic_category()));
#endif
    }

    [[nodiscard]] auto set_name(Tid tid, const std::string &name) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        if (auto blk = lock_block_(tid))
            return blk->set_name(name);
        HANDLE h = OpenThread(THREAD_SET_LIMITED_INFORMATION | THREAD_SET_INFORMATION, FALSE, static_cast<DWORD>(tid));
        if (!h)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        using SetThreadDescriptionFn = HRESULT(WINAPI *)(HANDLE, PCWSTR);
        HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
        if (!hMod)
        {
            CloseHandle(h);
            return unexpected(std::make_error_code(std::errc::function_not_supported));
        }
        auto set_desc = reinterpret_cast<SetThreadDescriptionFn>(
            reinterpret_cast<void *>(GetProcAddress(hMod, "SetThreadDescription")));
        if (!set_desc)
        {
            CloseHandle(h);
            return unexpected(std::make_error_code(std::errc::function_not_supported));
        }
        std::wstring wide(name.begin(), name.end());
        HRESULT hr = set_desc(h, wide.c_str());
        CloseHandle(h);
        if (SUCCEEDED(hr))
            return {};
        return unexpected(std::make_error_code(std::errc::operation_not_permitted));
#else
        if (auto blk = lock_block(tid))
            return blk->set_name(name);
        if (name.length() > 15)
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        // Write to /proc/self/task/<tid>/comm
        if (tid <= 0)
            return unexpected(std::make_error_code(std::errc::invalid_argument));
        std::string path = std::string("/proc/self/task/") + std::to_string(tid) + "/comm";
        std::ofstream out(path);
        if (!out)
            return unexpected(std::error_code(errno, std::generic_category()));
        out << name;
        out.flush();
        if (!out)
            return unexpected(std::error_code(errno, std::generic_category()));
        return {};
#endif
    }

  private:
    [[nodiscard]] auto lock_block(Tid tid) const -> std::shared_ptr<ThreadControlBlock>
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = threads_.find(tid);
        if (it == threads_.end())
            return nullptr;
        return it->second.control.lock();
    }
    mutable std::shared_mutex mutex_;
    std::unordered_map<Tid, RegisteredThreadInfo> threads_;
};

// Registry access methods
inline auto registry_storage() -> ThreadRegistry *&
{
    static ThreadRegistry *external = nullptr;
    return external;
}

inline auto registry() -> ThreadRegistry &
{
    ThreadRegistry *&ext = registry_storage();
    if (ext != nullptr)
        return *ext;
    static ThreadRegistry local;
    return local;
}

inline void set_external_registry(ThreadRegistry *reg)
{
    registry_storage() = reg;
}

// Composite registry to aggregate multiple registries when explicit merging is desired
class CompositeThreadRegistry
{
  public:
    void attach(ThreadRegistry *reg)
    {
        if (reg == nullptr)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        registries_.push_back(reg);
    }

    template <typename Fn>
    void for_each(Fn &&fn) const
    {
        std::vector<ThreadRegistry *> regs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            regs = registries_;
        }
        for (auto *r : regs)
        {
            r->for_each(fn);
        }
    }

    template <typename Predicate, typename Fn>
    void apply(Predicate &&pred, Fn &&fn) const
    {
        std::vector<ThreadRegistry *> regs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            regs = registries_;
        }
        for (auto *r : regs)
        {
            r->apply(pred, fn);
        }
    }

  private:
    mutable std::mutex mutex_;
    std::vector<ThreadRegistry *> registries_;
};

// RAII helper to auto-register the current thread
class AutoRegisterCurrentThread
{
  public:
    explicit AutoRegisterCurrentThread(const std::string& name = std::string(), const std::string& componentTag = std::string())
        : active_(true), externalReg_(nullptr)
    {
        auto block = ThreadControlBlock::create_for_current_thread(name, componentTag);
        registry().register_current_thread(block);
    }

    explicit AutoRegisterCurrentThread(ThreadRegistry &reg, const std::string& name = std::string(),
                                       const std::string& componentTag = std::string())
        : active_(true), externalReg_(&reg)
    {
        auto block = ThreadControlBlock::create_for_current_thread(name, componentTag);
        externalReg_->register_current_thread(block);
    }
    ~AutoRegisterCurrentThread()
    {
        if (active_)
        {
            if (externalReg_ != nullptr)
                externalReg_->unregister_current_thread();
            else
                registry().unregister_current_thread();
        }
    }
    AutoRegisterCurrentThread(const AutoRegisterCurrentThread &) = delete;
    auto operator=(const AutoRegisterCurrentThread &) -> AutoRegisterCurrentThread & = delete;
    AutoRegisterCurrentThread(AutoRegisterCurrentThread &&other) noexcept : active_(other.active_)
    {
        other.active_ = false;
    }
    auto operator=(AutoRegisterCurrentThread &&other) noexcept -> AutoRegisterCurrentThread &
    {
        if (this != &other)
        {
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }

  private:
    bool active_;
    ThreadRegistry *externalReg_;
};

} // namespace threadschedule
