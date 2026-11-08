#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cdts {

using EqGains = std::array<int8_t, 10>;
inline constexpr int kEqGainLimit = 6;
inline constexpr std::array<float, 10> kEqFrequencies = {
    31.5F, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

enum class EqPreset { Flat, Warm, BassBoost, Vocal, Bright, Custom };
inline constexpr std::array<const char*, 6> kEqPresetNames = {
    "flat", "warm", "bass-boost", "vocal", "bright", "custom"};
inline constexpr std::array<EqGains, 5> kEqPresetGains = {{
    {},
    {1, 2, 2, 1, 0, 0, -1, -1, -2, -2},
    {4, 5, 3, 1, 0, 0, 0, 0, 0, 0},
    {-2, -2, -1, 0, 1, 2, 3, 2, 0, -1},
    {0, 0, 0, -1, -1, 0, 1, 2, 3, 3},
}};

struct EqBiquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

struct EqConfig {
    EqPreset preset = EqPreset::Flat;
    EqGains gains = {};
    std::array<EqBiquad, kEqFrequencies.size()> filters = {};
    size_t count = 0;
    float headroom_db = 0;
    float preamp = 1;

    static EqConfig make(EqPreset preset, const EqGains& gains, float rate) {
        constexpr float kPi = 3.14159265358979323846F;
        EqConfig result;
        result.preset = preset;
        if (preset == EqPreset::Flat) return result;
        result.gains = gains;
        for (size_t i = 0; i < gains.size(); ++i) {
            if (gains[i] == 0) continue;
            const float w = 2 * kPi * kEqFrequencies[i] / rate;
            const float alpha = std::sin(w) / (2 * 1.41421356F);
            const float a = std::pow(10.0F, gains[i] / 40.0F);
            const float inv_a0 = 1 / (1 + alpha / a);
            const float b1 = -2 * std::cos(w) * inv_a0;
            result.filters[result.count++] = {
                (1 + alpha * a) * inv_a0, b1, (1 - alpha * a) * inv_a0,
                b1, (1 - alpha / a) * inv_a0};
        }
        if (result.count == 0) return result;

        // Estimate peak boost over the entire cascade, including overlapping bands.
        // Reserve another 1 dB for transients; output saturation is counted separately.
        double peak_power = 1;
        for (int i = 0; i <= 192; ++i) {
            const double f = 10 * std::pow(rate / 20.0, i / 192.0);
            const double w = 2 * static_cast<double>(kPi) * f / rate;
            const double c1 = std::cos(w), s1 = std::sin(w);
            const double c2 = std::cos(2 * w), s2 = std::sin(2 * w);
            double power = 1;
            for (size_t j = 0; j < result.count; ++j) {
                const auto& b = result.filters[j];
                const double nr = b.b0 + b.b1 * c1 + b.b2 * c2;
                const double ni = b.b1 * s1 + b.b2 * s2;
                const double dr = 1 + b.a1 * c1 + b.a2 * c2;
                const double di = b.a1 * s1 + b.a2 * s2;
                power *= (nr * nr + ni * ni) / (dr * dr + di * di);
            }
            peak_power = std::max(peak_power, power);
        }
        result.headroom_db = static_cast<float>(10 * std::log10(peak_power) + 1);
        result.preamp = std::pow(10.0F, -result.headroom_db / 20);
        return result;
    }
};

class GraphicEq {
public:
    void configure(const EqConfig& config) {
        if (config.gains == current_.config.gains) return;
        previous_ = current_;
        current_ = {};
        current_.config = config;
        transitioning_ = true;
    }

    void reset_history() {
        current_.delay = {};
        transitioning_ = false;
    }

    uint32_t process(int16_t* stereo, size_t frames) {
        if (frames == 0 || (current_.config.count == 0 && !transitioning_)) return 0;
        uint32_t clipped = 0;
        const float step = transitioning_ ? 1.0F / static_cast<float>(frames) : 0;
        for (size_t i = 0; i < frames; ++i) {
            const float mix = step * static_cast<float>(i + 1);
            for (size_t channel = 0; channel < 2; ++channel) {
                int16_t& sample = stereo[i * 2 + channel];
                float out = current_.process(sample, channel);
                if (transitioning_) {
                    const float old = previous_.process(sample, channel);
                    out = old + (out - old) * mix;
                }
                if (out > 32767 || out < -32768) ++clipped;
                const float bounded = std::clamp(out, -32768.0F, 32767.0F);
                sample = static_cast<int16_t>(bounded + (bounded < 0 ? -0.5F : 0.5F));
            }
        }
        transitioning_ = false;
        return clipped;
    }

private:
    struct Bank {
        EqConfig config;
        std::array<std::array<std::array<float, 2>, kEqFrequencies.size()>, 2> delay = {};

        float process(float sample, size_t channel) {
            sample *= config.preamp;
            for (size_t i = 0; i < config.count; ++i) {
                const auto& b = config.filters[i];
                auto& z = delay[channel][i];
                const float out = b.b0 * sample + z[0];
                z[0] = b.b1 * sample - b.a1 * out + z[1];
                z[1] = b.b2 * sample - b.a2 * out;
                sample = out;
            }
            return sample;
        }
    };

    Bank current_;
    Bank previous_;
    bool transitioning_ = false;
};

}  // namespace cdts
