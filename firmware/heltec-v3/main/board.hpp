#pragma once

#include "driver/gpio.h"

namespace cdts::board {

// Buffered NAD I2S outputs from the 74LVC245.
inline constexpr gpio_num_t kI2sBclk = GPIO_NUM_4;
inline constexpr gpio_num_t kI2sLrclk = GPIO_NUM_5;
inline constexpr gpio_num_t kI2sData = GPIO_NUM_6;
inline constexpr gpio_num_t kEmphasis = GPIO_NUM_7;

// On-board 128x64 SSD1306 OLED.
inline constexpr gpio_num_t kOledSda = GPIO_NUM_17;
inline constexpr gpio_num_t kOledScl = GPIO_NUM_18;
inline constexpr gpio_num_t kOledReset = GPIO_NUM_21;
inline constexpr gpio_num_t kVextEnable = GPIO_NUM_36;

// Heltec WiFi LoRa 32 V3 SX1262 connections. These are never allocated to
// another peripheral. radio_guard.cpp asserts reset before Wi-Fi starts.
inline constexpr gpio_num_t kLoraNss = GPIO_NUM_8;
inline constexpr gpio_num_t kLoraSck = GPIO_NUM_9;
inline constexpr gpio_num_t kLoraMosi = GPIO_NUM_10;
inline constexpr gpio_num_t kLoraMiso = GPIO_NUM_11;
inline constexpr gpio_num_t kLoraReset = GPIO_NUM_12;
inline constexpr gpio_num_t kLoraBusy = GPIO_NUM_13;
inline constexpr gpio_num_t kLoraDio1 = GPIO_NUM_14;

inline constexpr uint32_t kSampleRate = 44100;
inline constexpr uint8_t kChannels = 2;
inline constexpr uint8_t kBitsPerSample = 16;
inline constexpr uint8_t kSlotBits = 24;
inline constexpr uint16_t kFlacBlockSamples = 1024;

}  // namespace cdts::board
