#include "flac_encoder.hpp"

#include <algorithm>
#include <cstring>

namespace cdts {

FlacEncoder::FlacEncoder(uint32_t sample_rate, uint16_t block_samples)
    : sample_rate_(sample_rate), block_samples_(block_samples) {}

void FlacEncoder::write_be16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value >> 8);
    output[1] = static_cast<uint8_t>(value);
}

size_t FlacEncoder::stream_header(uint8_t* output, size_t capacity) const {
    if (capacity < kStreamHeaderBytes) {
        return 0;
    }

    uint8_t* p = output;
    *p++ = 'f';
    *p++ = 'L';
    *p++ = 'a';
    *p++ = 'C';

    // Last metadata block + STREAMINFO, followed by its fixed 34-byte length.
    *p++ = 0x80;
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 34;

    write_be16(p, block_samples_);
    p += 2;
    write_be16(p, block_samples_);
    p += 2;

    // Unknown minimum/maximum frame sizes are valid for a live stream.
    std::memset(p, 0, 6);
    p += 6;

    // 20-bit sample rate, 3-bit channels-1, 5-bit bits-per-sample-1,
    // and an unknown 36-bit total sample count.
    const uint64_t packed =
        (static_cast<uint64_t>(sample_rate_) << 44) |
        (static_cast<uint64_t>(1) << 41) |
        (static_cast<uint64_t>(15) << 36);
    for (int shift = 56; shift >= 0; shift -= 8) {
        *p++ = static_cast<uint8_t>(packed >> shift);
    }

    // All-zero MD5 denotes an unavailable checksum for an unbounded stream.
    std::memset(p, 0, 16);
    p += 16;
    return static_cast<size_t>(p - output);
}

size_t FlacEncoder::write_utf8_uint(uint32_t value, uint8_t* output) {
    if (value < 0x80) {
        output[0] = static_cast<uint8_t>(value);
        return 1;
    }

    size_t bytes = 2;
    if (value >= 0x800) bytes = 3;
    if (value >= 0x10000) bytes = 4;
    if (value >= 0x200000) bytes = 5;
    if (value >= 0x4000000) bytes = 6;

    for (size_t index = bytes - 1; index > 0; --index) {
        output[index] = static_cast<uint8_t>(0x80 | (value & 0x3f));
        value >>= 6;
    }
    output[0] = static_cast<uint8_t>(
        ((0xffU << (8U - bytes)) & 0xffU) | value);
    return bytes;
}

uint8_t FlacEncoder::crc8(const uint8_t* data, size_t size) {
    uint8_t crc = 0;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<uint8_t>(
                (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1));
        }
    }
    return crc;
}

uint16_t FlacEncoder::crc16(const uint8_t* data, size_t size) {
    uint16_t crc = 0;
    for (size_t index = 0; index < size; ++index) {
        crc ^= static_cast<uint16_t>(data[index]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<uint16_t>(
                (crc & 0x8000) ? ((crc << 1) ^ 0x8005) : (crc << 1));
        }
    }
    return crc;
}

size_t FlacEncoder::encode_stereo_16(const int16_t* interleaved,
                                     size_t sample_frames,
                                     uint32_t frame_number,
                                     uint8_t* output,
                                     size_t capacity) const {
    if (interleaved == nullptr || output == nullptr || sample_frames == 0 ||
        sample_frames > 65536) {
        return 0;
    }

    // Allow for the largest fixed-block frame number encoding, both subframe
    // headers, the frame-header CRC and the frame CRC.
    const size_t required = 32 + (sample_frames * 4);
    if (capacity < required) {
        return 0;
    }

    uint8_t* p = output;
    *p++ = 0xff;
    *p++ = 0xf8;  // Sync, reserved=0, fixed-block stream.
    *p++ = 0x79;  // 16-bit block-size follows; sample rate is 44.1 kHz.
    *p++ = 0x18;  // Independent stereo, 16-bit samples, reserved=0.
    p += write_utf8_uint(frame_number, p);
    write_be16(p, static_cast<uint16_t>(sample_frames - 1));
    p += 2;
    *p = crc8(output, static_cast<size_t>(p - output));
    ++p;

    // Verbatim subframes retain every source bit and avoid a large encoder.
    for (size_t channel = 0; channel < 2; ++channel) {
        *p++ = 0x02;  // Zero pad, VERBATIM subframe type, no wasted bits.
        for (size_t frame = 0; frame < sample_frames; ++frame) {
            const uint16_t sample =
                static_cast<uint16_t>(interleaved[(frame * 2) + channel]);
            write_be16(p, sample);
            p += 2;
        }
    }

    const uint16_t checksum = crc16(output, static_cast<size_t>(p - output));
    write_be16(p, checksum);
    p += 2;
    return static_cast<size_t>(p - output);
}

}  // namespace cdts
