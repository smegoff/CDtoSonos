#include "oled_display.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "board.hpp"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cdts {
namespace {
constexpr char kTag[] = "oled";
constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr uint8_t kAddress = 0x3c;

// Compact 5x7 font covering ASCII space through Z. Lowercase is displayed
// as uppercase, keeping the firmware asset small and the OLED legible.
constexpr uint8_t kFont[][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5f,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},{0x14,0x7f,0x14,0x7f,0x14},
    {0x24,0x2a,0x7f,0x2a,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1c,0x22,0x41,0x00},{0x00,0x41,0x22,0x1c,0x00},
    {0x14,0x08,0x3e,0x08,0x14},{0x08,0x08,0x3e,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
    {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3e},{0x7e,0x11,0x11,0x11,0x7e},
    {0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},
    {0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},
    {0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},
    {0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},
    {0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},
    {0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},
    {0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43}
};
}

OledDisplay::OledDisplay(AudioPipeline& audio,
                         LosslessServer& lossless,
                         WifiManager& wifi)
    : audio_(audio), lossless_(lossless), wifi_(wifi) {}

bool OledDisplay::start() {
    if (!initialize()) return false;
    return xTaskCreate(task_entry, "oled", 4096, this, 3, nullptr) == pdPASS;
}

bool OledDisplay::initialize() {
    gpio_config_t power = {};
    power.pin_bit_mask = (1ULL << board::kVextEnable) |
                         (1ULL << board::kOledReset);
    power.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&power) != ESP_OK) return false;
    gpio_set_level(board::kVextEnable, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(board::kOledReset, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(board::kOledReset, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    i2c_config_t config = {};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = board::kOledSda;
    config.scl_io_num = board::kOledScl;
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = 400000;
    if (i2c_param_config(kI2cPort, &config) != ESP_OK ||
        i2c_driver_install(kI2cPort, config.mode, 0, 0, 0) != ESP_OK) {
        return false;
    }

    static constexpr uint8_t commands[] = {
        0xae,0xd5,0x80,0xa8,0x3f,0xd3,0x00,0x40,0x8d,0x14,
        0x20,0x00,0xa1,0xc8,0xda,0x12,0x81,0xcf,0xd9,0xf1,
        0xdb,0x40,0xa4,0xa6,0x2e,0xaf
    };
    if (!send_commands(commands, sizeof(commands))) return false;
    clear();
    const bool ready = flush();
    ESP_LOGI(kTag, "128x64 status display %s", ready ? "ready" : "failed");
    return ready;
}

bool OledDisplay::send_commands(const uint8_t* commands, size_t size) {
    std::array<uint8_t, 17> packet = {};
    packet[0] = 0x00;
    size_t offset = 0;
    while (offset < size) {
        const size_t count = std::min<size_t>(16, size - offset);
        std::copy_n(commands + offset, count, packet.begin() + 1);
        if (i2c_master_write_to_device(kI2cPort, kAddress, packet.data(),
                                       count + 1, pdMS_TO_TICKS(100)) !=
            ESP_OK) {
            return false;
        }
        offset += count;
    }
    return true;
}

bool OledDisplay::flush() {
    static constexpr uint8_t range[] = {0x21,0x00,0x7f,0x22,0x00,0x07};
    if (!send_commands(range, sizeof(range))) return false;
    std::array<uint8_t, 17> packet = {};
    packet[0] = 0x40;
    for (size_t offset = 0; offset < framebuffer_.size(); offset += 16) {
        std::copy_n(framebuffer_.begin() + offset, 16, packet.begin() + 1);
        if (i2c_master_write_to_device(kI2cPort, kAddress, packet.data(),
                                       packet.size(), pdMS_TO_TICKS(100)) !=
            ESP_OK) {
            return false;
        }
    }
    return true;
}

void OledDisplay::clear() {
    framebuffer_.fill(0);
}

const uint8_t* OledDisplay::glyph(char character) {
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    if (character < ' ' || character > 'Z') character = '?';
    return kFont[static_cast<unsigned>(character - ' ')];
}

void OledDisplay::text(size_t column,
                       size_t row,
                       const std::string& value) {
    if (row >= kPages) return;
    size_t x = column * 6;
    for (char character : value) {
        if (x + 5 >= kWidth) break;
        const uint8_t* pixels = glyph(character);
        for (size_t index = 0; index < 5; ++index) {
            framebuffer_[row * kWidth + x++] = pixels[index];
        }
        framebuffer_[row * kWidth + x++] = 0;
    }
}

void OledDisplay::horizontal_line(size_t x, size_t y, size_t width) {
    if (y >= kHeight) return;
    for (size_t pixel = x; pixel < std::min(kWidth, x + width); ++pixel) {
        framebuffer_[(y / 8) * kWidth + pixel] |= 1U << (y % 8);
    }
}

void OledDisplay::level_bar(size_t x,
                            size_t y,
                            size_t width,
                            float dbfs) {
    horizontal_line(x, y, width);
    const float normalized =
        std::clamp((dbfs + 60.0F) / 60.0F, 0.0F, 1.0F);
    const size_t filled = static_cast<size_t>(normalized * width);
    horizontal_line(x, y - 1, filled);
    horizontal_line(x, y - 2, filled);
}

const char* OledDisplay::wifi_quality(int rssi) {
    if (rssi >= -60) return "GREAT";
    if (rssi >= -70) return "GOOD";
    if (rssi >= -80) return "FAIR";
    return "WEAK";
}

void OledDisplay::task_entry(void* context) {
    static_cast<OledDisplay*>(context)->task();
}

void OledDisplay::task() {
    while (true) {
        const AudioMetrics metrics = audio_.metrics();
        const int rssi = wifi_.rssi();
        const uint32_t clients = lossless_.clients();
        clear();

        const char* state = !wifi_.connected()
                                ? "OFFLINE"
                                : clients > 0
                                      ? "STREAMING"
                                      : metrics.clock_locked
                                            ? "READY"
                                            : "WAITING";
        text(0, 0, std::string("CD > SONOS  ") + state);

        std::array<char, 24> line = {};
        if (wifi_.connected()) {
            std::snprintf(line.data(), line.size(), "WIFI %dDBM %s", rssi,
                          wifi_quality(rssi));
        } else {
            std::snprintf(line.data(), line.size(), "WIFI CONNECTING");
        }
        text(0, 1, line.data());
        text(0, 2, std::string("IP ") + wifi_.ip_address());
        text(0, 3, "PCM 44.1K 16B STEREO");

        std::snprintf(line.data(), line.size(), "CLOCK %s  SIGNAL %s",
                      metrics.clock_locked ? "YES" : "NO",
                      metrics.signal_present ? "YES" : "NO");
        text(0, 4, line.data());

        std::snprintf(line.data(), line.size(), "L%3.0F", 
                      metrics.peak_left_dbfs);
        text(0, 5, line.data());
        level_bar(30, 47, 96, metrics.peak_left_dbfs);
        std::snprintf(line.data(), line.size(), "R%3.0F",
                      metrics.peak_right_dbfs);
        text(0, 6, line.data());
        level_bar(30, 55, 96, metrics.peak_right_dbfs);

        std::snprintf(line.data(), line.size(), "FLAC CLIENTS %lu  %s",
                      static_cast<unsigned long>(clients),
                      metrics.emphasis ? "EMPH" : "");
        text(0, 7, line.data());
        flush();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

}  // namespace cdts
