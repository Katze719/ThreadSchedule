#include <atomic>
#include <benchmark/benchmark.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <threadschedule/threadschedule.hpp>
#include <unordered_map>
#include <vector>

using namespace threadschedule;
using json = nlohmann::json;

// =============================================================================
// Realistic Web Server Workloads
// =============================================================================

// Simulated user session data
struct UserSession
{
    std::string user_id;
    std::string session_token;
    std::unordered_map<std::string, std::string> preferences;
    std::vector<std::string> recent_actions;
    std::chrono::steady_clock::time_point last_activity;
};

// Thread-safe session store
class SessionStore
{
  private:
    std::unordered_map<std::string, std::shared_ptr<UserSession>> sessions_;
    mutable std::shared_mutex mutex_;

  public:
    void store_session(std::string session_id, std::shared_ptr<UserSession> session)
    {
        std::unique_lock lock(mutex_);
        sessions_[session_id] = session;
    }

    std::shared_ptr<UserSession> get_session(std::string const& session_id)
    {
        std::shared_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        return (it != sessions_.end()) ? it->second : nullptr;
    }

    void cleanup_expired_sessions()
    {
        auto now = std::chrono::steady_clock::now();
        std::unique_lock lock(mutex_);

        auto it = sessions_.begin();
        while (it != sessions_.end())
        {
            if (now - it->second->last_activity > std::chrono::minutes(30))
            {
                it = sessions_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    size_t size() const
    {
        std::shared_lock lock(mutex_);
        return sessions_.size();
    }
};

// Simulated HTTP request
struct HttpRequest
{
    std::string method;
    std::string path;
    std::string user_agent;
    std::string session_id;
    json body;
    std::unordered_map<std::string, std::string> headers;
    std::chrono::steady_clock::time_point timestamp;
};

// Simulated HTTP response
struct HttpResponse
{
    int status_code;
    std::string content_type;
    json body;
    std::unordered_map<std::string, std::string> headers;
};

// Thread-safe request queue
class RequestQueue
{
  private:
    std::queue<HttpRequest> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool stopped_ = false;

  public:
    void push(HttpRequest request)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stopped_)
        {
            queue_.push(std::move(request));
            condition_.notify_one();
        }
    }

    bool pop(HttpRequest& request, std::chrono::milliseconds timeout = std::chrono::milliseconds(100))
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (condition_.wait_for(lock, timeout, [this] { return !queue_.empty() || stopped_; }))
        {
            if (!queue_.empty())
            {
                request = std::move(queue_.front());
                queue_.pop();
                return true;
            }
        }
        return false;
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        condition_.notify_all();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
};

// Web server workload simulation
class WebServerWorkloads
{
  public:
    // Simulate JSON API request processing
    static HttpResponse process_json_api_request(HttpRequest const& request, SessionStore& sessions)
    {
        HttpResponse response;
        response.status_code = 200;
        response.content_type = "application/json";

        // Simulate session validation (expensive operation)
        auto session = sessions.get_session(request.session_id);
        if (!session)
        {
            response.status_code = 401;
            response.body = {{"error", "Invalid session"}};
            return response;
        }

        // Simulate heavy JSON processing
        json result;
        if (request.path == "/api/users")
        {
            // Simulate database-like operation with complex JSON construction
            result = simulate_user_database_query(request.body);
        }
        else if (request.path == "/api/analytics")
        {
            // Simulate analytics computation
            result = simulate_analytics_computation(session->user_id, request.body);
        }
        else if (request.path == "/api/recommendations")
        {
            // Simulate recommendation engine
            result = simulate_recommendation_engine(session->preferences, request.body);
        }

        response.body = result;
        return response;
    }

    // Simulate file upload processing
    static HttpResponse process_file_upload(HttpRequest const& request)
    {
        HttpResponse response;
        response.content_type = "application/json";

        // Simulate expensive file processing operations
        volatile size_t hash = 0;
        std::string_view content = request.body.value("content", "");

        // Simulate content hashing (CPU intensive)
        for (size_t i = 0; i < content.size(); ++i)
        {
            hash = hash * 31 + content[i];
        }

        // Simulate image processing if it's an image upload
        if (request.headers.count("content-type") &&
            request.headers.at("content-type").find("image/") != std::string::npos)
        {
            // Simulate image resizing/thumbnail generation
            simulate_image_processing(hash % 1000);
        }

        response.status_code = 200;
        response.body = {{"status", "success"}, {"hash", std::to_string(hash)}, {"processed_size", content.size()}};

        return response;
    }

    // Simulate real-time data streaming
    static HttpResponse process_websocket_message(HttpRequest const& request)
    {
        HttpResponse response;
        response.content_type = "application/json";

        // Simulate real-time data aggregation
        auto data = request.body;
        std::vector<double> values;

        if (data.contains("metrics") && data["metrics"].is_array())
        {
            for (auto const& metric : data["metrics"])
            {
                if (metric.contains("value"))
                {
                    values.push_back(metric["value"]);
                }
            }
        }

        // Simulate statistical computation
        double sum = 0.0;
        double sum_sq = 0.0;
        for (double val : values)
        {
            sum += val;
            sum_sq += val * val;
        }

        double mean = values.empty() ? 0.0 : sum / values.size();
        double variance = values.empty() ? 0.0 : (sum_sq / values.size()) - (mean * mean);

        response.status_code = 200;
        response.body = {{"type", "statistics"},
                         {"count", values.size()},
                         {"mean", mean},
                         {"variance", variance},
                         {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count()}};

        return response;
    }

  private:
    // Simulate database query with complex JSON construction
    static json simulate_user_database_query(json const& params)
    {
        json result = json::array();

        // Simulate expensive database operations
        size_t user_count = 100;
        if (params.contains("limit") && params["limit"].is_number())
        {
            user_count = std::min(user_count, static_cast<size_t>(params["limit"]));
        }

        for (size_t i = 0; i < user_count; ++i)
        {
            json user = {{"id", i + 1},
                         {"name", "User_" + std::to_string(i + 1)},
                         {"email", "user" + std::to_string(i + 1) + "@example.com"},
                         {"preferences",
                          {{"theme", (i % 2 == 0) ? "dark" : "light"},
                           {"notifications", i % 3 == 0},
                           {"language", (i % 4 == 0)   ? "en"
                                        : (i % 4 == 1) ? "de"
                                                       : "fr"}}},
                         {"created_at", "2024-01-" + std::string(i < 9 ? "0" : "") + std::to_string((i % 28) + 1)}};
            result.push_back(user);
        }

        return result;
    }

    // Simulate analytics computation
    static json simulate_analytics_computation(std::string const& user_id, json const& params)
    {
        // Simulate expensive computation with random data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<double> dist(100.0, 15.0);

        json analytics = {{"user_id", user_id}, {"period", params.value("period", "30d")}, {"metrics", json::array()}};

        for (int i = 0; i < 50; ++i)
        {
            json metric = {{"date", "2024-01-" + std::string(i < 9 ? "0" : "") + std::to_string((i % 28) + 1)},
                           {"page_views", static_cast<int>(dist(gen))},
                           {"session_duration", static_cast<int>(dist(gen) * 0.5)},
                           {"bounce_rate", dist(gen) / 1000.0}};
            analytics["metrics"].push_back(metric);
        }

        return analytics;
    }

    // Simulate recommendation engine
    static json simulate_recommendation_engine(std::unordered_map<std::string, std::string> const& preferences,
                                               [[maybe_unused]] json const& context)
    {
        json recommendations = {{"products", json::array()}, {"confidence_scores", json::array()}};

        // Simulate ML-like recommendation computation
        std::vector<std::pair<std::string, double>> candidates = {{"product_A", 0.85}, {"product_B", 0.72},
                                                                  {"product_C", 0.91}, {"product_D", 0.68},
                                                                  {"product_E", 0.79}, {"product_F", 0.88}};

        // Apply preference-based filtering
        for (auto const& [product, score] : candidates)
        {
            double adjusted_score = score;

            // Simulate preference matching (expensive computation)
            if (preferences.count("category") && preferences.at("category") == "electronics")
            {
                adjusted_score *= 1.2;
            }

            if (adjusted_score > 0.7)
            {
                recommendations["products"].push_back(
                    {{"id", product},
                     {"name", "Product " + product},
                     {"score", adjusted_score},
                     {"category", preferences.count("category") ? preferences.at("category") : "general"}});
                recommendations["confidence_scores"].push_back(adjusted_score);
            }
        }

        return recommendations;
    }

    // Simulate image processing
    static void simulate_image_processing(size_t size)
    {
        // Simulate CPU-intensive image operations
        volatile double sum = 0.0;
        for (size_t i = 0; i < size; ++i)
        {
            sum += std::sin(i * 0.01) * std::cos(i * 0.02);
        }
    }
};

// =============================================================================
// Web Server Benchmarks
// =============================================================================

static void BM_WebServer_JSON_API_Processing(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const requests_per_batch = state.range(1);
    size_t const concurrent_users = state.range(2);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("web_worker");
    pool.distribute_across_cpus();

    SessionStore sessions;
    RequestQueue request_queue;

    // Pre-populate sessions
    for (size_t i = 0; i < concurrent_users; ++i)
    {
        auto session = std::make_shared<UserSession>();
        session->user_id = "user_" + std::to_string(i);
        session->session_token = "token_" + std::to_string(i);
        session->preferences["category"] = (i % 2 == 0) ? "electronics" : "books";
        session->last_activity = std::chrono::steady_clock::now();
        sessions.store_session("session_" + std::to_string(i), session);
    }

    for (auto _ : state)
    {
        std::atomic<size_t> processed_requests{0};
        std::atomic<size_t> errors{0};

        // Submit API processing tasks
        for (size_t i = 0; i < requests_per_batch; ++i)
        {
            HttpRequest request;
            request.method = "POST";
            request.path = (i % 3 == 0) ? "/api/users" : (i % 3 == 1) ? "/api/analytics" : "/api/recommendations";
            request.session_id = "session_" + std::to_string(i % concurrent_users);
            request.body = {{"limit", 50}, {"offset", i * 10}, {"period", "30d"}};
            request.timestamp = std::chrono::steady_clock::now();

            pool.submit([&sessions, &processed_requests, &errors, request]() {
                try
                {
                    auto response = WebServerWorkloads::process_json_api_request(request, sessions);
                    if (response.status_code == 200)
                    {
                        processed_requests.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        errors.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                catch (std::exception const&)
                {
                    errors.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["processed_requests"] = benchmark::Counter(processed_requests.load());
        state.counters["errors"] = benchmark::Counter(errors.load());
        state.counters["error_rate_percent"] =
            benchmark::Counter(100.0 * errors.load() / std::max(requests_per_batch, size_t(1)));
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
        state.counters["avg_task_time_ms"] =
            benchmark::Counter(static_cast<double>(stats.avg_task_time.count()) / 1000.0);

        benchmark::DoNotOptimize(processed_requests.load());
    }

    state.SetItemsProcessed(state.iterations() * requests_per_batch);
    state.SetLabel("threads=" + std::to_string(num_threads) + " requests=" + std::to_string(requests_per_batch) +
                   " users=" + std::to_string(concurrent_users));
}

static void BM_WebServer_FileUpload_Processing(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const uploads_per_batch = state.range(1);
    size_t const file_size_kb = state.range(2);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("upload_worker");
    pool.distribute_across_cpus();

    for (auto _ : state)
    {
        std::atomic<size_t> processed_uploads{0};
        std::atomic<size_t> total_bytes{0};

        // Submit file upload processing tasks
        for (size_t i = 0; i < uploads_per_batch; ++i)
        {
            HttpRequest request;
            request.method = "POST";
            request.path = "/api/upload";

            // Generate simulated file content
            std::string content;
            content.reserve(file_size_kb * 1024);
            for (size_t j = 0; j < file_size_kb * 1024; ++j)
            {
                content += static_cast<char>('A' + (j % 26));
            }

            request.body = {
                {"filename", "file_" + std::to_string(i) + ".txt"}, {"content", content}, {"size", content.size()}};

            request.headers["content-type"] = (i % 3 == 0) ? "text/plain" : "image/jpeg";

            pool.submit([&processed_uploads, &total_bytes, request]() {
                auto response = WebServerWorkloads::process_file_upload(request);
                if (response.status_code == 200)
                {
                    processed_uploads.fetch_add(1, std::memory_order_relaxed);
                    total_bytes.fetch_add(request.body.value("size", size_t(0)), std::memory_order_relaxed);
                }
            });
        }

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["processed_uploads"] = benchmark::Counter(processed_uploads.load());
        state.counters["total_bytes_mb"] = benchmark::Counter(total_bytes.load() / (1024.0 * 1024.0));
        state.counters["throughput_mbps"] =
            benchmark::Counter((total_bytes.load() / (1024.0 * 1024.0)) / (stats.avg_task_time.count() / 1e9));
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));

        benchmark::DoNotOptimize(processed_uploads.load());
    }

    state.SetItemsProcessed(state.iterations() * uploads_per_batch);
    state.SetLabel("threads=" + std::to_string(num_threads) + " uploads=" + std::to_string(uploads_per_batch) +
                   " size=" + std::to_string(file_size_kb) + "KB");
}

static void BM_WebServer_RealTimeStreaming(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const messages_per_second = state.range(1);
    size_t const duration_seconds = 3;

    HighPerformancePool pool(num_threads);
    pool.configure_threads("streaming_worker");
    pool.distribute_across_cpus();

    for (auto _ : state)
    {
        std::atomic<size_t> processed_messages{0};
        std::atomic<double> avg_latency_ms{0.0};
        std::atomic<size_t> message_count{0};

        auto start_time = std::chrono::steady_clock::now();

        // Submit streaming message processing
        for (size_t i = 0; i < messages_per_second * duration_seconds; ++i)
        {
            auto submit_time = std::chrono::steady_clock::now();

            HttpRequest request;
            request.method = "POST";
            request.path = "/api/stream";

            // Generate realistic streaming data
            json metrics = json::array();
            for (int j = 0; j < 10; ++j)
            {
                metrics.push_back(
                    {{"sensor_id", j},
                     {"value", 100.0 + std::sin(i * 0.1 + j) * 10.0},
                     {"timestamp",
                      std::chrono::duration_cast<std::chrono::milliseconds>(submit_time.time_since_epoch()).count()}});
            }

            request.body = {{"stream_id", "stream_001"}, {"metrics", metrics}, {"batch_size", 10}};

            pool.submit([&processed_messages, &avg_latency_ms, &message_count, &request, submit_time]() {
                auto process_start = std::chrono::steady_clock::now();

                auto response = WebServerWorkloads::process_websocket_message(request);

                auto process_end = std::chrono::steady_clock::now();
                auto latency =
                    std::chrono::duration_cast<std::chrono::microseconds>(process_end - submit_time).count() / 1000.0;

                processed_messages.fetch_add(1, std::memory_order_relaxed);
                double current_avg = avg_latency_ms.load(std::memory_order_relaxed);
                size_t count = message_count.fetch_add(1, std::memory_order_relaxed) + 1;

                // Update running average
                double new_avg = current_avg + (latency - current_avg) / count;
                avg_latency_ms.store(new_avg, std::memory_order_relaxed);
            });
        }

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["processed_messages"] = benchmark::Counter(processed_messages.load());
        state.counters["target_mps"] = benchmark::Counter(messages_per_second);
        state.counters["actual_mps"] =
            benchmark::Counter(static_cast<double>(processed_messages.load()) / duration_seconds);
        state.counters["avg_latency_ms"] = benchmark::Counter(avg_latency_ms.load());
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
        state.counters["efficiency_percent"] =
            benchmark::Counter(100.0 * processed_messages.load() / (messages_per_second * duration_seconds));

        benchmark::DoNotOptimize(processed_messages.load());
    }

    state.SetItemsProcessed(state.iterations() * messages_per_second * duration_seconds);
    state.SetLabel("threads=" + std::to_string(num_threads) + " target_mps=" + std::to_string(messages_per_second));
}

// =============================================================================
// Registration
// =============================================================================

BENCHMARK(BM_WebServer_JSON_API_Processing)
    ->Args({2, 100, 10}) // 2 threads, 100 requests, 10 users
    ->Args({4, 100, 20}) // 4 threads, 100 requests, 20 users
    ->Args({8, 100, 50}) // 8 threads, 100 requests, 50 users
    ->Args({4, 500, 25}) // 4 threads, 500 requests, 25 users
    ->Args({8, 500, 50}) // 8 threads, 500 requests, 50 users
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_WebServer_FileUpload_Processing)
    ->Args({2, 50, 100})  // 2 threads, 50 uploads, 100KB each
    ->Args({4, 50, 100})  // 4 threads, 50 uploads, 100KB each
    ->Args({8, 50, 100})  // 8 threads, 50 uploads, 100KB each
    ->Args({4, 100, 500}) // 4 threads, 100 uploads, 500KB each
    ->Args({8, 100, 500}) // 8 threads, 100 uploads, 500KB each
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_WebServer_RealTimeStreaming)
    ->Args({2, 100})  // 2 threads, 100 messages/sec
    ->Args({4, 100})  // 4 threads, 100 messages/sec
    ->Args({8, 100})  // 8 threads, 100 messages/sec
    ->Args({4, 500})  // 4 threads, 500 messages/sec
    ->Args({8, 500})  // 8 threads, 500 messages/sec
    ->Args({4, 1000}) // 4 threads, 1000 messages/sec
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
