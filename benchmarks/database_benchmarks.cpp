#include <atomic>
#include <benchmark/benchmark.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <threadschedule/threadschedule.hpp>
#include <unordered_map>
#include <vector>

using namespace threadschedule;

// =============================================================================
// Realistic Database Workloads
// =============================================================================

// Simulated database record
struct DatabaseRecord
{
    std::string id;
    std::string user_id;
    std::string category;
    std::string title;
    std::string content;
    std::unordered_map<std::string, std::string> metadata;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point updated_at;
    bool is_active;
};

// Simulated database query result
struct QueryResult
{
    std::vector<DatabaseRecord> records;
    size_t total_count;
    double query_time_ms;
    std::string query_plan;
};

// Thread-safe in-memory database simulation
class SimulatedDatabase
{
  private:
    std::unordered_map<std::string, DatabaseRecord> records_;
    mutable std::shared_mutex mutex_;
    std::atomic<size_t> next_id_{1};

  public:
    // CRUD Operations
    std::string create_record(DatabaseRecord const& record)
    {
        std::unique_lock lock(mutex_);

        std::string id = "record_" + std::to_string(next_id_.fetch_add(1));
        DatabaseRecord new_record = record;
        new_record.id = id;
        new_record.created_at = std::chrono::steady_clock::now();
        new_record.updated_at = new_record.created_at;

        records_[id] = std::move(new_record);
        return id;
    }

    bool read_record(std::string const& id, DatabaseRecord& record)
    {
        std::shared_lock lock(mutex_);

        auto it = records_.find(id);
        if (it != records_.end())
        {
            record = it->second;
            return true;
        }
        return false;
    }

    bool update_record(std::string const& id, std::unordered_map<std::string, std::string> const& updates)
    {
        std::unique_lock lock(mutex_);

        auto it = records_.find(id);
        if (it != records_.end())
        {
            for (auto const& [key, value] : updates)
            {
                if (key == "title")
                    it->second.title = value;
                else if (key == "content")
                    it->second.content = value;
                else if (key == "category")
                    it->second.category = value;
                else
                    it->second.metadata[key] = value;
            }
            it->second.updated_at = std::chrono::steady_clock::now();
            return true;
        }
        return false;
    }

    bool delete_record(std::string const& id)
    {
        std::unique_lock lock(mutex_);

        auto it = records_.find(id);
        if (it != records_.end())
        {
            records_.erase(it);
            return true;
        }
        return false;
    }

    // Complex queries
    QueryResult query_by_user(std::string const& user_id, size_t limit = 100, size_t offset = 0)
    {
        QueryResult result;
        result.query_time_ms = simulate_query_latency();

        std::shared_lock lock(mutex_);

        for (auto const& [id, record] : records_)
        {
            if (record.user_id == user_id && record.is_active)
            {
                if (offset > 0)
                {
                    --offset;
                    continue;
                }
                if (result.records.size() >= limit)
                    break;
                result.records.push_back(record);
            }
        }

        result.total_count = result.records.size();
        result.query_plan = "Index scan on user_id";
        return result;
    }

    QueryResult query_by_category(std::string const& category, size_t limit = 100)
    {
        QueryResult result;
        result.query_time_ms = simulate_query_latency();

        std::shared_lock lock(mutex_);

        for (auto const& [id, record] : records_)
        {
            if (record.category == category && record.is_active)
            {
                if (result.records.size() >= limit)
                    break;
                result.records.push_back(record);
            }
        }

        result.total_count = result.records.size();
        result.query_plan = "Full table scan on category";
        return result;
    }

    QueryResult complex_aggregation_query(std::string const& user_id, [[maybe_unused]] std::string const& date_range)
    {
        QueryResult result;
        result.query_time_ms = simulate_query_latency();

        std::shared_lock lock(mutex_);

        // Simulate complex aggregation with joins and calculations
        std::unordered_map<std::string, size_t> category_counts;
        std::unordered_map<std::string, size_t> metadata_stats;
        size_t total_records = 0;

        for (auto const& [id, record] : records_)
        {
            if (record.user_id == user_id && record.is_active)
            {
                ++category_counts[record.category];
                ++total_records;

                for (auto const& [key, value] : record.metadata)
                {
                    ++metadata_stats[key];
                }
            }
        }

        // Create complex result with aggregations
        result.records.clear();
        DatabaseRecord aggregate_record;
        aggregate_record.id = "aggregate_" + user_id;
        aggregate_record.user_id = user_id;
        aggregate_record.title = "Aggregation Result";

        std::stringstream content;
        content << "Categories: ";
        for (auto const& [category, count] : category_counts)
        {
            content << category << "(" << count << ") ";
        }
        content << "| Metadata: ";
        for (auto const& [key, count] : metadata_stats)
        {
            content << key << "(" << count << ") ";
        }
        content << "| Total: " << total_records;

        aggregate_record.content = content.str();
        result.records.push_back(aggregate_record);
        result.total_count = 1;
        result.query_plan = "Complex aggregation with multiple table scans";

        return result;
    }

    // Transaction simulation
    bool transfer_ownership(std::string const& record_id, std::string const& new_user_id)
    {
        std::unique_lock lock(mutex_);

        auto it = records_.find(record_id);
        if (it != records_.end())
        {
            // Simulate transaction with rollback capability
            std::string old_user_id = it->second.user_id;

            // Simulate expensive validation
            simulate_transaction_validation(old_user_id, new_user_id);

            it->second.user_id = new_user_id;
            it->second.updated_at = std::chrono::steady_clock::now();

            // Simulate commit
            return true;
        }
        return false;
    }

    size_t size() const
    {
        std::shared_lock lock(mutex_);
        return records_.size();
    }

  private:
    double simulate_query_latency()
    {
        // Simulate realistic query latency (1-50ms)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> dist(1.0, 50.0);
        return dist(gen);
    }

    void simulate_transaction_validation(std::string const& old_user, std::string const& new_user)
    {
        // Simulate expensive validation logic
        volatile size_t validation_hash = 0;
        for (char c : old_user + new_user)
        {
            validation_hash = validation_hash * 31 + c;
        }
    }
};

// Database workload simulation
class DatabaseWorkloads
{
  public:
    // Simulate heavy CRUD operations
    static void perform_crud_operations(SimulatedDatabase& db, size_t num_operations)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> op_dist(0, 3); // 0=create, 1=read, 2=update, 3=delete
        std::uniform_int_distribution<int> category_dist(0, 4);
        std::uniform_int_distribution<int> user_dist(1, 100);

        std::vector<std::string> categories = {"documents", "images", "videos", "audio", "archives"};

        for (size_t i = 0; i < num_operations; ++i)
        {
            int operation = op_dist(gen);
            std::string user_id = "user_" + std::to_string(user_dist(gen));
            std::string category = categories[category_dist(gen)];

            switch (operation)
            {
            case 0: // CREATE
            {
                DatabaseRecord record;
                record.user_id = user_id;
                record.category = category;
                record.title = "File_" + std::to_string(i);
                record.content = "Content for file " + std::to_string(i);
                record.metadata["size"] = std::to_string((i % 1000) + 1);
                record.metadata["type"] = category;
                record.is_active = true;

                db.create_record(record);
                break;
            }
            case 1: // READ
            {
                std::vector<std::string> record_ids;
                {
                    // Simulate getting record IDs (expensive operation)
                    volatile size_t temp = 0;
                    for (size_t j = 1; j <= db.size(); ++j)
                    {
                        temp += j;
                    }
                }

                // In real scenario, this would be a query to get IDs
                if (!record_ids.empty())
                {
                    DatabaseRecord record;
                    db.read_record(record_ids[0], record);
                }
                break;
            }
            case 2: // UPDATE
            {
                // Simulate finding record to update
                std::string update_id = "record_" + std::to_string((i % 1000) + 1);
                std::unordered_map<std::string, std::string> updates = {
                    {"title", "Updated_File_" + std::to_string(i)},
                    {"content", "Updated content " + std::to_string(i)}};
                db.update_record(update_id, updates);
                break;
            }
            case 3: // DELETE
            {
                std::string delete_id = "record_" + std::to_string((i % 1000) + 1);
                db.delete_record(delete_id);
                break;
            }
            }
        }
    }

    // Simulate complex analytical queries
    static void perform_analytical_queries(SimulatedDatabase& db, size_t num_queries)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> user_dist(1, 100);
        std::uniform_int_distribution<int> category_dist(0, 4);

        std::vector<std::string> categories = {"documents", "images", "videos", "audio", "archives"};

        for (size_t i = 0; i < num_queries; ++i)
        {
            int query_type = i % 3;

            switch (query_type)
            {
            case 0: // User query
            {
                std::string user_id = "user_" + std::to_string(user_dist(gen));
                auto result = db.query_by_user(user_id, 50);
                break;
            }
            case 1: // Category query
            {
                std::string category = categories[category_dist(gen)];
                auto result = db.query_by_category(category, 100);
                break;
            }
            case 2: // Complex aggregation
            {
                std::string user_id = "user_" + std::to_string(user_dist(gen));
                auto result = db.complex_aggregation_query(user_id, "30d");
                break;
            }
            }
        }
    }

    // Simulate concurrent transactions
    static void perform_concurrent_transactions(SimulatedDatabase& db, size_t num_transactions)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> record_dist(1, 1000);

        for (size_t i = 0; i < num_transactions; ++i)
        {
            std::string record_id = "record_" + std::to_string(record_dist(gen));
            std::string new_user_id = "user_" + std::to_string((i % 50) + 1);

            // Simulate transaction with potential conflicts
            if (!db.transfer_ownership(record_id, new_user_id))
            {
                // Simulate rollback logic
                volatile size_t rollback_work = 0;
                for (size_t j = 0; j < 1000; ++j)
                {
                    rollback_work += j;
                }
            }
        }
    }
};

// =============================================================================
// Database Benchmarks
// =============================================================================

static void BM_Database_CRUD_Operations(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const operations_per_thread = state.range(1);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("db_worker");
    pool.distribute_across_cpus();

    SimulatedDatabase db;

    // Pre-populate database with initial data
    for (size_t i = 0; i < 1000; ++i)
    {
        DatabaseRecord record;
        record.user_id = "user_" + std::to_string((i % 100) + 1);
        record.category = (i % 5 == 0)   ? "documents"
                          : (i % 5 == 1) ? "images"
                          : (i % 5 == 2) ? "videos"
                          : (i % 5 == 3) ? "audio"
                                         : "archives";
        record.title = "Initial_File_" + std::to_string(i);
        record.content = "Initial content " + std::to_string(i);
        record.is_active = true;
        db.create_record(record);
    }

    for (auto _ : state)
    {
        std::atomic<size_t> completed_operations{0};
        std::atomic<size_t> failed_operations{0};

        // Submit CRUD operations
        for (size_t t = 0; t < num_threads; ++t)
        {
            pool.submit([&db, operations_per_thread, num_threads, &completed_operations, &failed_operations]() {
                try
                {
                    DatabaseWorkloads::perform_crud_operations(db, operations_per_thread / num_threads);
                    completed_operations.fetch_add(operations_per_thread / num_threads, std::memory_order_relaxed);
                }
                catch (std::exception const&)
                {
                    failed_operations.fetch_add(operations_per_thread / num_threads, std::memory_order_relaxed);
                }
            });
        }

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["completed_operations"] = benchmark::Counter(completed_operations.load());
        state.counters["failed_operations"] = benchmark::Counter(failed_operations.load());
        state.counters["success_rate_percent"] =
            benchmark::Counter(100.0 * completed_operations.load() /
                               std::max(completed_operations.load() + failed_operations.load(), size_t(1)));
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
        state.counters["avg_task_time_ms"] =
            benchmark::Counter(static_cast<double>(stats.avg_task_time.count()) / 1000.0);

        benchmark::DoNotOptimize(completed_operations.load());
    }

    state.SetItemsProcessed(state.iterations() * num_threads * operations_per_thread);
    state.SetLabel("threads=" + std::to_string(num_threads) +
                   " ops_per_thread=" + std::to_string(operations_per_thread));
}

static void BM_Database_AnalyticalQueries(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const queries_per_thread = state.range(1);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("analytics_worker");
    pool.distribute_across_cpus();

    SimulatedDatabase db;

    // Pre-populate with larger dataset for analytical queries
    for (size_t i = 0; i < 5000; ++i)
    {
        DatabaseRecord record;
        record.user_id = "user_" + std::to_string((i % 200) + 1);
        record.category = (i % 5 == 0)   ? "documents"
                          : (i % 5 == 1) ? "images"
                          : (i % 5 == 2) ? "videos"
                          : (i % 5 == 3) ? "audio"
                                         : "archives";
        record.title = "Analytics_File_" + std::to_string(i);
        record.content = "Analytics content " + std::to_string(i);
        record.metadata["size"] = std::to_string((i % 10000) + 1);
        record.metadata["priority"] = (i % 3 == 0) ? "high" : (i % 3 == 1) ? "medium" : "low";
        record.is_active = (i % 10 != 0); // 90% active
        db.create_record(record);
    }

    for (auto _ : state)
    {
        std::atomic<size_t> completed_queries{0};
        std::atomic<double> total_query_time{0.0};

        // Submit analytical query tasks
        for (size_t t = 0; t < num_threads; ++t)
        {
            pool.submit([&db, queries_per_thread, num_threads, &completed_queries, &total_query_time]() {
                auto start_time = std::chrono::steady_clock::now();

                try
                {
                    DatabaseWorkloads::perform_analytical_queries(db, queries_per_thread / num_threads);

                    auto end_time = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

                    completed_queries.fetch_add(queries_per_thread / num_threads, std::memory_order_relaxed);
                    total_query_time.fetch_add(duration.count(), std::memory_order_relaxed);
                }
                catch (std::exception const&)
                {
                    // Query failed
                }
            });
        }

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["completed_queries"] = benchmark::Counter(completed_queries.load());
        state.counters["avg_query_time_ms"] =
            benchmark::Counter(total_query_time.load() / std::max(completed_queries.load(), size_t(1)) / 1000.0);
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
        state.counters["queries_per_second"] =
            benchmark::Counter(completed_queries.load() / (total_query_time.load() / 1e6));

        benchmark::DoNotOptimize(completed_queries.load());
    }

    state.SetItemsProcessed(state.iterations() * num_threads * queries_per_thread);
    state.SetLabel("threads=" + std::to_string(num_threads) +
                   " queries_per_thread=" + std::to_string(queries_per_thread));
}

static void BM_Database_ConcurrentTransactions(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const transactions_per_thread = state.range(1);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("transaction_worker");
    pool.distribute_across_cpus();

    SimulatedDatabase db;

    // Pre-populate with records for transactions
    for (size_t i = 0; i < 1000; ++i)
    {
        DatabaseRecord record;
        record.user_id = "owner_" + std::to_string((i % 50) + 1);
        record.category = "transactions";
        record.title = "Transaction_File_" + std::to_string(i);
        record.content = "Transaction content " + std::to_string(i);
        record.is_active = true;
        db.create_record(record);
    }

    for (auto _ : state)
    {
        std::atomic<size_t> successful_transactions{0};
        std::atomic<size_t> failed_transactions{0};
        std::atomic<size_t> rollback_count{0};

        // Submit transaction tasks
        for (size_t t = 0; t < num_threads; ++t)
        {
            pool.submit([&db, transactions_per_thread, num_threads, &successful_transactions, &failed_transactions,
                         &rollback_count]() {
                try
                {
                    DatabaseWorkloads::perform_concurrent_transactions(db, transactions_per_thread / num_threads);
                    successful_transactions.fetch_add(transactions_per_thread / num_threads, std::memory_order_relaxed);
                }
                catch (std::exception const&)
                {
                    // Transaction failed, simulate rollback
                    rollback_count.fetch_add(1, std::memory_order_relaxed);
                    failed_transactions.fetch_add(transactions_per_thread / num_threads, std::memory_order_relaxed);
                }
            });
        }

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["successful_transactions"] = benchmark::Counter(successful_transactions.load());
        state.counters["failed_transactions"] = benchmark::Counter(failed_transactions.load());
        state.counters["rollback_count"] = benchmark::Counter(rollback_count.load());
        state.counters["success_rate_percent"] =
            benchmark::Counter(100.0 * successful_transactions.load() /
                               std::max(successful_transactions.load() + failed_transactions.load(), size_t(1)));
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
        state.counters["avg_task_time_ms"] =
            benchmark::Counter(static_cast<double>(stats.avg_task_time.count()) / 1000.0);

        benchmark::DoNotOptimize(successful_transactions.load());
    }

    state.SetItemsProcessed(state.iterations() * num_threads * transactions_per_thread);
    state.SetLabel("threads=" + std::to_string(num_threads) +
                   " txns_per_thread=" + std::to_string(transactions_per_thread));
}

static void BM_Database_MixedWorkload(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const total_operations = state.range(1);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("mixed_worker");
    pool.distribute_across_cpus();

    SimulatedDatabase db;

    // Pre-populate with mixed data
    for (size_t i = 0; i < 2000; ++i)
    {
        DatabaseRecord record;
        record.user_id = "user_" + std::to_string((i % 100) + 1);
        record.category = (i % 5 == 0)   ? "documents"
                          : (i % 5 == 1) ? "images"
                          : (i % 5 == 2) ? "videos"
                          : (i % 5 == 3) ? "audio"
                                         : "archives";
        record.title = "Mixed_File_" + std::to_string(i);
        record.content = "Mixed content " + std::to_string(i);
        record.is_active = (i % 8 != 0); // 87.5% active
        db.create_record(record);
    }

    for (auto _ : state)
    {
        std::atomic<size_t> crud_operations{0};
        std::atomic<size_t> analytical_queries{0};
        std::atomic<size_t> transactions{0};
        std::atomic<double> total_latency_ms{0.0};

        // Submit mixed workload tasks
        for (size_t i = 0; i < total_operations; ++i)
        {
            int workload_type = i % 3;

            auto start_time = std::chrono::steady_clock::now();

            if (workload_type == 0) // CRUD
            {
                pool.submit([&db, &crud_operations, &total_latency_ms, start_time]() {
                    DatabaseWorkloads::perform_crud_operations(db, 1);
                    auto end_time = std::chrono::steady_clock::now();
                    double latency =
                        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() / 1000.0;
                    crud_operations.fetch_add(1, std::memory_order_relaxed);
                    total_latency_ms.fetch_add(latency, std::memory_order_relaxed);
                });
            }
            else if (workload_type == 1) // Analytics
            {
                pool.submit([&db, &analytical_queries, &total_latency_ms, start_time]() {
                    DatabaseWorkloads::perform_analytical_queries(db, 1);
                    auto end_time = std::chrono::steady_clock::now();
                    double latency =
                        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() / 1000.0;
                    analytical_queries.fetch_add(1, std::memory_order_relaxed);
                    total_latency_ms.fetch_add(latency, std::memory_order_relaxed);
                });
            }
            else // Transactions
            {
                pool.submit([&db, &transactions, &total_latency_ms, start_time]() {
                    DatabaseWorkloads::perform_concurrent_transactions(db, 1);
                    auto end_time = std::chrono::steady_clock::now();
                    double latency =
                        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() / 1000.0;
                    transactions.fetch_add(1, std::memory_order_relaxed);
                    total_latency_ms.fetch_add(latency, std::memory_order_relaxed);
                });
            }
        }

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["crud_operations"] = benchmark::Counter(crud_operations.load());
        state.counters["analytical_queries"] = benchmark::Counter(analytical_queries.load());
        state.counters["transactions"] = benchmark::Counter(transactions.load());
        state.counters["avg_latency_ms"] =
            benchmark::Counter(total_latency_ms.load() / std::max(total_operations, size_t(1)));
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
        state.counters["operations_per_second"] =
            benchmark::Counter(total_operations / (stats.avg_task_time.count() / 1e9));

        benchmark::DoNotOptimize(crud_operations.load());
    }

    state.SetItemsProcessed(state.iterations() * total_operations);
    state.SetLabel("threads=" + std::to_string(num_threads) + " total_ops=" + std::to_string(total_operations));
}

// =============================================================================
// Registration
// =============================================================================

BENCHMARK(BM_Database_CRUD_Operations)
    ->Args({2, 1000}) // 2 threads, 1000 ops each
    ->Args({4, 1000}) // 4 threads, 1000 ops each
    ->Args({8, 1000}) // 8 threads, 1000 ops each
    ->Args({4, 5000}) // 4 threads, 5000 ops each
    ->Args({8, 5000}) // 8 threads, 5000 ops each
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Database_AnalyticalQueries)
    ->Args({2, 100}) // 2 threads, 100 queries each
    ->Args({4, 100}) // 4 threads, 100 queries each
    ->Args({8, 100}) // 8 threads, 100 queries each
    ->Args({4, 500}) // 4 threads, 500 queries each
    ->Args({8, 500}) // 8 threads, 500 queries each
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Database_ConcurrentTransactions)
    ->Args({2, 500})  // 2 threads, 500 txns each
    ->Args({4, 500})  // 4 threads, 500 txns each
    ->Args({8, 500})  // 8 threads, 500 txns each
    ->Args({4, 1000}) // 4 threads, 1000 txns each
    ->Args({8, 1000}) // 8 threads, 1000 txns each
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Database_MixedWorkload)
    ->Args({2, 1000}) // 2 threads, 1000 mixed ops
    ->Args({4, 1000}) // 4 threads, 1000 mixed ops
    ->Args({8, 1000}) // 8 threads, 1000 mixed ops
    ->Args({4, 5000}) // 4 threads, 5000 mixed ops
    ->Args({8, 5000}) // 8 threads, 5000 mixed ops
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
