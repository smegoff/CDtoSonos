#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "board.hpp"
#include "spectrum_analyzer.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace cdts {

struct AudioMetrics {
    bool clock_locked = false;
    bool signal_present = false;
    bool emphasis = false;
    float peak_left_dbfs = -96.0F;
    float peak_right_dbfs = -96.0F;
    float rms_left_dbfs = -96.0F;
    float rms_right_dbfs = -96.0F;
    std::array<float, SpectrumAnalyzer::kBandCount> spectrum_dbfs = {};
    uint64_t captured_frames = 0;
    uint32_t read_timeouts = 0;
    uint32_t dma_queue_overflows = 0;
    uint32_t capture_publish_drops = 0;
    uint32_t clock_loss_events = 0;
    // UINT32_MAX means no complete PCM block has been published yet.
    uint32_t capture_age_ms = UINT32_MAX;
    uint32_t overruns = 0;
    uint32_t analysis_runs = 0;
    uint32_t meter_updates = 0;
    uint32_t spectrum_updates = 0;
    // Uptime milliseconds at publication; uint32_t wraps after 49.7 days.
    uint32_t meter_updated_ms = 0;
    uint32_t spectrum_updated_ms = 0;
    // Analysis wall time, including task preemption, excluding publication.
    uint32_t analysis_dsp_us = 0;
    uint32_t analysis_dsp_max_us = 0;
    uint32_t capture_stack_free = 0;
    uint32_t analysis_stack_free = 0;
    uint32_t source_rate_hz = 0;
};

class AudioPipeline {
public:
    static constexpr size_t kSamplesPerBlock =
        board::kFlacBlockSamples * board::kChannels;
    // About 0.56 seconds at 44.1 kHz. This absorbs Wi-Fi scheduling jitter
    // while leaving sufficient internal RAM for the network stack and client.
    static constexpr size_t kRingBlocks = 24;

    AudioPipeline();
    ~AudioPipeline();

    bool start();
    bool copy_after(uint64_t last_sequence,
                    int16_t* destination,
                    size_t destination_samples,
                    uint64_t* sequence,
                    uint32_t timeout_ms);
    AudioMetrics metrics() const;
    // Snapshot for frequent visual polling; no task stack watermark scans.
    AudioMetrics meters() const;

private:
    struct PcmBlock {
        uint64_t sequence = 0;
        std::array<int16_t, kSamplesPerBlock> samples = {};
    };

    static void capture_task_entry(void* context);
    static void analysis_task_entry(void* context);
    void capture_task();
    void analysis_task();
    void publish(const int16_t* samples);
    void analyze(const int16_t* samples, bool emphasis, bool update_spectrum);
    static float to_dbfs(float value);

    std::array<PcmBlock, kRingBlocks> ring_ = {};
    // DMA scratch belongs to the long-lived pipeline object rather than the
    // capture task stack. The I2S peripheral extracts the 16 data bits from
    // each 24-bit wire slot, so this buffer is already native PCM.
    std::array<int16_t, kSamplesPerBlock> raw_buffer_ = {};
    std::array<int16_t, kSamplesPerBlock> analysis_buffer_ = {};
    // Current plus four preceding blocks: about 116 ms of peak history.
    // Accessed only by the analysis task; RMS remains a single-block value.
    std::array<std::array<uint16_t, 2>, 5> peak_history_ = {};
    size_t peak_history_index_ = 0;
    uint32_t peak_history_updated_ms_ = 0;
    SpectrumAnalyzer spectrum_analyzer_{board::kSampleRate,
                                        board::kFlacBlockSamples};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    SemaphoreHandle_t block_ready_ = nullptr;
    TaskHandle_t capture_task_handle_ = nullptr;
    TaskHandle_t analysis_task_handle_ = nullptr;
    std::atomic<bool> running_ = false;
    // The DMA overflow counter is also incremented from the I2S ISR.
    std::atomic<uint32_t> dma_queue_overflows_ = 0;
    std::atomic<uint32_t> capture_publish_drops_ = 0;
    uint64_t latest_sequence_ = 0;
    int64_t last_published_us_ = 0;  // Protected by mutex_.
    AudioMetrics metrics_ = {};
};

}  // namespace cdts
