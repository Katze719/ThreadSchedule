#include <atomic>
#include <benchmark/benchmark.h>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

// =============================================================================
// Image Resampling Workload Simulation
// =============================================================================

struct SimulatedImage
{
    std::vector<uint32_t> pixels;
    size_t                width;
    size_t                height;

    SimulatedImage(
        size_t w,
        size_t h
    )
        : width(w),
          height(h)
    {
        pixels.resize(w * h);
        // Fill with some pattern to simulate real image data
        for (size_t i = 0; i < pixels.size(); ++i)
        {
            pixels[i] = static_cast<uint32_t>(i * 0x12345678);
        }
    }
};

struct ResampledImage
{
    size_t                width;
    size_t                height;
    std::vector<uint32_t> pixels;

    ResampledImage(
        size_t w,
        size_t h
    )
        : width(w),
          height(h),
          pixels(w * h)
    {
    }
};

// Thread-safe image queue
template <typename T> class ImageQueue
{
  private:
    std::queue<T>           queue_;
    mutable std::mutex      mutex_;
    std::condition_variable condition_;
    bool                    stopped_ = false;

  public:
    void push(T item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stopped_)
        {
            queue_.push(std::move(item));
            condition_.notify_one();
        }
    }

    bool pop(
        T                        &item,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(100)
    )
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (condition_.wait_for(lock, timeout, [this] { return !queue_.empty() || stopped_; }))
        {
            if (!queue_.empty())
            {
                item = std::move(queue_.front());
                queue_.pop();
                return true;
            }
        }
        return false;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        condition_.notify_all();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
};

// Realistic resampling simulation (heavy computing)
class ImageResampler
{
  public:
    static ResampledImage resample_bilinear(
        const SimulatedImage &input,
        size_t                new_width,
        size_t                new_height
    )
    {
        ResampledImage output(new_width, new_height);

        const double x_ratio = static_cast<double>(input.width - 1) / new_width;
        const double y_ratio = static_cast<double>(input.height - 1) / new_height;

        for (size_t y = 0; y < new_height; ++y)
        {
            for (size_t x = 0; x < new_width; ++x)
            {
                const double px = x * x_ratio;
                const double py = y * y_ratio;

                const size_t x_floor = static_cast<size_t>(px);
                const size_t y_floor = static_cast<size_t>(py);
                const size_t x_ceil  = std::min(x_floor + 1, input.width - 1);
                const size_t y_ceil  = std::min(y_floor + 1, input.height - 1);

                const double x_weight = px - x_floor;
                const double y_weight = py - y_floor;

                // Bilinear interpolation with simulated heavy computation
                const uint32_t top_left     = input.pixels[y_floor * input.width + x_floor];
                const uint32_t top_right    = input.pixels[y_floor * input.width + x_ceil];
                const uint32_t bottom_left  = input.pixels[y_ceil * input.width + x_floor];
                const uint32_t bottom_right = input.pixels[y_ceil * input.width + x_ceil];

                // Simulate heavy computation by doing multiple interpolations
                uint32_t result = 0;
                for (int iter = 0; iter < 50; ++iter)
                {
                    const double top =
                        static_cast<double>(top_left) * (1.0 - x_weight) + static_cast<double>(top_right) * x_weight;
                    const double bottom = static_cast<double>(bottom_left) * (1.0 - x_weight) +
                                          static_cast<double>(bottom_right) * x_weight;
                    result += static_cast<uint32_t>(top * (1.0 - y_weight) + bottom * y_weight);
                }

                output.pixels[y * new_width + x] = result;
            }
        }

        return output;
    }
};

// =============================================================================
// Producer-Consumer Resampling Benchmarks (Your Real Workload)
// =============================================================================

static void BM_Resampling_HighPerformancePool_4Core(benchmark::State &state)
{
    const size_t image_width  = state.range(0);
    const size_t image_height = state.range(1);
    const size_t num_images   = state.range(2);

    // 4 cores: 1 producer + 3 workers (your scenario)
    const size_t        num_workers = 3;
    HighPerformancePool pool(num_workers);
    pool.configure_threads("resampling_worker", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    for (auto _ : state)
    {
        ImageQueue<std::shared_ptr<SimulatedImage>> input_queue;
        ImageQueue<std::shared_ptr<ResampledImage>> output_queue;
        std::atomic<size_t>                         processed_images{0};
        std::atomic<bool>                           producer_done{false};

        // Producer thread (simulates your image feed)
        std::thread producer([&]() {
            for (size_t i = 0; i < num_images; ++i)
            {
                auto image = std::make_shared<SimulatedImage>(image_width, image_height);
                input_queue.push(image);

                // Simulate realistic producer timing (camera frame rate etc.)
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            producer_done.store(true);
        });

        std::vector<std::future<void>> futures;

        // Submit resampling tasks to thread pool
        while (!producer_done.load() || !input_queue.empty())
        {
            std::shared_ptr<SimulatedImage> input_image;

            if (input_queue.pop(input_image, std::chrono::milliseconds(10)))
            {
                futures.push_back(pool.submit([&output_queue, &processed_images, input_image]() {
                    // Heavy resampling computation (your actual workload)
                    auto resampled = std::make_shared<ResampledImage>(
                        ImageResampler::resample_bilinear(*input_image, input_image->width / 2, input_image->height / 2)
                    );

                    output_queue.push(resampled);
                    processed_images.fetch_add(1, std::memory_order_relaxed);
                }));
            }
        }

        // Wait for all resampling tasks to complete
        for (auto &future : futures)
        {
            future.wait();
        }

        producer.join();

        auto stats                         = pool.get_statistics();
        state.counters["work_steal_ratio"] = 100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1));
        state.counters["avg_task_time_ms"] = static_cast<double>(stats.avg_task_time.count()) / 1000.0;

        benchmark::DoNotOptimize(processed_images.load());
    }

    state.SetItemsProcessed(state.iterations() * num_images);
    state.SetLabel(
        "size=" + std::to_string(image_width) + "x" + std::to_string(image_height) +
        " images=" + std::to_string(num_images)
    );
}

static void BM_Resampling_FastThreadPool_4Core(benchmark::State &state)
{
    const size_t image_width  = state.range(0);
    const size_t image_height = state.range(1);
    const size_t num_images   = state.range(2);

    const size_t   num_workers = 3;
    FastThreadPool pool(num_workers);
    pool.configure_threads("fast_resampling_worker");

    for (auto _ : state)
    {
        ImageQueue<std::shared_ptr<SimulatedImage>> input_queue;
        ImageQueue<std::shared_ptr<ResampledImage>> output_queue;
        std::atomic<size_t>                         processed_images{0};
        std::atomic<bool>                           producer_done{false};

        std::thread producer([&]() {
            for (size_t i = 0; i < num_images; ++i)
            {
                auto image = std::make_shared<SimulatedImage>(image_width, image_height);
                input_queue.push(image);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            producer_done.store(true);
        });

        std::vector<std::future<void>> futures;

        while (!producer_done.load() || !input_queue.empty())
        {
            std::shared_ptr<SimulatedImage> input_image;

            if (input_queue.pop(input_image, std::chrono::milliseconds(10)))
            {
                futures.push_back(pool.submit([&output_queue, &processed_images, input_image]() {
                    auto resampled = std::make_shared<ResampledImage>(
                        ImageResampler::resample_bilinear(*input_image, input_image->width / 2, input_image->height / 2)
                    );

                    output_queue.push(resampled);
                    processed_images.fetch_add(1, std::memory_order_relaxed);
                }));
            }
        }

        for (auto &future : futures)
        {
            future.wait();
        }

        producer.join();

        auto stats                         = pool.get_statistics();
        state.counters["avg_task_time_ms"] = static_cast<double>(stats.avg_task_time.count()) / 1000.0;

        benchmark::DoNotOptimize(processed_images.load());
    }

    state.SetItemsProcessed(state.iterations() * num_images);
    state.SetLabel(
        "size=" + std::to_string(image_width) + "x" + std::to_string(image_height) +
        " images=" + std::to_string(num_images)
    );
}

// =============================================================================
// Real-time Video Processing Simulation (30fps typical)
// =============================================================================

static void BM_Resampling_RealTimeVideo(benchmark::State &state)
{
    const size_t fps              = state.range(0); // Target frames per second
    const size_t duration_seconds = 3; // Shorter for benchmark
    const size_t num_workers      = 3;

    HighPerformancePool pool(num_workers);
    pool.configure_threads("video_worker", SchedulingPolicy::OTHER, ThreadPriority::normal());
    pool.distribute_across_cpus();

    // Standard video resolution
    const size_t image_width    = 1280;
    const size_t image_height   = 720;
    const auto   frame_interval = std::chrono::microseconds(1000000 / fps);

    for (auto _ : state)
    {
        ImageQueue<std::shared_ptr<SimulatedImage>> input_queue;
        ImageQueue<std::shared_ptr<ResampledImage>> output_queue;
        std::atomic<size_t>                         frames_processed{0};
        std::atomic<size_t>                         frames_dropped{0};
        std::atomic<bool>                           should_stop{false};

        // Producer thread with precise timing (simulates video capture)
        std::thread producer([&]() {
            const auto start_time  = std::chrono::steady_clock::now();
            size_t     frame_count = 0;

            while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(duration_seconds))
            {
                auto image = std::make_shared<SimulatedImage>(image_width, image_height);

                // Check if we can keep up with real-time processing
                if (input_queue.size() > 5)
                { // Buffer overflow simulation
                    frames_dropped.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    input_queue.push(image);
                }

                frame_count++;

                // Wait for next frame
                auto next_frame_time = start_time + frame_count * frame_interval;
                std::this_thread::sleep_until(next_frame_time);
            }
            should_stop.store(true);
        });

        std::vector<std::future<void>> futures;

        // Processing loop
        while (!should_stop.load() || !input_queue.empty())
        {
            std::shared_ptr<SimulatedImage> input_image;

            if (input_queue.pop(input_image, std::chrono::milliseconds(5)))
            {
                futures.push_back(pool.submit([&output_queue, &frames_processed, input_image]() {
                    // Heavy resampling computation (downscale to half resolution)
                    auto resampled =
                        std::make_shared<ResampledImage>(ImageResampler::resample_bilinear(*input_image, 640, 360));

                    output_queue.push(resampled);
                    frames_processed.fetch_add(1, std::memory_order_relaxed);
                }));
            }
        }

        for (auto &future : futures)
        {
            future.wait();
        }

        producer.join();

        auto stats                       = pool.get_statistics();
        state.counters["fps_target"]     = static_cast<double>(fps);
        state.counters["fps_achieved"]   = static_cast<double>(frames_processed.load()) / duration_seconds;
        state.counters["frames_dropped"] = static_cast<double>(frames_dropped.load());
        state.counters["drop_rate_percent"] =
            100.0 * frames_dropped.load() / std::max(frames_processed.load() + frames_dropped.load(), size_t(1));
        state.counters["work_steal_ratio"] = 100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1));

        benchmark::DoNotOptimize(frames_processed.load());
    }

    state.SetItemsProcessed(state.iterations() * fps * duration_seconds);
    state.SetLabel("target_fps=" + std::to_string(fps) + " resolution=1280x720");
}

// =============================================================================
// Pool Comparison for Image Workload
// =============================================================================

static void BM_Resampling_PoolComparison(benchmark::State &state)
{
    const size_t num_images = state.range(0);
    const int    pool_type  = state.range(1); // 0=ThreadPool, 1=FastThreadPool, 2=HighPerformancePool

    const size_t num_workers  = 3;
    const size_t image_width  = 1024;
    const size_t image_height = 768;

    for (auto _ : state)
    {
        ImageQueue<std::shared_ptr<SimulatedImage>> input_queue;
        ImageQueue<std::shared_ptr<ResampledImage>> output_queue;
        std::atomic<size_t>                         processed_images{0};
        std::atomic<bool>                           producer_done{false};
        std::atomic<size_t>                         total_pixels_processed{0};

        // Create the appropriate pool type
        std::unique_ptr<ThreadPool>          simple_pool;
        std::unique_ptr<FastThreadPool>      fast_pool;
        std::unique_ptr<HighPerformancePool> hp_pool;

        if (pool_type == 0)
        {
            simple_pool = std::make_unique<ThreadPool>(num_workers);
            simple_pool->configure_threads("resampling");
        }
        else if (pool_type == 1)
        {
            fast_pool = std::make_unique<FastThreadPool>(num_workers);
            fast_pool->configure_threads("resampling");
        }
        else
        {
            hp_pool = std::make_unique<HighPerformancePool>(num_workers);
            hp_pool->configure_threads("resampling", SchedulingPolicy::OTHER, ThreadPriority::normal());
            hp_pool->distribute_across_cpus();
        }

        // Producer thread
        std::thread producer([&]() {
            for (size_t i = 0; i < num_images; ++i)
            {
                auto image = std::make_shared<SimulatedImage>(image_width, image_height);
                input_queue.push(image);
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            producer_done.store(true);
        });

        std::vector<std::future<void>> futures;

        // Worker task submission loop
        while (!producer_done.load() || !input_queue.empty())
        {
            std::shared_ptr<SimulatedImage> input_image;

            if (input_queue.pop(input_image, std::chrono::milliseconds(10)))
            {
                const size_t pixels = input_image->width * input_image->height;

                if (pool_type == 0)
                {
                    futures.push_back(simple_pool->submit(
                        [&output_queue, &processed_images, &total_pixels_processed, pixels, input_image]() {
                            auto resampled = std::make_shared<ResampledImage>(ImageResampler::resample_bilinear(
                                *input_image, input_image->width / 2, input_image->height / 2
                            ));
                            output_queue.push(resampled);
                            processed_images.fetch_add(1, std::memory_order_relaxed);
                            total_pixels_processed.fetch_add(pixels, std::memory_order_relaxed);
                        }
                    ));
                }
                else if (pool_type == 1)
                {
                    futures.push_back(fast_pool->submit(
                        [&output_queue, &processed_images, &total_pixels_processed, pixels, input_image]() {
                            auto resampled = std::make_shared<ResampledImage>(ImageResampler::resample_bilinear(
                                *input_image, input_image->width / 2, input_image->height / 2
                            ));
                            output_queue.push(resampled);
                            processed_images.fetch_add(1, std::memory_order_relaxed);
                            total_pixels_processed.fetch_add(pixels, std::memory_order_relaxed);
                        }
                    ));
                }
                else
                {
                    futures.push_back(hp_pool->submit(
                        [&output_queue, &processed_images, &total_pixels_processed, pixels, input_image]() {
                            auto resampled = std::make_shared<ResampledImage>(ImageResampler::resample_bilinear(
                                *input_image, input_image->width / 2, input_image->height / 2
                            ));
                            output_queue.push(resampled);
                            processed_images.fetch_add(1, std::memory_order_relaxed);
                            total_pixels_processed.fetch_add(pixels, std::memory_order_relaxed);
                        }
                    ));
                }
            }
        }

        // Wait for all resampling to complete
        for (auto &future : futures)
        {
            future.wait();
        }

        producer.join();

        // Add pool-specific counters
        if (pool_type == 2 && hp_pool)
        {
            auto stats = hp_pool->get_statistics();
            state.counters["work_steal_ratio"] =
                100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1));
            state.counters["avg_task_time_ms"] = static_cast<double>(stats.avg_task_time.count()) / 1000.0;
        }

        state.counters["total_pixels"]         = static_cast<double>(total_pixels_processed.load());
        state.counters["avg_pixels_per_image"] = static_cast<double>(total_pixels_processed.load()) / num_images;

        benchmark::DoNotOptimize(processed_images.load());
        benchmark::DoNotOptimize(output_queue.size());
    }

    std::vector<std::string> pool_names = {"ThreadPool", "FastThreadPool", "HighPerformancePool"};
    state.SetItemsProcessed(state.iterations() * num_images);
    state.SetLabel(pool_names[pool_type] + " images=" + std::to_string(num_images));
}

// =============================================================================
// Queue Depth Impact Analysis (Buffer Management)
// =============================================================================

static void BM_Resampling_QueueDepthImpact(benchmark::State &state)
{
    const size_t max_queue_depth = state.range(0);
    const size_t num_images      = 50;
    const size_t num_workers     = 3;

    HighPerformancePool pool(num_workers);
    pool.configure_threads("queue_depth_worker");

    for (auto _ : state)
    {
        ImageQueue<std::shared_ptr<SimulatedImage>> input_queue;
        ImageQueue<std::shared_ptr<ResampledImage>> output_queue;
        std::atomic<size_t>                         processed_images{0};
        std::atomic<size_t>                         queue_overflows{0};
        std::atomic<bool>                           producer_done{false};

        // Producer with queue depth limiting
        std::thread producer([&]() {
            for (size_t i = 0; i < num_images; ++i)
            {
                // Wait if queue is too deep (backpressure simulation)
                while (input_queue.size() >= max_queue_depth && !producer_done.load())
                {
                    queue_overflows.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }

                auto image = std::make_shared<SimulatedImage>(512, 512);
                input_queue.push(image);
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
            producer_done.store(true);
        });

        std::vector<std::future<void>> futures;

        while (!producer_done.load() || !input_queue.empty())
        {
            std::shared_ptr<SimulatedImage> input_image;

            if (input_queue.pop(input_image, std::chrono::milliseconds(10)))
            {
                futures.push_back(pool.submit([&output_queue, &processed_images, input_image]() {
                    auto resampled =
                        std::make_shared<ResampledImage>(ImageResampler::resample_bilinear(*input_image, 256, 256));

                    output_queue.push(resampled);
                    processed_images.fetch_add(1, std::memory_order_relaxed);
                }));
            }
        }

        for (auto &future : futures)
        {
            future.wait();
        }

        producer.join();

        state.counters["queue_overflows"]       = static_cast<double>(queue_overflows.load());
        state.counters["overflow_rate_percent"] = 100.0 * queue_overflows.load() / num_images;

        benchmark::DoNotOptimize(processed_images.load());
    }

    state.SetItemsProcessed(state.iterations() * num_images);
    state.SetLabel("max_queue_depth=" + std::to_string(max_queue_depth));
}

// =============================================================================
// Registration (Optimized for 4-Core Systems)
// =============================================================================

// Your specific 4-core image resampling workload
BENCHMARK(BM_Resampling_HighPerformancePool_4Core)
    ->Args(
        {256,
         256,
         30}
    ) // Small images, many frames
    ->Args(
        {512,
         512,
         20}
    ) // Medium images
    ->Args(
        {1024,
         768,
         15}
    ) // Large images
    ->Args(
        {1920,
         1080,
         10}
    ) // HD images (fewer due to heavy computation)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Resampling_FastThreadPool_4Core)
    ->Args(
        {256,
         256,
         30}
    )
    ->Args(
        {512,
         512,
         20}
    )
    ->Args(
        {1024,
         768,
         15}
    )
    ->Unit(benchmark::kMillisecond);

// Pool comparison for your workload
BENCHMARK(BM_Resampling_PoolComparison)
    ->Args(
        {15,
         0}
    )
    ->Args(
        {15,
         1}
    )
    ->Args(
        {15,
         2}
    ) // 15 images, different pools
    ->Args(
        {30,
         0}
    )
    ->Args(
        {30,
         1}
    )
    ->Args(
        {30,
         2}
    ) // 30 images, different pools
    ->Unit(benchmark::kMillisecond);

// Real-time video processing simulation
BENCHMARK(BM_Resampling_RealTimeVideo)
    ->Args({15}) // 15 FPS
    ->Args({24}) // 24 FPS (cinema)
    ->Args({30}) // 30 FPS (standard video)
    ->Unit(benchmark::kMillisecond);

// Queue depth impact (buffer management)
BENCHMARK(BM_Resampling_QueueDepthImpact)
    ->Args({1})
    ->Args({2})
    ->Args({5})
    ->Args({10})
    ->Args({20})
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
