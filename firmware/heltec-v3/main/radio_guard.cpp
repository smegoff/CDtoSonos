#include "radio_guard.hpp"

#include "board.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

namespace cdts {
namespace {
constexpr char kTag[] = "radio_guard";
}

void disable_lora_radio() {
    static_assert(CDTS_LORA_ENABLED == 0,
                  "This firmware target must never enable the SX1262");

    const uint64_t output_mask =
        (1ULL << board::kLoraNss) |
        (1ULL << board::kLoraSck) |
        (1ULL << board::kLoraMosi) |
        (1ULL << board::kLoraReset);

    gpio_config_t outputs = {};
    outputs.pin_bit_mask = output_mask;
    outputs.mode = GPIO_MODE_OUTPUT;
    outputs.pull_up_en = GPIO_PULLUP_DISABLE;
    outputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    outputs.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&outputs));

    // NSS high prevents SPI selection. Clock and MOSI are held low. Reset is
    // active-low and remains asserted for the lifetime of the firmware.
    ESP_ERROR_CHECK(gpio_set_level(board::kLoraNss, 1));
    ESP_ERROR_CHECK(gpio_set_level(board::kLoraSck, 0));
    ESP_ERROR_CHECK(gpio_set_level(board::kLoraMosi, 0));
    ESP_ERROR_CHECK(gpio_set_level(board::kLoraReset, 0));

    const uint64_t input_mask =
        (1ULL << board::kLoraMiso) |
        (1ULL << board::kLoraBusy) |
        (1ULL << board::kLoraDio1);

    gpio_config_t inputs = {};
    inputs.pin_bit_mask = input_mask;
    inputs.mode = GPIO_MODE_INPUT;
    inputs.pull_up_en = GPIO_PULLUP_DISABLE;
    inputs.pull_down_en = GPIO_PULLDOWN_ENABLE;
    inputs.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&inputs));

    ESP_LOGI(kTag, "SX1262 disabled: reset asserted, NSS high, SPI idle");
}

}  // namespace cdts

