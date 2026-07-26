#pragma once

#include <cstddef>
#include <cstdint>

namespace cdts {

class FlacEncoder {
public:
    static constexpr size_t kStreamHeaderBytes = 42;
    static constexpr size_t kMaxFrameBytes = 4144;

    FlacEncoder(uint32_t sample_rate, uint16_t block_samples);

    size_t stream_header(uint8_t* output, size_t capacity) const;
    size_t encode_stereo_16(const int16_t* interleaved,
                            size_t sample_frames,
                            uint32_t frame_number,
                            uint8_t* output,
                            size_t capacity) const;

private:
    static uint8_t crc8(const uint8_t* data, size_t size);
    static uint16_t crc16(const uint8_t* data, size_t size);
    static size_t write_utf8_uint(uint32_t value, uint8_t* output);
    static void write_be16(uint8_t* output, uint16_t value);

    uint32_t sample_rate_;
    uint16_t block_samples_;
};

}  // namespace cdts

