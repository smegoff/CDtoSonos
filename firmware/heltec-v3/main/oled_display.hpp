#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "audio_pipeline.hpp"
#include "lossless_server.hpp"
#include "wifi_manager.hpp"

namespace cdts {

class OledDisplay {
public:
    OledDisplay(AudioPipeline& audio,
                LosslessServer& lossless,
                WifiManager& wifi);
    bool start();

private:
    static constexpr size_t kWidth = 128;
    static constexpr size_t kHeight = 64;
    static constexpr size_t kPages = kHeight / 8;

    static void task_entry(void* context);
    void task();
    bool initialize();
    bool send_commands(const uint8_t* commands, size_t size);
    bool flush();
    void clear();
    void text(size_t column, size_t row, const std::string& value);
    void horizontal_line(size_t x, size_t y, size_t width);
    void level_bar(size_t x, size_t y, size_t width, float dbfs);
    static const uint8_t* glyph(char character);
    static const char* wifi_quality(int rssi);

    AudioPipeline& audio_;
    LosslessServer& lossless_;
    WifiManager& wifi_;
    std::array<uint8_t, kWidth * kPages> framebuffer_ = {};
};

}  // namespace cdts
