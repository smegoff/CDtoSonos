#include "flac_encoder.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace cdts {
namespace {

class BitWriter {
public:
    BitWriter(uint8_t* output, size_t capacity)
        : output_(output), capacity_(capacity) {}

    bool write(uint32_t value, uint8_t bits) {
        for (int bit = bits - 1; bit >= 0; --bit) {
            if (bytes_used() >= capacity_) return false;
            if (((value >> bit) & 1U) != 0) {
                output_[bit_position_ / 8] |=
                    static_cast<uint8_t>(0x80U >> (bit_position_ % 8));
            }
            ++bit_position_;
        }
        return true;
    }

    bool unary(uint32_t zeros) {
        if (static_cast<uint64_t>(bit_position_) + zeros + 1 >
            static_cast<uint64_t>(capacity_) * 8) {
            return false;
        }
        bit_position_ += zeros;
        return write(1, 1);
    }

    void align() {
        bit_position_ = (bit_position_ + 7U) & ~size_t{7U};
    }

    size_t bytes_used() const {
        return (bit_position_ + 7U) / 8U;
    }

private:
    uint8_t* output_;
    size_t capacity_;
    size_t bit_position_ = 0;
};

int32_t sample_at(const int16_t* interleaved,
                  size_t frame,
                  size_t channel) {
    return interleaved[(frame * 2) + channel];
}

int32_t residual_at(const int16_t* interleaved,
                    size_t frame,
                    size_t channel,
                    uint8_t order) {
    const int32_t current = sample_at(interleaved, frame, channel);
    if (order == 0) return current;
    const int32_t previous = sample_at(interleaved, frame - 1, channel);
    if (order == 1) return current - previous;
    const int32_t previous2 = sample_at(interleaved, frame - 2, channel);
    return current - (2 * previous) + previous2;
}

uint32_t fold_signed(int32_t value) {
    return value >= 0 ? static_cast<uint32_t>(value) * 2U
                      : static_cast<uint32_t>(-static_cast<int64_t>(value) *
                                              2 - 1);
}

struct Predictor {
    uint8_t order = 0;
    uint8_t rice = 0;
    uint64_t bits = std::numeric_limits<uint64_t>::max();
};

Predictor choose_predictor(const int16_t* interleaved,
                           size_t frames,
                           size_t channel) {
    Predictor best;
    for (uint8_t order = 0; order <= 2 && order < frames; ++order) {
        const size_t residuals = frames - order;
        uint64_t folded_sum = 0;
        for (size_t frame = order; frame < frames; ++frame) {
            folded_sum += fold_signed(
                residual_at(interleaved, frame, channel, order));
        }

        // For a Laplacian residual distribution, log2(mean folded
        // magnitude) is a close Rice-parameter estimate. This avoids trying
        // all 15 parameters for every block on the ESP32-S3.
        const uint64_t mean = folded_sum / residuals;
        uint8_t rice = 0;
        while (rice < 14 && (uint64_t{1} << (rice + 1)) <= mean) {
            ++rice;
        }

        uint64_t bits = 8 + (static_cast<uint64_t>(order) * 16) + 10 +
                        (static_cast<uint64_t>(residuals) * (rice + 1));
        for (size_t frame = order; frame < frames; ++frame) {
            const uint32_t folded =
                fold_signed(residual_at(interleaved, frame, channel, order));
            bits += folded >> rice;
        }
        if (bits < best.bits) {
            best = {.order = order, .rice = rice, .bits = bits};
        }
    }
    return best;
}

bool write_subframe(BitWriter& writer,
                    const int16_t* interleaved,
                    size_t frames,
                    size_t channel) {
    const Predictor predictor = choose_predictor(interleaved, frames, channel);
    const uint64_t verbatim_bits = 8 + (static_cast<uint64_t>(frames) * 16);
    if (predictor.bits >= verbatim_bits) {
        if (!writer.write(0x02, 8)) return false;
        for (size_t frame = 0; frame < frames; ++frame) {
            if (!writer.write(
                    static_cast<uint16_t>(
                        sample_at(interleaved, frame, channel)),
                    16)) {
                return false;
            }
        }
        return true;
    }

    // Zero pad, FIXED subframe type (001xxx), predictor order, no wasted bits.
    if (!writer.write(static_cast<uint8_t>(0x10U | (predictor.order << 1)),
                      8)) {
        return false;
    }
    for (size_t frame = 0; frame < predictor.order; ++frame) {
        if (!writer.write(
                static_cast<uint16_t>(sample_at(interleaved, frame, channel)),
                16)) {
            return false;
        }
    }

    // Rice residual coding method 0, one partition, selected 4-bit parameter.
    if (!writer.write(0, 2) || !writer.write(0, 4) ||
        !writer.write(predictor.rice, 4)) {
        return false;
    }
    for (size_t frame = predictor.order; frame < frames; ++frame) {
        const uint32_t folded =
            fold_signed(residual_at(interleaved, frame, channel,
                                    predictor.order));
        if (!writer.unary(folded >> predictor.rice) ||
            !writer.write(folded, predictor.rice)) {
            return false;
        }
    }
    return true;
}

}  // namespace

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

    // Fixed predictors with Rice-coded residuals remain bit-perfect while
    // materially reducing Wi-Fi traffic. Incompressible blocks fall back to
    // verbatim subframes.
    const size_t prefix_size = static_cast<size_t>(p - output);
    std::memset(p, 0, capacity - prefix_size);
    BitWriter writer(p, capacity - prefix_size - 2);
    for (size_t channel = 0; channel < 2; ++channel) {
        if (!write_subframe(writer, interleaved, sample_frames, channel)) {
            return 0;
        }
    }
    writer.align();
    p += writer.bytes_used();

    const uint16_t checksum = crc16(output, static_cast<size_t>(p - output));
    write_be16(p, checksum);
    p += 2;
    return static_cast<size_t>(p - output);
}

}  // namespace cdts
