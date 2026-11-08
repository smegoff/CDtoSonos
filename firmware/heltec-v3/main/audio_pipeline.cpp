#include "audio_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/pulse_cnt.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace cdts {
namespace {
constexpr char kTag[] = "audio";
constexpr float kSilenceThresholdDbfs = -55.0F;
// Levels and continuous spectrum filters process every block (~43 Hz).
// Spectrum display snapshots are published at ~10.8 Hz. Capture still
// preempts this low-priority task.
constexpr uint64_t kSpectrumIntervalBlocks = 4;
// i2s_channel_read accepts milliseconds, unlike the FreeRTOS APIs.
constexpr uint32_t kReadTimeoutMs = 150;
constexpr int64_t kClockLossUs = 1200000;

static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "The I2S ISR counter must never acquire a lock");

bool IRAM_ATTR on_receive_queue_overflow(i2s_chan_handle_t,
                                         i2s_event_data_t*, void* context) {
    // No logging, allocation, or task mutex use in this callback. The pipeline
    // and its counter live in internal RAM on this no-PSRAM board.
    static_cast<std::atomic<uint32_t>*>(context)->fetch_add(
        1, std::memory_order_relaxed);
    return false;
}
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
    i2s_event_callbacks_t callbacks = {};
    callbacks.on_recv_q_ovf = on_receive_queue_overflow;
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(
        receiver, &callbacks, &dma_queue_overflows_));
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
    int64_t last_received_us = esp_timer_get_time();
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
                             kReadTimeoutMs);

        const int64_t now = esp_timer_get_time();
        if ((result == ESP_OK || result == ESP_ERR_TIMEOUT) && bytes_read > 0) {
            last_received_us = now;
        } else if (now - last_received_us >= kClockLossUs) {
            eq_.reset_history();
            if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (metrics_.clock_locked) ++metrics_.clock_loss_events;
                metrics_.clock_locked = false;
                metrics_.signal_present = false;
                metrics_.peak_left_dbfs = -96.0F;
                metrics_.peak_right_dbfs = -96.0F;
                metrics_.rms_left_dbfs = -96.0F;
                metrics_.rms_right_dbfs = -96.0F;
                metrics_.spectrum_dbfs.fill(-96.0F);
                xSemaphoreGive(mutex_);
            }
        }
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
        accumulated_bytes += bytes_read;
        if (accumulated_bytes < sizeof(raw)) {
            continue;
        }
        accumulated_bytes = 0;

        // The peripheral has already extracted the 16 audio bits from each
        // 24-bit right-justified slot. Apply EQ once for all stream listeners.
        process_eq(raw.data());
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
        analyze(samples.data(), gpio_get_level(board::kEmphasis) != 0,
                (sequence % kSpectrumIntervalBlocks) == 0);
    }
    vTaskDelete(nullptr);
}

void AudioPipeline::set_eq(EqPreset preset, const EqGains& gains) {
    const EqConfig config = EqConfig::make(preset, gains, board::kSampleRate);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    requested_eq_ = config;
    ++eq_revision_;
    xSemaphoreGive(mutex_);
}

EqStatus AudioPipeline::eq_status() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const EqStatus status = {requested_eq_.preset, requested_eq_.gains,
        requested_eq_.headroom_db, eq_revision_.load() == eq_applied_revision_.load(),
        eq_clipped_samples_.load()};
    xSemaphoreGive(mutex_);
    return status;
}

void AudioPipeline::process_eq(int16_t* samples) {
    uint32_t applied = eq_applied_revision_.load();
    if (eq_revision_.load() != applied && xSemaphoreTake(mutex_, 0) == pdTRUE) {
        const EqConfig config = requested_eq_;
        applied = eq_revision_.load();
        xSemaphoreGive(mutex_);
        eq_.configure(config);
    }
    const uint32_t clipped = eq_.process(samples, board::kFlacBlockSamples);
    if (clipped != 0) eq_clipped_samples_.fetch_add(clipped);
    eq_applied_revision_.store(applied);
}

void AudioPipeline::publish(const int16_t* samples) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) {
        capture_publish_drops_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint64_t sequence = ++latest_sequence_;
    PcmBlock& block = ring_[sequence % kRingBlocks];
    block.sequence = sequence;
    std::copy_n(samples, kSamplesPerBlock, block.samples.begin());
    metrics_.clock_locked = true;
    metrics_.captured_frames += board::kFlacBlockSamples;
    last_published_us_ = esp_timer_get_time();
    xSemaphoreGive(mutex_);
    xSemaphoreGive(block_ready_);
}

float AudioPipeline::to_dbfs(float value) {
    if (value <= 1.0F) {
        return -96.0F;
    }
    return std::max(-96.0F, 20.0F * std::log10(value / 32768.0F));
}

void AudioPipeline::analyze(const int16_t* samples, bool emphasis,
                            bool update_spectrum) {
    const int64_t dsp_started_us = esp_timer_get_time();
    int32_t peak_left = 0;
    int32_t peak_right = 0;
    uint64_t sum_left = 0;
    uint64_t sum_right = 0;

    for (size_t frame = 0; frame < board::kFlacBlockSamples; ++frame) {
        const int32_t left = samples[frame * 2];
        const int32_t right = samples[(frame * 2) + 1];
        peak_left = std::max(peak_left, std::abs(left));
        peak_right = std::max(peak_right, std::abs(right));
        // A squared int16_t fits int32_t, but a full block needs uint64_t.
        sum_left += static_cast<uint32_t>(left * left);
        sum_right += static_cast<uint32_t>(right * right);
    }

    const uint32_t now_ms =
        static_cast<uint32_t>(esp_timer_get_time() / 1000);
    constexpr uint32_t kPeakHistoryMs =
        (5U * board::kFlacBlockSamples * 1000U) / board::kSampleRate;
    if (now_ms - peak_history_updated_ms_ >= kPeakHistoryMs) {
        peak_history_.fill({0, 0});
        spectrum_analyzer_.reset();
    }
    peak_history_updated_ms_ = now_ms;
    peak_history_[peak_history_index_] = {
        static_cast<uint16_t>(peak_left), static_cast<uint16_t>(peak_right)};
    peak_history_index_ = (peak_history_index_ + 1) % peak_history_.size();
    for (const auto& peaks : peak_history_) {
        peak_left = std::max(peak_left, static_cast<int32_t>(peaks[0]));
        peak_right = std::max(peak_right, static_cast<int32_t>(peaks[1]));
    }

    spectrum_analyzer_.process(samples, board::kFlacBlockSamples);
    std::array<float, SpectrumAnalyzer::kBandCount> spectrum = {};
    if (update_spectrum) {
        spectrum = spectrum_analyzer_.levels_dbfs();
    }

    const float peak_l = to_dbfs(static_cast<float>(peak_left));
    const float peak_r = to_dbfs(static_cast<float>(peak_right));
    const float rms_l = to_dbfs(std::sqrt(
        static_cast<float>(sum_left) / board::kFlacBlockSamples));
    const float rms_r = to_dbfs(std::sqrt(
        static_cast<float>(sum_right) / board::kFlacBlockSamples));
    const uint32_t dsp_us = static_cast<uint32_t>(
        esp_timer_get_time() - dsp_started_us);

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Analysis can finish after capture declares clock loss. Do not
        // resurrect stale signal levels from the buffered block in that case.
        if (!metrics_.clock_locked) {
            xSemaphoreGive(mutex_);
            return;
        }
        metrics_.peak_left_dbfs = peak_l;
        metrics_.peak_right_dbfs = peak_r;
        metrics_.rms_left_dbfs = rms_l;
        metrics_.rms_right_dbfs = rms_r;
        metrics_.signal_present =
            std::max(rms_l, rms_r) >= kSilenceThresholdDbfs;
        metrics_.emphasis = emphasis;
        metrics_.meter_updated_ms =
            static_cast<uint32_t>(esp_timer_get_time() / 1000);
        metrics_.analysis_dsp_us = dsp_us;
        metrics_.analysis_dsp_max_us =
            std::max(metrics_.analysis_dsp_max_us, dsp_us);
        ++metrics_.meter_updates;
        if (update_spectrum) {
            metrics_.spectrum_dbfs = spectrum;
            metrics_.spectrum_updated_ms = metrics_.meter_updated_ms;
            ++metrics_.spectrum_updates;
            ++metrics_.analysis_runs;
        }
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

AudioMetrics AudioPipeline::meters() const {
    AudioMetrics copy;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
        copy = metrics_;
        if (last_published_us_ != 0) {
            const uint64_t age_ms = static_cast<uint64_t>(
                (esp_timer_get_time() - last_published_us_) / 1000);
            copy.capture_age_ms = static_cast<uint32_t>(
                std::min<uint64_t>(age_ms, UINT32_MAX - 1ULL));
        }
        xSemaphoreGive(mutex_);
    }
    copy.dma_queue_overflows =
        dma_queue_overflows_.load(std::memory_order_relaxed);
    copy.capture_publish_drops =
        capture_publish_drops_.load(std::memory_order_relaxed);
    return copy;
}

AudioMetrics AudioPipeline::metrics() const {
    AudioMetrics copy = meters();
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
