#include "audio_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace cdts {
namespace {
constexpr char kTag[] = "audio";
constexpr float kSilenceThresholdDbfs = -55.0F;
constexpr float kPi = 3.14159265358979323846F;
// About 5.4 display updates per second at 44.1 kHz. This task remains
// low-priority on the capture core, so I2S capture always preempts it.
constexpr uint64_t kAnalysisIntervalBlocks = 8;
constexpr uint32_t kClockLossTimeouts = 8;
constexpr std::array<float, 16> kSpectrumFrequencies = {
    63.0F, 100.0F, 160.0F, 250.0F, 400.0F, 630.0F, 1000.0F, 1600.0F,
    2500.0F, 4000.0F, 6300.0F, 8000.0F, 10000.0F, 12500.0F, 16000.0F,
    20000.0F};
}

AudioPipeline::AudioPipeline() {
    mutex_ = xSemaphoreCreateMutex();
    block_ready_ = xSemaphoreCreateBinary();
    metrics_.spectrum_dbfs.fill(-96.0F);
}

AudioPipeline::~AudioPipeline() {
    running_.store(false);
    if (mutex_ != nullptr) vSemaphoreDelete(mutex_);
    if (block_ready_ != nullptr) vSemaphoreDelete(block_ready_);
}

bool AudioPipeline::start() {
    if (mutex_ == nullptr || block_ready_ == nullptr) {
        return false;
    }
    running_.store(true);
    if (xTaskCreatePinnedToCore(capture_task_entry, "i2s_capture", 6144, this,
                                18, &capture_task_handle_, 1) != pdPASS) {
        running_.store(false);
        return false;
    }
    if (xTaskCreatePinnedToCore(analysis_task_entry, "audio_analysis", 4096,
                                this, 4, &analysis_task_handle_, 1) != pdPASS) {
        running_.store(false);
        return false;
    }
    return true;
}

void AudioPipeline::capture_task_entry(void* context) {
    static_cast<AudioPipeline*>(context)->capture_task();
}

void AudioPipeline::analysis_task_entry(void* context) {
    static_cast<AudioPipeline*>(context)->analysis_task();
}

void AudioPipeline::capture_task() {
    i2s_chan_handle_t receiver = nullptr;
    i2s_chan_config_t channel =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
    channel.dma_desc_num = 16;
    channel.dma_frame_num = 256;
    channel.auto_clear = false;

    ESP_ERROR_CHECK(i2s_new_channel(&channel, nullptr, &receiver));

    i2s_std_config_t standard = {};
    standard.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(board::kSampleRate);
    standard.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    standard.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    standard.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_24BIT;
    standard.slot_cfg.ws_width = board::kSlotBits;
    standard.slot_cfg.left_align = false;
    standard.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    standard.gpio_cfg.bclk = board::kI2sBclk;
    standard.gpio_cfg.ws = board::kI2sLrclk;
    standard.gpio_cfg.dout = I2S_GPIO_UNUSED;
    standard.gpio_cfg.din = board::kI2sData;
    standard.gpio_cfg.invert_flags.mclk_inv = false;
    standard.gpio_cfg.invert_flags.bclk_inv = false;
    standard.gpio_cfg.invert_flags.ws_inv = false;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(receiver, &standard));
    ESP_ERROR_CHECK(i2s_channel_enable(receiver));

    pcnt_unit_handle_t lrclk_counter = nullptr;
    pcnt_channel_handle_t lrclk_channel = nullptr;
    const pcnt_unit_config_t counter_config = {
        .low_limit = -1,
        .high_limit = 30000,
        .intr_priority = 0,
        .flags = {},
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&counter_config, &lrclk_counter));
    const pcnt_chan_config_t lrclk_config = {
        .edge_gpio_num = static_cast<int>(board::kI2sLrclk),
        .level_gpio_num = -1,
        .flags = {},
    };
    ESP_ERROR_CHECK(
        pcnt_new_channel(lrclk_counter, &lrclk_config, &lrclk_channel));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
        lrclk_channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(
        lrclk_channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP));
    ESP_ERROR_CHECK(pcnt_unit_enable(lrclk_counter));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(lrclk_counter));
    ESP_ERROR_CHECK(pcnt_unit_start(lrclk_counter));
    int64_t rate_window_started = esp_timer_get_time();

    gpio_config_t emphasis_config = {};
    emphasis_config.pin_bit_mask = 1ULL << board::kEmphasis;
    emphasis_config.mode = GPIO_MODE_INPUT;
    emphasis_config.pull_up_en = GPIO_PULLUP_DISABLE;
    emphasis_config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    emphasis_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&emphasis_config));

    auto& raw = raw_buffer_;
    size_t accumulated_bytes = 0;
    uint32_t consecutive_empty_reads = 0;
    ESP_LOGI(kTag,
             "I2S RX ready: slave, right-justified, 44.1kHz, stereo, "
             "16-bit data in 24-bit slots");

    while (running_.load()) {
        size_t bytes_read = 0;
        uint8_t* write_at =
            reinterpret_cast<uint8_t*>(raw.data()) + accumulated_bytes;
        const size_t remaining = sizeof(raw) - accumulated_bytes;
        const esp_err_t result =
            i2s_channel_read(receiver, write_at, remaining, &bytes_read,
                             pdMS_TO_TICKS(150));

        const int64_t now = esp_timer_get_time();
        const int64_t rate_elapsed = now - rate_window_started;
        if (rate_elapsed >= 250000) {
            int lrclk_edges = 0;
            if (pcnt_unit_get_count(lrclk_counter, &lrclk_edges) == ESP_OK) {
                pcnt_unit_clear_count(lrclk_counter);
                const uint32_t measured_rate = static_cast<uint32_t>(
                    (static_cast<uint64_t>(lrclk_edges) * 1000000ULL) /
                    static_cast<uint64_t>(rate_elapsed));
                if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
                    metrics_.source_rate_hz = measured_rate;
                    xSemaphoreGive(mutex_);
                }
            }
            rate_window_started = now;
        }

        if (result == ESP_ERR_TIMEOUT && bytes_read == 0) {
            if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
                ++metrics_.read_timeouts;
                if (++consecutive_empty_reads >= kClockLossTimeouts) {
                    metrics_.clock_locked = false;
                }
                xSemaphoreGive(mutex_);
            }
            continue;
        }
        if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
            ESP_LOGW(kTag, "I2S read failed: %s", esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // ESP-IDF may return ESP_ERR_TIMEOUT after copying a valid partial DMA
        // buffer. Retain those bytes and complete the block on the next read.
        consecutive_empty_reads = 0;
        accumulated_bytes += bytes_read;
        if (accumulated_bytes < sizeof(raw)) {
            continue;
        }
        accumulated_bytes = 0;

        // The peripheral has already extracted the 16 audio bits from each
        // 24-bit right-justified slot. Publish unchanged: no resampling/gain.
        publish(raw.data());
    }

    pcnt_unit_stop(lrclk_counter);
    pcnt_unit_disable(lrclk_counter);
    pcnt_del_channel(lrclk_channel);
    pcnt_del_unit(lrclk_counter);
    i2s_channel_disable(receiver);
    i2s_del_channel(receiver);
    vTaskDelete(nullptr);
}

void AudioPipeline::analysis_task() {
    auto& samples = analysis_buffer_;
    uint64_t sequence = 0;

    while (running_.load()) {
        uint64_t next_sequence = 0;
        if (!copy_after(sequence, samples.data(), samples.size(),
                        &next_sequence, 1000)) {
            continue;
        }
        sequence = next_sequence;
        if ((sequence % kAnalysisIntervalBlocks) != 0) {
            continue;
        }
        analyze(samples.data(), gpio_get_level(board::kEmphasis) != 0);
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
            ++metrics_.analysis_runs;
            xSemaphoreGive(mutex_);
        }
    }
    vTaskDelete(nullptr);
}

void AudioPipeline::publish(const int16_t* samples) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }
    const uint64_t sequence = ++latest_sequence_;
    PcmBlock& block = ring_[sequence % kRingBlocks];
    block.sequence = sequence;
    std::copy_n(samples, kSamplesPerBlock, block.samples.begin());
    metrics_.clock_locked = true;
    metrics_.captured_frames += board::kFlacBlockSamples;
    xSemaphoreGive(mutex_);
    xSemaphoreGive(block_ready_);
}

float AudioPipeline::to_dbfs(double value) {
    if (value <= 1.0) {
        return -96.0F;
    }
    return static_cast<float>(
        std::max(-96.0, 20.0 * std::log10(value / 32768.0)));
}

void AudioPipeline::analyze(const int16_t* samples, bool emphasis) {
    int32_t peak_left = 0;
    int32_t peak_right = 0;
    double sum_left = 0.0;
    double sum_right = 0.0;

    for (size_t frame = 0; frame < board::kFlacBlockSamples; ++frame) {
        const int32_t left = samples[frame * 2];
        const int32_t right = samples[(frame * 2) + 1];
        peak_left = std::max(peak_left, std::abs(left));
        peak_right = std::max(peak_right, std::abs(right));
        sum_left += static_cast<double>(left) * left;
        sum_right += static_cast<double>(right) * right;
    }

    std::array<float, 16> spectrum = {};
    for (size_t band = 0; band < kSpectrumFrequencies.size(); ++band) {
        const float omega =
            (2.0 * kPi * kSpectrumFrequencies[band]) / board::kSampleRate;
        const float coefficient = 2.0F * std::cos(omega);
        float q0 = 0.0F;
        float q1 = 0.0F;
        float q2 = 0.0F;
        for (size_t frame = 0; frame < board::kFlacBlockSamples; ++frame) {
            const float mono =
                (static_cast<float>(samples[frame * 2]) +
                 static_cast<float>(samples[(frame * 2) + 1])) *
                0.5F;
            q0 = (coefficient * q1) - q2 + mono;
            q2 = q1;
            q1 = q0;
        }
        const float power =
            std::max(0.0F,
                     (q1 * q1) + (q2 * q2) - (coefficient * q1 * q2));
        const float magnitude =
            std::sqrt(power) /
            (static_cast<float>(board::kFlacBlockSamples) * 0.5F);
        spectrum[band] = to_dbfs(magnitude);
    }

    const float peak_l = to_dbfs(peak_left);
    const float peak_r = to_dbfs(peak_right);
    const float rms_l =
        to_dbfs(std::sqrt(sum_left / board::kFlacBlockSamples));
    const float rms_r =
        to_dbfs(std::sqrt(sum_right / board::kFlacBlockSamples));

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        metrics_.peak_left_dbfs = peak_l;
        metrics_.peak_right_dbfs = peak_r;
        metrics_.rms_left_dbfs = rms_l;
        metrics_.rms_right_dbfs = rms_r;
        metrics_.signal_present =
            std::max(rms_l, rms_r) >= kSilenceThresholdDbfs;
        metrics_.emphasis = emphasis;
        metrics_.spectrum_dbfs = spectrum;
        xSemaphoreGive(mutex_);
    }
}

bool AudioPipeline::copy_after(uint64_t last_sequence,
                               int16_t* destination,
                               size_t destination_samples,
                               uint64_t* sequence,
                               uint32_t timeout_ms) {
    if (destination == nullptr || sequence == nullptr ||
        destination_samples < kSamplesPerBlock) {
        return false;
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (running_.load()) {
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (latest_sequence_ > last_sequence) {
                uint64_t wanted = last_sequence + 1;
                const uint64_t oldest =
                    latest_sequence_ >= kRingBlocks
                        ? latest_sequence_ - kRingBlocks + 1
                        : 1;
                if (wanted < oldest) {
                    // A new consumer deliberately begins at the oldest
                    // buffered block. Only count loss after it has started.
                    if (last_sequence != 0) {
                        metrics_.overruns +=
                            static_cast<uint32_t>(oldest - wanted);
                    }
                    wanted = oldest;
                }
                const PcmBlock& block = ring_[wanted % kRingBlocks];
                if (block.sequence == wanted) {
                    std::copy(block.samples.begin(), block.samples.end(),
                              destination);
                    *sequence = wanted;
                    xSemaphoreGive(mutex_);
                    return true;
                }
            }
            xSemaphoreGive(mutex_);
        }

        const TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            return false;
        }
        xSemaphoreTake(block_ready_,
                       std::min(pdMS_TO_TICKS(20), deadline - now));
    }
    return false;
}

AudioMetrics AudioPipeline::metrics() const {
    AudioMetrics copy;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
        copy = metrics_;
        xSemaphoreGive(mutex_);
    }
    if (capture_task_handle_ != nullptr) {
        copy.capture_stack_free = static_cast<uint32_t>(
            uxTaskGetStackHighWaterMark(capture_task_handle_));
    }
    if (analysis_task_handle_ != nullptr) {
        copy.analysis_stack_free = static_cast<uint32_t>(
            uxTaskGetStackHighWaterMark(analysis_task_handle_));
    }
    return copy;
}

}  // namespace cdts
