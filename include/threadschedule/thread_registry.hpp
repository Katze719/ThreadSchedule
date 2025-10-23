#pragma once

#include "expected.hpp"
#include "scheduler_policy.hpp"
#include "thread_wrapper.hpp" // for ThreadInfo, ThreadAffinity
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <tuple>
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

// Optional export macro for building a runtime (shared/dll) variant
#if defined(_WIN32) || defined(_WIN64)
#if defined(THREADSCHEDULE_EXPORTS)
#define THREADSCHEDULE_API __declspec(dllexport)
#else
#define THREADSCHEDULE_API __declspec(dllimport)
#endif
#else
#define THREADSCHEDULE_API
#endif

#ifdef _WIN32
using Tid = unsigned long; // DWORD thread id
#else
using Tid = pid_t; // Linux TID via gettid()
#endif

struct RegisteredThreadInfo
{
    Tid tid{};
    std::thread::id stdId;
    std::string name;
    std::string componentTag;
    bool alive{true};
    std::shared_ptr<class ThreadControlBlock> control;
};

class ThreadControlBlock
{
  public:
    ThreadControlBlock() = default;
    ThreadControlBlock(ThreadControlBlock const&) = delete;
    auto operator=(ThreadControlBlock const&) -> ThreadControlBlock& = delete;
    ThreadControlBlock(ThreadControlBlock&&) = delete;
    auto operator=(ThreadControlBlock&&) -> ThreadControlBlock& = delete;

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
    // Removed name/component metadata from control block; metadata lives in RegisteredThreadInfo

    [[nodiscard]] auto set_affinity(ThreadAffinity const& affinity) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        if (!handle_)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        using SetThreadGroupAffinityFn = BOOL(WINAPI*)(HANDLE, const GROUP_AFFINITY*, PGROUP_AFFINITY);
        HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
        if (hMod)
        {
            auto set_group_affinity = reinterpret_cast<SetThreadGroupAffinityFn>(
                reinterpret_cast<void*>(GetProcAddress(hMod, "SetThreadGroupAffinity")));
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

    [[nodiscard]] auto set_scheduling_policy(SchedulingPolicy policy, ThreadPriority priority) const
        -> expected<void, std::error_code>
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

    [[nodiscard]] auto set_name(std::string const& name) const -> expected<void, std::error_code>
    {
#ifdef _WIN32
        if (!handle_)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
        HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
        if (!hMod)
            return unexpected(std::make_error_code(std::errc::function_not_supported));
        auto set_desc = reinterpret_cast<SetThreadDescriptionFn>(
            reinterpret_cast<void*>(GetProcAddress(hMod, "SetThreadDescription")));
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

    static auto create_for_current_thread() -> std::shared_ptr<ThreadControlBlock>
    {
        auto block = std::make_shared<ThreadControlBlock>();
        block->tid_ = ThreadInfo::get_thread_id();
        block->stdId_ = std::this_thread::get_id();
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
    ThreadRegistry(ThreadRegistry const&) = delete;
    auto operator=(ThreadRegistry const&) -> ThreadRegistry& = delete;

    // Register/unregister the CURRENT thread (to be called inside the running thread)
    void register_current_thread(std::string name = std::string(), std::string componentTag = std::string())
    {
        Tid const tid = ThreadInfo::get_thread_id();
        RegisteredThreadInfo info;
        info.tid = tid;
        info.stdId = std::this_thread::get_id();
        info.name = std::move(name);
        info.componentTag = std::move(componentTag);
        info.alive = true;

        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            auto it = threads_.find(tid);
            if (it == threads_.end())
            {
                auto stored = info; // copy for callback
                threads_.emplace(tid, std::move(info));
                if (onRegister_)
                {
                    auto cb = onRegister_;
                    lock.unlock();
                    cb(stored);
                }
            }
            else
            {
                // Duplicate registration of the same TID is a no-op (first registration wins)
            }
        }
    }

    void register_current_thread(std::shared_ptr<ThreadControlBlock> const& controlBlock,
                                 std::string name = std::string(), std::string componentTag = std::string())
    {
        if (!controlBlock)
            return;
        RegisteredThreadInfo info;
        info.tid = controlBlock->tid();
        info.stdId = controlBlock->std_id();
        info.name = std::move(name);
        info.componentTag = std::move(componentTag);
        info.alive = true;
        info.control = controlBlock;
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = threads_.find(info.tid);
        if (it == threads_.end())
        {
            auto stored = info; // copy for callback
            threads_.emplace(info.tid, std::move(info));
            if (onRegister_)
            {
                auto cb = onRegister_;
                lock.unlock();
                cb(stored);
            }
        }
        else
        {
            // Duplicate registration of the same TID is a no-op (first registration wins)
        }
    }

    void unregister_current_thread()
    {
        Tid const tid = ThreadInfo::get_thread_id();
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = threads_.find(tid);
        if (it != threads_.end())
        {
            it->second.alive = false;
            auto info = it->second;
            threads_.erase(it);
            if (onUnregister_)
            {
                auto cb = onUnregister_;
                lock.unlock();
                cb(info);
            }
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

    // Chainable query API
    class QueryView
    {
      public:
        explicit QueryView(std::vector<RegisteredThreadInfo> entries) : entries_(std::move(entries))
        {
        }

        template <typename Predicate>
        auto filter(Predicate&& pred) const -> QueryView
        {
            std::vector<RegisteredThreadInfo> filtered;
            filtered.reserve(entries_.size());
            for (auto const& entry : entries_)
            {
                if (pred(entry))
                    filtered.push_back(entry);
            }
            return QueryView(std::move(filtered));
        }

        template <typename Fn>
        void for_each(Fn&& fn) const
        {
            for (auto const& entry : entries_)
            {
                fn(entry);
            }
        }

        [[nodiscard]] auto count() const -> size_t
        {
            return entries_.size();
        }

        [[nodiscard]] auto empty() const -> bool
        {
            return entries_.empty();
        }

        [[nodiscard]] auto entries() const -> std::vector<RegisteredThreadInfo> const&
        {
            return entries_;
        }

        // Transform entries to a vector of another type
        template <typename Fn>
        [[nodiscard]] auto map(Fn&& fn) const -> std::vector<std::invoke_result_t<Fn, RegisteredThreadInfo const&>>
        {
            std::vector<std::invoke_result_t<Fn, RegisteredThreadInfo const&>> result;
            result.reserve(entries_.size());
            for (auto const& entry : entries_)
            {
                result.push_back(fn(entry));
            }
            return result;
        }

        // Find first entry matching predicate
        template <typename Predicate>
        [[nodiscard]] auto find_if(Predicate&& pred) const -> std::optional<RegisteredThreadInfo>
        {
            for (auto const& entry : entries_)
            {
                if (pred(entry))
                    return entry;
            }
            return std::nullopt;
        }

        template <typename Predicate>
        [[nodiscard]] auto any(Predicate&& pred) const -> bool
        {
            for (auto const& entry : entries_)
            {
                if (pred(entry))
                    return true;
            }
            return false;
        }

        template <typename Predicate>
        [[nodiscard]] auto all(Predicate&& pred) const -> bool
        {
            for (auto const& entry : entries_)
            {
                if (!pred(entry))
                    return false;
            }
            return true;
        }

        template <typename Predicate>
        [[nodiscard]] auto none(Predicate&& pred) const -> bool
        {
            return !any(std::forward<Predicate>(pred));
        }

        [[nodiscard]] auto take(size_t n) const -> QueryView
        {
            auto result = entries_;
            if (result.size() > n)
                result.resize(n);
            return QueryView(std::move(result));
        }

        [[nodiscard]] auto skip(size_t n) const -> QueryView
        {
            std::vector<RegisteredThreadInfo> result;
            if (n < entries_.size())
            {
                result.assign(entries_.begin() + n, entries_.end());
            }
            return QueryView(std::move(result));
        }

      private:
        std::vector<RegisteredThreadInfo> entries_;
    };

    // Create a query view over all registered threads
    [[nodiscard]] auto query() const -> QueryView
    {
        std::vector<RegisteredThreadInfo> snapshot;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        snapshot.reserve(threads_.size());
        for (auto const& kv : threads_)
        {
            snapshot.push_back(kv.second);
        }
        return QueryView(std::move(snapshot));
    }

    template <typename Predicate>
    [[nodiscard]] auto filter(Predicate&& pred) const -> QueryView
    {
        return query().filter(std::forward<Predicate>(pred));
    }

    [[nodiscard]] auto count() const -> size_t
    {
        return query().count();
    }

    [[nodiscard]] auto empty() const -> bool
    {
        return query().empty();
    }

    template <typename Fn>
    void for_each(Fn&& fn) const
    {
        query().for_each(std::forward<Fn>(fn));
    }

    template <typename Predicate, typename Fn>
    void apply(Predicate&& pred, Fn&& fn) const
    {
        query().filter(std::forward<Predicate>(pred)).for_each(std::forward<Fn>(fn));
    }

    template <typename Fn>
    [[nodiscard]] auto map(Fn&& fn) const -> std::vector<std::invoke_result_t<Fn, RegisteredThreadInfo const&>>
    {
        return query().map(std::forward<Fn>(fn));
    }

    template <typename Predicate>
    [[nodiscard]] auto find_if(Predicate&& pred) const -> std::optional<RegisteredThreadInfo>
    {
        return query().find_if(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto any(Predicate&& pred) const -> bool
    {
        return query().any(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto all(Predicate&& pred) const -> bool
    {
        return query().all(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto none(Predicate&& pred) const -> bool
    {
        return query().none(std::forward<Predicate>(pred));
    }

    [[nodiscard]] auto take(size_t n) const -> QueryView
    {
        return query().take(n);
    }

    [[nodiscard]] auto skip(size_t n) const -> QueryView
    {
        return query().skip(n);
    }

    [[nodiscard]] auto set_affinity(Tid tid, ThreadAffinity const& affinity) const -> expected<void, std::error_code>
    {
        auto blk = lock_block(tid);
        if (!blk)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return blk->set_affinity(affinity);
    }

    [[nodiscard]] auto set_priority(Tid tid, ThreadPriority priority) const -> expected<void, std::error_code>
    {
        auto blk = lock_block(tid);
        if (!blk)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return blk->set_priority(priority);
    }

    [[nodiscard]] auto set_scheduling_policy(Tid tid, SchedulingPolicy policy, ThreadPriority priority) const
        -> expected<void, std::error_code>
    {
        auto blk = lock_block(tid);
        if (!blk)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return blk->set_scheduling_policy(policy, priority);
    }

    [[nodiscard]] auto set_name(Tid tid, std::string const& name) const -> expected<void, std::error_code>
    {
        auto blk = lock_block(tid);
        if (!blk)
            return unexpected(std::make_error_code(std::errc::no_such_process));
        return blk->set_name(name);
    }

    // Register/unregister hooks (system integration)
    void set_on_register(std::function<void(RegisteredThreadInfo const&)> cb)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        onRegister_ = std::move(cb);
    }

    void set_on_unregister(std::function<void(RegisteredThreadInfo const&)> cb)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        onUnregister_ = std::move(cb);
    }

  private:
    [[nodiscard]] auto lock_block(Tid tid) const -> std::shared_ptr<ThreadControlBlock>
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = threads_.find(tid);
        if (it == threads_.end())
            return nullptr;
        return it->second.control;
    }
    mutable std::shared_mutex mutex_;
    std::unordered_map<Tid, RegisteredThreadInfo> threads_;

    // Integration hooks
    std::function<void(RegisteredThreadInfo const&)> onRegister_;
    std::function<void(RegisteredThreadInfo const&)> onUnregister_;
};

// Registry access methods
#if defined(THREADSCHEDULE_RUNTIME)
// Declarations only; implemented in the runtime translation unit
THREADSCHEDULE_API auto registry() -> ThreadRegistry&;
THREADSCHEDULE_API void set_external_registry(ThreadRegistry* reg);
#else
inline auto registry_storage() -> ThreadRegistry*&
{
    static ThreadRegistry* external = nullptr;
    return external;
}

inline auto registry() -> ThreadRegistry&
{
    ThreadRegistry*& ext = registry_storage();
    if (ext != nullptr)
        return *ext;
    static ThreadRegistry local;
    return local;
}

inline void set_external_registry(ThreadRegistry* reg)
{
    registry_storage() = reg;
}
#endif

// Composite registry to aggregate multiple registries when explicit merging is desired
class CompositeThreadRegistry
{
  public:
    void attach(ThreadRegistry* reg)
    {
        if (reg == nullptr)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        registries_.push_back(reg);
    }

    // Chainable query API
    [[nodiscard]] auto query() const -> ThreadRegistry::QueryView
    {
        std::vector<RegisteredThreadInfo> merged;
        std::vector<ThreadRegistry*> regs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            regs = registries_;
        }
        for (auto* r : regs)
        {
            auto view = r->query();
            auto const& entries = view.entries();
            merged.insert(merged.end(), entries.begin(), entries.end());
        }
        return ThreadRegistry::QueryView(std::move(merged));
    }

    template <typename Predicate>
    [[nodiscard]] auto filter(Predicate&& pred) const -> ThreadRegistry::QueryView
    {
        return query().filter(std::forward<Predicate>(pred));
    }

    [[nodiscard]] auto count() const -> size_t
    {
        return query().count();
    }

    [[nodiscard]] auto empty() const -> bool
    {
        return query().empty();
    }

    template <typename Fn>
    void for_each(Fn&& fn) const
    {
        query().for_each(std::forward<Fn>(fn));
    }

    template <typename Predicate, typename Fn>
    void apply(Predicate&& pred, Fn&& fn) const
    {
        query().filter(std::forward<Predicate>(pred)).for_each(std::forward<Fn>(fn));
    }

    template <typename Fn>
    [[nodiscard]] auto map(Fn&& fn) const -> std::vector<std::invoke_result_t<Fn, RegisteredThreadInfo const&>>
    {
        return query().map(std::forward<Fn>(fn));
    }

    template <typename Predicate>
    [[nodiscard]] auto find_if(Predicate&& pred) const -> std::optional<RegisteredThreadInfo>
    {
        return query().find_if(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto any(Predicate&& pred) const -> bool
    {
        return query().any(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto all(Predicate&& pred) const -> bool
    {
        return query().all(std::forward<Predicate>(pred));
    }

    template <typename Predicate>
    [[nodiscard]] auto none(Predicate&& pred) const -> bool
    {
        return query().none(std::forward<Predicate>(pred));
    }

    [[nodiscard]] auto take(size_t n) const -> ThreadRegistry::QueryView
    {
        return query().take(n);
    }

    [[nodiscard]] auto skip(size_t n) const -> ThreadRegistry::QueryView
    {
        return query().skip(n);
    }

  private:
    mutable std::mutex mutex_;
    std::vector<ThreadRegistry*> registries_;
};

// RAII helper to auto-register the current thread
class AutoRegisterCurrentThread
{
  public:
    explicit AutoRegisterCurrentThread(std::string const& name = std::string(),
                                       std::string const& componentTag = std::string())
        : active_(true), externalReg_(nullptr)
    {
        auto block = ThreadControlBlock::create_for_current_thread();
        (void)block->set_name(name);
        registry().register_current_thread(block, name, componentTag);
    }

    explicit AutoRegisterCurrentThread(ThreadRegistry& reg, std::string const& name = std::string(),
                                       std::string const& componentTag = std::string())
        : active_(true), externalReg_(&reg)
    {
        auto block = ThreadControlBlock::create_for_current_thread();
        (void)block->set_name(name);
        externalReg_->register_current_thread(block, name, componentTag);
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
    AutoRegisterCurrentThread(AutoRegisterCurrentThread const&) = delete;
    auto operator=(AutoRegisterCurrentThread const&) -> AutoRegisterCurrentThread& = delete;
    AutoRegisterCurrentThread(AutoRegisterCurrentThread&& other) noexcept : active_(other.active_)
    {
        other.active_ = false;
    }
    auto operator=(AutoRegisterCurrentThread&& other) noexcept -> AutoRegisterCurrentThread&
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
    ThreadRegistry* externalReg_;
};

} // namespace threadschedule

#ifndef _WIN32
// Helper: attach a TID to a cgroup directory (cgroup v2 tries cgroup.threads, then tasks, then cgroup.procs)
namespace threadschedule
{
inline auto cgroup_attach_tid(std::string const& cgroupDir, Tid tid) -> expected<void, std::error_code>
{
    std::vector<std::string> candidates = {"cgroup.threads", "tasks", "cgroup.procs"};
    for (auto const& file : candidates)
    {
        std::string path = cgroupDir + "/" + file;
        std::ofstream out(path);
        if (!out)
            continue;
        out << tid;
        out.flush();
        if (out)
            return {};
    }
    return unexpected(std::make_error_code(std::errc::operation_not_permitted));
}
} // namespace threadschedule
#endif
