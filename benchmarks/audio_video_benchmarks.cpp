#include <atomic>
#include <benchmark/benchmark.h>
#include <chrono>
#include <cmath>
#include <memory>
#include <random>
#include <thread>
#include <threadschedule/threadschedule.hpp>
#include <vector>

using namespace threadschedule;

// =============================================================================
// Realistic Audio/Video Processing Workloads
// =============================================================================

// Simulated audio frame
struct AudioFrame
{
    std::vector<float> samples_left;
    std::vector<float> samples_right;
    size_t sample_rate;
    size_t channels;
    double duration_ms;
};

// Simulated video frame
struct VideoFrame
{
    std::vector<uint8_t> y_plane;
    std::vector<uint8_t> u_plane;
    std::vector<uint8_t> v_plane;
    size_t width;
    size_t height;
    size_t stride_y;
    size_t stride_uv;
    std::string format; // "YUV420P", "RGB24", etc.
};

// Thread-safe frame queues
template <typename T>
class FrameQueue
{
  private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool stopped_ = false;

  public:
    void push(T frame)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stopped_)
        {
            queue_.push(std::move(frame));
            condition_.notify_one();
        }
    }

    bool pop(T& frame, std::chrono::milliseconds timeout = std::chrono::milliseconds(100))
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (condition_.wait_for(lock, timeout, [this] { return !queue_.empty() || stopped_; }))
        {
            if (!queue_.empty())
            {
                frame = std::move(queue_.front());
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

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
};

// Audio processing workloads
class AudioWorkloads
{
  public:
    // Simulate audio encoding (MP3, AAC, etc.)
    static std::vector<uint8_t> encode_audio(AudioFrame const& frame, std::string const& codec, int bitrate_kbps)
    {
        std::vector<uint8_t> encoded_data;

        // Simulate psychoacoustic analysis
        double psychoacoustic_ratio = analyze_psychoacoustic_model(frame);

        // Simulate bit allocation based on psychoacoustic model
        size_t total_bits = bitrate_kbps * 1000 * (frame.duration_ms / 1000.0);
        size_t frame_bits = static_cast<size_t>(total_bits * psychoacoustic_ratio);

        // Simulate entropy coding
        encoded_data.resize(frame_bits / 8);

        // Fill with pseudo-random data representing compressed audio
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dist(0, 255);

        for (auto& byte : encoded_data)
        {
            byte = dist(gen);
        }

        return encoded_data;
    }

    // Simulate audio filtering (equalizer, noise reduction, etc.)
    static AudioFrame apply_audio_filter(AudioFrame const& input, std::string const& filter_type)
    {
        AudioFrame output = input;

        if (filter_type == "lowpass")
        {
            // Simulate low-pass filter (remove high frequencies)
            apply_lowpass_filter(output.samples_left, output.samples_right);
        }
        else if (filter_type == "highpass")
        {
            // Simulate high-pass filter (remove low frequencies)
            apply_highpass_filter(output.samples_left, output.samples_right);
        }
        else if (filter_type == "equalizer")
        {
            // Simulate 10-band equalizer
            apply_equalizer(output.samples_left, output.samples_right);
        }
        else if (filter_type == "noise_reduction")
        {
            // Simulate noise reduction (spectral subtraction)
            apply_noise_reduction(output.samples_left, output.samples_right);
        }

        return output;
    }

    // Simulate audio mixing
    static AudioFrame mix_audio_frames(std::vector<AudioFrame> const& frames, double master_volume)
    {
        if (frames.empty())
            return AudioFrame{};

        AudioFrame result = frames[0];

        // Mix multiple audio streams
        for (size_t i = 1; i < frames.size(); ++i)
        {
            mix_two_frames(result, frames[i], master_volume);
        }

        return result;
    }

  private:
    static double analyze_psychoacoustic_model(AudioFrame const& frame)
    {
        // Simulate psychoacoustic analysis (masking thresholds, etc.)
        double total_energy = 0.0;
        size_t sample_count = frame.samples_left.size();

        for (size_t i = 0; i < sample_count; ++i)
        {
            double left = frame.samples_left[i];
            double right = frame.samples_right[i];
            total_energy += left * left + right * right;
        }

        // Normalize energy and apply psychoacoustic model
        double avg_energy = total_energy / sample_count;
        return std::min(1.0, avg_energy * 100.0); // Simplified model
    }

    static void apply_lowpass_filter(std::vector<float>& left, std::vector<float>& right)
    {
        // Simple IIR low-pass filter simulation
        float alpha = 0.1f; // Filter coefficient
        for (size_t i = 1; i < left.size(); ++i)
        {
            left[i] = alpha * left[i] + (1.0f - alpha) * left[i - 1];
            right[i] = alpha * right[i] + (1.0f - alpha) * right[i - 1];
        }
    }

    static void apply_highpass_filter(std::vector<float>& left, std::vector<float>& right)
    {
        // Simple IIR high-pass filter simulation
        float alpha = 0.9f; // Filter coefficient
        for (size_t i = 1; i < left.size(); ++i)
        {
            left[i] = alpha * (left[i] - left[i - 1]);
            right[i] = alpha * (right[i] - right[i - 1]);
        }
    }

    static void apply_equalizer(std::vector<float>& left, std::vector<float>& right)
    {
        // Simulate 10-band equalizer with different gains
        std::vector<float> band_gains = {1.2f, 1.1f, 1.0f, 0.9f, 0.8f, 1.0f, 1.1f, 1.2f, 1.0f, 0.9f};

        // Apply band gains (simplified - in reality this would use FFT)
        for (size_t i = 0; i < left.size(); ++i)
        {
            size_t band = (i * 10) / left.size(); // Simple band mapping
            float gain = band_gains[std::min(band, band_gains.size() - 1)];

            left[i] *= gain;
            right[i] *= gain;
        }
    }

    static void apply_noise_reduction(std::vector<float>& left, std::vector<float>& right)
    {
        // Simulate spectral subtraction noise reduction
        float noise_threshold = 0.01f;

        for (size_t i = 0; i < left.size(); ++i)
        {
            float noise_level = std::abs(left[i]) + std::abs(right[i]);
            if (noise_level < noise_threshold)
            {
                left[i] *= 0.1f; // Attenuate noise
                right[i] *= 0.1f;
            }
        }
    }

    static void mix_two_frames(AudioFrame& target, AudioFrame const& source, double master_volume)
    {
        // Mix two audio frames with volume control
        size_t min_samples = std::min(target.samples_left.size(), source.samples_left.size());

        for (size_t i = 0; i < min_samples; ++i)
        {
            target.samples_left[i] = (target.samples_left[i] + source.samples_left[i] * master_volume) * 0.5f;
            target.samples_right[i] = (target.samples_right[i] + source.samples_right[i] * master_volume) * 0.5f;
        }
    }
};

// Video processing workloads
class VideoWorkloads
{
  public:
    // Simulate video encoding (H.264, H.265, VP9, etc.)
    static std::vector<uint8_t> encode_video_frame(VideoFrame const& frame, std::string const& codec, int bitrate_kbps)
    {
        std::vector<uint8_t> encoded_data;

        // Simulate motion estimation and compensation
        double motion_complexity = analyze_motion_complexity(frame);

        // Simulate intra/inter prediction
        size_t macroblock_count = (frame.width / 16) * (frame.height / 16);
        size_t intra_blocks = static_cast<size_t>(macroblock_count * (1.0 - motion_complexity));

        // Simulate entropy coding
        size_t total_bits = bitrate_kbps * 1000 * 0.033; // ~33ms per frame at 30fps
        size_t compressed_bits = static_cast<size_t>(total_bits * (0.3 + motion_complexity * 0.7));

        encoded_data.resize(compressed_bits / 8);

        // Fill with pseudo-random data representing compressed video
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dist(0, 255);

        for (auto& byte : encoded_data)
        {
            byte = dist(gen);
        }

        return encoded_data;
    }

    // Simulate video filtering (resize, color correction, stabilization, etc.)
    static VideoFrame apply_video_filter(VideoFrame const& input, std::string const& filter_type)
    {
        VideoFrame output = input;

        if (filter_type == "resize")
        {
            // Simulate video resizing (downscale to half resolution)
            resize_video_frame(output, output.width / 2, output.height / 2);
        }
        else if (filter_type == "color_correction")
        {
            // Simulate color correction (brightness, contrast, saturation)
            apply_color_correction(output);
        }
        else if (filter_type == "denoise")
        {
            // Simulate video denoising
            apply_denoise_filter(output);
        }
        else if (filter_type == "sharpen")
        {
            // Simulate sharpening filter
            apply_sharpen_filter(output);
        }

        return output;
    }

    // Simulate video stabilization
    static VideoFrame stabilize_video_frame(VideoFrame const& input, std::vector<float> const& motion_vectors)
    {
        VideoFrame output = input;

        // Simulate motion compensation
        float avg_motion_x = 0.0f, avg_motion_y = 0.0f;
        for (float motion : motion_vectors)
        {
            avg_motion_x += motion;
            avg_motion_y += motion * 0.5f; // Simplified model
        }

        if (!motion_vectors.empty())
        {
            avg_motion_x /= motion_vectors.size();
            avg_motion_y /= motion_vectors.size();
        }

        // Apply stabilization transform (simplified)
        apply_motion_compensation(output, -avg_motion_x, -avg_motion_y);

        return output;
    }

  private:
    static double analyze_motion_complexity(VideoFrame const& frame)
    {
        // Simulate motion vector analysis
        size_t total_motion = 0;
        size_t block_count = (frame.width / 16) * (frame.height / 16);

        // Simulate SAD (Sum of Absolute Differences) calculation
        for (size_t i = 0; i < block_count; ++i)
        {
            size_t block_x = (i * 16) % frame.width;
            size_t block_y = (i * 16) / frame.width;

            if (block_x + 16 <= frame.width && block_y + 16 <= frame.height)
            {
                // Calculate block variance as motion indicator
                volatile int variance = 0;
                for (size_t y = 0; y < 16; ++y)
                {
                    for (size_t x = 0; x < 16; ++x)
                    {
                        size_t pixel_idx = (block_y + y) * frame.stride_y + (block_x + x);
                        if (pixel_idx < frame.y_plane.size())
                        {
                            variance += frame.y_plane[pixel_idx];
                        }
                    }
                }
                total_motion += std::abs(variance);
            }
        }

        return std::min(1.0, static_cast<double>(total_motion) / (block_count * 1000.0));
    }

    static void resize_video_frame(VideoFrame& frame, size_t new_width, size_t new_height)
    {
        // Simulate bilinear resize (simplified)
        std::vector<uint8_t> new_y_plane(new_width * new_height);
        std::vector<uint8_t> new_u_plane((new_width / 2) * (new_height / 2));
        std::vector<uint8_t> new_v_plane((new_width / 2) * (new_height / 2));

        // Simple nearest-neighbor resize for benchmark
        float x_ratio = static_cast<float>(frame.width) / new_width;
        float y_ratio = static_cast<float>(frame.height) / new_height;

        for (size_t y = 0; y < new_height; ++y)
        {
            for (size_t x = 0; x < new_width; ++x)
            {
                size_t src_x = static_cast<size_t>(x * x_ratio);
                size_t src_y = static_cast<size_t>(y * y_ratio);
                size_t src_idx = src_y * frame.stride_y + src_x;

                if (src_idx < frame.y_plane.size())
                {
                    new_y_plane[y * new_width + x] = frame.y_plane[src_idx];
                }
            }
        }

        frame.y_plane = std::move(new_y_plane);
        frame.u_plane = std::move(new_u_plane);
        frame.v_plane = std::move(new_v_plane);
        frame.width = new_width;
        frame.height = new_height;
        frame.stride_y = new_width;
        frame.stride_uv = new_width / 2;
    }

    static void apply_color_correction(VideoFrame& frame)
    {
        // Simulate color correction (brightness, contrast, saturation)
        float brightness = 0.1f; // Increase brightness
        float contrast = 1.2f;   // Increase contrast
        float saturation = 1.1f; // Increase saturation

        for (size_t i = 0; i < frame.y_plane.size(); ++i)
        {
            float y = frame.y_plane[i] / 255.0f;
            float u = frame.u_plane[i / 4] / 255.0f; // UV planes are 1/4 size
            float v = frame.v_plane[i / 4] / 255.0f;

            // Apply brightness and contrast to luminance
            y = std::min(1.0f, std::max(0.0f, (y - 0.5f) * contrast + 0.5f + brightness));

            // Apply saturation to chrominance
            float u_center = u - 0.5f;
            float v_center = v - 0.5f;
            u_center *= saturation;
            v_center *= saturation;
            u = u_center + 0.5f;
            v = v_center + 0.5f;

            frame.y_plane[i] = static_cast<uint8_t>(y * 255.0f);
            if (i % 4 == 0)
            {
                frame.u_plane[i / 4] = static_cast<uint8_t>(u * 255.0f);
                frame.v_plane[i / 4] = static_cast<uint8_t>(v * 255.0f);
            }
        }
    }

    static void apply_denoise_filter(VideoFrame& frame)
    {
        // Simulate simple gaussian blur for denoising
        std::vector<uint8_t> temp_y = frame.y_plane;

        for (size_t y = 1; y < frame.height - 1; ++y)
        {
            for (size_t x = 1; x < frame.width - 1; ++x)
            {
                size_t idx = y * frame.stride_y + x;
                uint8_t sum = 0;
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        sum += temp_y[(y + dy) * frame.stride_y + (x + dx)];
                    }
                }
                frame.y_plane[idx] = sum / 9;
            }
        }
    }

    static void apply_sharpen_filter(VideoFrame& frame)
    {
        // Simulate unsharp masking for sharpening
        std::vector<uint8_t> blurred = frame.y_plane;
        std::vector<uint8_t> temp = frame.y_plane;

        // Apply gaussian blur first
        for (size_t y = 1; y < frame.height - 1; ++y)
        {
            for (size_t x = 1; x < frame.width - 1; ++x)
            {
                size_t idx = y * frame.stride_y + x;
                uint8_t sum = 0;
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        sum += temp[(y + dy) * frame.stride_y + (x + dx)];
                    }
                }
                blurred[idx] = sum / 9;
            }
        }

        // Apply unsharp mask
        float amount = 1.5f;
        for (size_t i = 0; i < frame.y_plane.size(); ++i)
        {
            int diff = static_cast<int>(frame.y_plane[i]) - static_cast<int>(blurred[i]);
            int sharpened = static_cast<int>(frame.y_plane[i]) + static_cast<int>(diff * amount);
            frame.y_plane[i] = static_cast<uint8_t>(std::min(255, std::max(0, sharpened)));
        }
    }

    static void apply_motion_compensation(VideoFrame& frame, float offset_x, float offset_y)
    {
        // Simplified motion compensation (just shift pixels)
        std::vector<uint8_t> temp_y = frame.y_plane;

        for (size_t y = 0; y < frame.height; ++y)
        {
            for (size_t x = 0; x < frame.width; ++x)
            {
                int src_x = static_cast<int>(x + offset_x);
                int src_y = static_cast<int>(y + offset_y);

                if (src_x >= 0 && src_x < static_cast<int>(frame.width) && src_y >= 0 &&
                    src_y < static_cast<int>(frame.height))
                {
                    frame.y_plane[y * frame.stride_y + x] = temp_y[src_y * frame.stride_y + src_x];
                }
            }
        }
    }
};

// =============================================================================
// Audio/Video Processing Benchmarks
// =============================================================================

static void BM_Audio_Encoding(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const frames_per_batch = state.range(1);
    size_t const sample_rate = 44100;
    size_t const duration_ms = 1000; // 1 second per frame

    HighPerformancePool pool(num_threads);
    pool.configure_threads("audio_encoder");
    pool.distribute_across_cpus();

    for (auto _ : state)
    {
        std::atomic<size_t> encoded_frames{0};
        std::atomic<size_t> total_bytes{0};

        // Submit audio encoding tasks
        for (size_t i = 0; i < frames_per_batch; ++i)
        {
            AudioFrame frame;
            frame.sample_rate = sample_rate;
            frame.channels = 2;
            frame.duration_ms = duration_ms;
            frame.samples_left.resize(sample_rate * duration_ms / 1000);
            frame.samples_right.resize(sample_rate * duration_ms / 1000);

            // Generate test audio (sine wave)
            for (size_t j = 0; j < frame.samples_left.size(); ++j)
            {
                double t = static_cast<double>(j) / sample_rate;
                frame.samples_left[j] = 0.5f * std::sin(2.0 * M_PI * 440.0 * t); // A4 note
                frame.samples_right[j] = 0.5f * std::sin(2.0 * M_PI * 440.0 * t);
            }

            pool.submit([&frame, &encoded_frames, &total_bytes]() {
                auto encoded = AudioWorkloads::encode_audio(frame, "AAC", 128);
                encoded_frames.fetch_add(1, std::memory_order_relaxed);
                total_bytes.fetch_add(encoded.size(), std::memory_order_relaxed);
            });
        }

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["encoded_frames"] = benchmark::Counter(encoded_frames.load());
        state.counters["total_bytes_mb"] = benchmark::Counter(total_bytes.load() / (1024.0 * 1024.0));
        state.counters["compression_ratio"] = benchmark::Counter(
            (total_bytes.load() / (1024.0 * 1024.0)) /
            (frames_per_batch * sample_rate * 2 * 2 / (1024.0 * 1024.0))); // 2 channels * 2 bytes per sample
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
        state.counters["avg_task_time_ms"] =
            benchmark::Counter(static_cast<double>(stats.avg_task_time.count()) / 1000.0);

        benchmark::DoNotOptimize(encoded_frames.load());
    }

    state.SetItemsProcessed(state.iterations() * frames_per_batch);
    state.SetLabel("threads=" + std::to_string(num_threads) + " frames=" + std::to_string(frames_per_batch));
}

static void BM_Video_Encoding(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const frames_per_batch = state.range(1);
    size_t const width = 1920;
    size_t const height = 1080;

    HighPerformancePool pool(num_threads);
    pool.configure_threads("video_encoder");
    pool.distribute_across_cpus();

    for (auto _ : state)
    {
        std::atomic<size_t> encoded_frames{0};
        std::atomic<size_t> total_bytes{0};

        // Submit video encoding tasks
        for (size_t i = 0; i < frames_per_batch; ++i)
        {
            VideoFrame frame;
            frame.width = width;
            frame.height = height;
            frame.stride_y = width;
            frame.stride_uv = width / 2;
            frame.format = "YUV420P";

            // Generate test video frame (gradient pattern)
            frame.y_plane.resize(width * height);
            frame.u_plane.resize((width / 2) * (height / 2));
            frame.v_plane.resize((width / 2) * (height / 2));

            for (size_t y = 0; y < height; ++y)
            {
                for (size_t x = 0; x < width; ++x)
                {
                    size_t idx = y * width + x;
                    frame.y_plane[idx] = static_cast<uint8_t>((x + y) % 256);

                    if (y % 2 == 0 && x % 2 == 0)
                    {
                        size_t uv_idx = (y / 2) * (width / 2) + (x / 2);
                        if (uv_idx < frame.u_plane.size())
                        {
                            frame.u_plane[uv_idx] = static_cast<uint8_t>(x % 256);
                            frame.v_plane[uv_idx] = static_cast<uint8_t>(y % 256);
                        }
                    }
                }
            }

            pool.submit([&frame, &encoded_frames, &total_bytes]() {
                auto encoded = VideoWorkloads::encode_video_frame(frame, "H264", 5000);
                encoded_frames.fetch_add(1, std::memory_order_relaxed);
                total_bytes.fetch_add(encoded.size(), std::memory_order_relaxed);
            });
        }

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["encoded_frames"] = benchmark::Counter(encoded_frames.load());
        state.counters["total_bytes_mb"] = benchmark::Counter(total_bytes.load() / (1024.0 * 1024.0));
        state.counters["compression_ratio"] =
            benchmark::Counter((total_bytes.load() / (1024.0 * 1024.0)) /
                               ((width * height * 1.5) / (1024.0 * 1024.0))); // YUV420P = 1.5 bytes per pixel
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
        state.counters["avg_task_time_ms"] =
            benchmark::Counter(static_cast<double>(stats.avg_task_time.count()) / 1000.0);

        benchmark::DoNotOptimize(encoded_frames.load());
    }

    state.SetItemsProcessed(state.iterations() * frames_per_batch);
    state.SetLabel("threads=" + std::to_string(num_threads) + " frames=" + std::to_string(frames_per_batch) +
                   " resolution=1920x1080");
}

static void BM_AudioVideo_Pipeline_Processing(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const frames_to_process = state.range(1);

    HighPerformancePool pool(num_threads);
    pool.configure_threads("pipeline_worker");
    pool.distribute_across_cpus();

    FrameQueue<AudioFrame> audio_queue;
    FrameQueue<VideoFrame> video_queue;
    FrameQueue<std::pair<AudioFrame, VideoFrame>> processed_queue;

    std::atomic<size_t> processed_frames{0};

    for (auto _ : state)
    {
        // Submit pipeline processing tasks
        for (size_t i = 0; i < frames_to_process; ++i)
        {
            // Audio processing task
            pool.submit([&audio_queue, &video_queue, &processed_queue, &processed_frames]() {
                AudioFrame audio_frame;
                if (audio_queue.pop(audio_frame, std::chrono::milliseconds(10)))
                {
                    // Apply multiple audio filters
                    audio_frame = AudioWorkloads::apply_audio_filter(audio_frame, "equalizer");
                    audio_frame = AudioWorkloads::apply_audio_filter(audio_frame, "noise_reduction");

                    // Wait for corresponding video frame (simplified synchronization)
                    VideoFrame video_frame;
                    if (video_queue.pop(video_frame, std::chrono::milliseconds(10)))
                    {
                        processed_queue.push({audio_frame, video_frame});
                        processed_frames.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });

            // Video processing task
            pool.submit([&video_queue, &audio_queue, &processed_queue, &processed_frames]() {
                VideoFrame video_frame;
                if (video_queue.pop(video_frame, std::chrono::milliseconds(10)))
                {
                    // Apply multiple video filters
                    video_frame = VideoWorkloads::apply_video_filter(video_frame, "denoise");
                    video_frame = VideoWorkloads::apply_video_filter(video_frame, "sharpen");
                    video_frame = VideoWorkloads::apply_video_filter(video_frame, "color_correction");

                    // Wait for corresponding audio frame (simplified synchronization)
                    AudioFrame audio_frame;
                    if (audio_queue.pop(audio_frame, std::chrono::milliseconds(10)))
                    {
                        processed_queue.push({audio_frame, video_frame});
                        processed_frames.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });

            // Generate test frames
            AudioFrame audio_frame;
            audio_frame.sample_rate = 44100;
            audio_frame.channels = 2;
            audio_frame.duration_ms = 33; // ~30fps audio
            audio_frame.samples_left.resize(44100 * 33 / 1000);
            audio_frame.samples_right.resize(44100 * 33 / 1000);

            VideoFrame video_frame;
            video_frame.width = 1920;
            video_frame.height = 1080;
            video_frame.stride_y = 1920;
            video_frame.stride_uv = 960;
            video_frame.format = "YUV420P";
            video_frame.y_plane.resize(1920 * 1080);
            video_frame.u_plane.resize(960 * 540);
            video_frame.v_plane.resize(960 * 540);

            audio_queue.push(audio_frame);
            video_queue.push(video_frame);
        }

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["processed_frames"] = benchmark::Counter(processed_frames.load());
        state.counters["pipeline_efficiency"] = benchmark::Counter(100.0 * processed_frames.load() / frames_to_process);
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));
        state.counters["avg_task_time_ms"] =
            benchmark::Counter(static_cast<double>(stats.avg_task_time.count()) / 1000.0);

        benchmark::DoNotOptimize(processed_frames.load());
    }

    state.SetItemsProcessed(state.iterations() * frames_to_process);
    state.SetLabel("threads=" + std::to_string(num_threads) + " frames=" + std::to_string(frames_to_process));
}

static void BM_RealTime_Streaming_Processing(benchmark::State& state)
{
    size_t const num_threads = state.range(0);
    size_t const stream_duration_seconds = 5;
    size_t const fps = 30;

    HighPerformancePool pool(num_threads);
    pool.configure_threads("streaming_worker");
    pool.distribute_across_cpus();

    FrameQueue<VideoFrame> input_queue;
    FrameQueue<VideoFrame> output_queue;

    for (auto _ : state)
    {
        std::atomic<size_t> processed_frames{0};
        std::atomic<size_t> dropped_frames{0};
        std::atomic<double> total_latency_ms{0.0};

        auto start_time = std::chrono::steady_clock::now();

        // Producer thread (simulates camera capture)
        std::thread producer([&]() {
            for (size_t frame_num = 0; frame_num < fps * stream_duration_seconds; ++frame_num)
            {
                VideoFrame frame;
                frame.width = 1280;
                frame.height = 720;
                frame.stride_y = 1280;
                frame.stride_uv = 640;
                frame.format = "YUV420P";
                frame.y_plane.resize(1280 * 720);
                frame.u_plane.resize(640 * 360);
                frame.v_plane.resize(640 * 360);

                // Check if queue is getting full (simulate real-time constraints)
                if (input_queue.size() > 10)
                {
                    dropped_frames.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    input_queue.push(frame);
                }

                // Simulate frame timing (30fps)
                auto frame_time = std::chrono::milliseconds(1000 / fps);
                std::this_thread::sleep_for(frame_time);
            }
        });

        // Processing tasks
        for (size_t i = 0; i < fps * stream_duration_seconds; ++i)
        {
            pool.submit([&input_queue, &output_queue, &processed_frames, &total_latency_ms, &start_time]() {
                auto submit_time = std::chrono::steady_clock::now();

                VideoFrame frame;
                if (input_queue.pop(frame, std::chrono::milliseconds(50)))
                {
                    // Apply real-time processing (stabilization + enhancement)
                    std::vector<float> motion_vectors = {0.5f, -0.3f, 0.1f}; // Simulated motion data
                    frame = VideoWorkloads::stabilize_video_frame(frame, motion_vectors);
                    frame = VideoWorkloads::apply_video_filter(frame, "sharpen");

                    output_queue.push(frame);
                    processed_frames.fetch_add(1, std::memory_order_relaxed);

                    auto process_time = std::chrono::steady_clock::now();
                    double latency =
                        std::chrono::duration_cast<std::chrono::microseconds>(process_time - submit_time).count() /
                        1000.0;
                    total_latency_ms.fetch_add(latency, std::memory_order_relaxed);
                }
            });
        }

        producer.join();

        // Wait for completion
        auto stats = pool.get_statistics();

        state.counters["processed_frames"] = benchmark::Counter(processed_frames.load());
        state.counters["dropped_frames"] = benchmark::Counter(dropped_frames.load());
        state.counters["target_fps"] = benchmark::Counter(fps);
        state.counters["actual_fps"] =
            benchmark::Counter(static_cast<double>(processed_frames.load()) / stream_duration_seconds);
        state.counters["drop_rate_percent"] = benchmark::Counter(
            100.0 * dropped_frames.load() / std::max(processed_frames.load() + dropped_frames.load(), size_t(1)));
        state.counters["avg_latency_ms"] =
            benchmark::Counter(total_latency_ms.load() / std::max(processed_frames.load(), size_t(1)));
        state.counters["work_steal_ratio"] =
            benchmark::Counter(100.0 * stats.stolen_tasks / std::max(stats.completed_tasks, size_t(1)));

        benchmark::DoNotOptimize(processed_frames.load());
    }

    state.SetItemsProcessed(state.iterations() * fps * stream_duration_seconds);
    state.SetLabel("threads=" + std::to_string(num_threads) + " duration=" + std::to_string(stream_duration_seconds) +
                   "s" + " fps=" + std::to_string(fps));
}

// =============================================================================
// Registration
// =============================================================================

BENCHMARK(BM_Audio_Encoding)
    ->Args({2, 100}) // 2 threads, 100 audio frames
    ->Args({4, 100}) // 4 threads, 100 audio frames
    ->Args({8, 100}) // 8 threads, 100 audio frames
    ->Args({4, 500}) // 4 threads, 500 audio frames
    ->Args({8, 500}) // 8 threads, 500 audio frames
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Video_Encoding)
    ->Args({2, 50})  // 2 threads, 50 video frames (1080p)
    ->Args({4, 50})  // 4 threads, 50 video frames (1080p)
    ->Args({8, 50})  // 8 threads, 50 video frames (1080p)
    ->Args({4, 100}) // 4 threads, 100 video frames (1080p)
    ->Args({8, 100}) // 8 threads, 100 video frames (1080p)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_AudioVideo_Pipeline_Processing)
    ->Args({2, 100}) // 2 threads, 100 frame pairs
    ->Args({4, 100}) // 4 threads, 100 frame pairs
    ->Args({8, 100}) // 8 threads, 100 frame pairs
    ->Args({4, 500}) // 4 threads, 500 frame pairs
    ->Args({8, 500}) // 8 threads, 500 frame pairs
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_RealTime_Streaming_Processing)
    ->Args({2, 30}) // 2 threads, 30fps for 5 seconds
    ->Args({4, 30}) // 4 threads, 30fps for 5 seconds
    ->Args({8, 30}) // 8 threads, 30fps for 5 seconds
    ->Args({4, 60}) // 4 threads, 60fps for 5 seconds
    ->Args({8, 60}) // 8 threads, 60fps for 5 seconds
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
