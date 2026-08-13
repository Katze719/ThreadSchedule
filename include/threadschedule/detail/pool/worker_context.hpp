#pragma once

/**
 * @file detail/pool/worker_context.hpp
 * @brief Worker identity, CPU selection, and registration helpers.
 *
 * Internal implementation fragment included by backend.hpp inside
 * threadschedule::detail.
 */

template <typename Pool>
class worker_context_guard
{
public:
  worker_context_guard(Pool*& slot, Pool* current) noexcept : slot_(slot), previous_(slot)
  {
    slot_ = current;
  }

  ~worker_context_guard()
  {
    slot_ = previous_;
  }

  worker_context_guard(worker_context_guard const&) = delete;
  auto operator=(worker_context_guard const&) -> worker_context_guard& = delete;

private:
  Pool*& slot_;
  Pool* previous_;
};

[[noreturn]] inline void
throw_worker_deadlock()
{
  throw std::system_error(std::make_error_code(std::errc::resource_deadlock_would_occur),
                          "pool lifecycle operation called from its own worker");
}

inline auto
worker_thread_name(std::string const& name_prefix, size_t index) -> std::string
{
  std::string const suffix = "_" + std::to_string(index);
#ifndef _WIN32
  constexpr size_t linux_name_limit = 15;
  if (suffix.size() >= linux_name_limit)
    return suffix.substr(suffix.size() - linux_name_limit);
  return name_prefix.substr(0, linux_name_limit - suffix.size()) + suffix;
#else
  return name_prefix + suffix;
#endif
}

template <typename WorkerRange>
inline auto
configure_worker_threads(WorkerRange& workers, std::string const& name_prefix, native_scheduling_policy policy,
                         native_thread_priority priority) -> expected<void, std::error_code>
{
  std::error_code first_error;
  for (size_t i = 0; i < workers.size(); ++i)
    {
      std::string const thread_name = worker_thread_name(name_prefix, i);
      auto named = workers[i].set_name(thread_name);
      if (!named && !first_error)
        first_error = named.error();
      auto scheduled = workers[i].set_scheduling_policy(policy, priority);
      if (!scheduled && !first_error)
        first_error = scheduled.error();
    }
  if (first_error)
    return unexpected(first_error);
  return {};
}

template <typename WorkerRange>
inline auto
configure_worker_threads(WorkerRange& workers, native_thread_config const& config) -> expected<void, std::error_code>
{
  std::error_code first_error;
  for (size_t i = 0; i < workers.size(); ++i)
    {
      if (!config.name.empty())
        {
          std::string const thread_name = worker_thread_name(config.name, i);
          auto named = workers[i].set_name(thread_name);
          if (!named && !first_error)
            first_error = named.error();
        }
      auto scheduled = workers[i].configure(config.scheduling);
      if (!scheduled && !first_error)
        first_error = scheduled.error();
      if (config.affinity.has_value())
        {
          auto affinity = workers[i].set_affinity(*config.affinity);
          if (!affinity && !first_error)
            first_error = affinity.error();
        }
    }
  if (first_error)
    return unexpected(first_error);
  return {};
}

template <typename WorkerRange>
inline auto
set_worker_affinity(WorkerRange& workers, native_thread_affinity const& affinity) -> expected<void, std::error_code>
{
  std::error_code first_error;
  for (auto& worker : workers)
    {
      auto configured = worker.set_affinity(affinity);
      if (!configured && !first_error)
        first_error = configured.error();
    }
  if (first_error)
    return unexpected(first_error);
  return {};
}

template <typename WorkerRange>
inline auto
distribute_workers_across_cpus(WorkerRange& workers) -> expected<void, std::error_code>
{
  auto allowed = thread_info().get_affinity();
  if (!allowed)
    return unexpected(allowed.error());

  auto const cpus = allowed->get_cpus();
  if (cpus.empty())
    return unexpected(std::make_error_code(std::errc::invalid_argument));

  std::error_code first_error;
  for (size_t i = 0; i < workers.size(); ++i)
    {
      native_thread_affinity affinity({ cpus[i % cpus.size()] });
      auto configured = workers[i].set_affinity(affinity);
      if (!configured && !first_error)
        first_error = configured.error();
    }
  if (first_error)
    return unexpected(first_error);
  return {};
}

template <typename Pool, typename Iterator, typename F>
inline void
parallel_for_each_chunked(Pool& pool, Iterator begin, Iterator end, F&& func, size_t num_workers)
{
  auto const total = static_cast<size_t>(std::distance(begin, end));
  if (total == 0)
    return;

  size_t const chunk_size = (std::max)(size_t(1), total / (num_workers * 4));
  std::vector<std::future<void>> futures;
  auto it = begin;
  std::exception_ptr first_error;

  try
    {
      while (it != end)
        {
          auto remaining = static_cast<size_t>(std::distance(it, end));
          auto this_chunk = (std::min)(chunk_size, remaining);
          auto chunk_end = it;
          std::advance(chunk_end, this_chunk);

          futures.push_back(pool.submit(
              [it, chunk_end, &func]()
                {
                  for (auto cur = it; cur != chunk_end; ++cur)
                    func(*cur);
                }));

          it = chunk_end;
        }
    }
  catch (...)
    {
      first_error = std::current_exception();
    }

  for (auto& f : futures)
    {
      try
        {
          f.get();
        }
      catch (...)
        {
          if (!first_error)
            first_error = std::current_exception();
        }
    }

  if (first_error)
    std::rethrow_exception(first_error);
}
