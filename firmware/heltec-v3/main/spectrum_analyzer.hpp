#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cdts {

// Display-oriented, nominal third-octave band energy. The 31 constant-Q
// bandpasses integrate stereo energy instead of sampling isolated frequencies.
// No FFT window, dynamic allocation, or modification of the captured PCM.
class SpectrumAnalyzer {
public:
    static constexpr size_t kBandCount = 31;
    inline static constexpr std::array<float, kBandCount> kFrequencies = {
        20.0F, 25.0F, 31.5F, 40.0F, 50.0F, 63.0F, 80.0F, 100.0F,
        125.0F, 160.0F, 200.0F, 250.0F, 315.0F, 400.0F, 500.0F,
        630.0F, 800.0F, 1000.0F, 1250.0F, 1600.0F, 2000.0F,
        2500.0F, 3150.0F, 4000.0F, 5000.0F, 6300.0F, 8000.0F,
        10000.0F, 12500.0F, 16000.0F, 20000.0F};

    SpectrumAnalyzer(uint32_t sample_rate, size_t block_frames) {
        // Coefficients are calculated once. Sample processing is float only.
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kQ = 4.318;
        for (size_t index = 0; index < bands_.size(); ++index) {
            const double omega = 2.0 * kPi * kFrequencies[index] / sample_rate;
            const double alpha = std::sin(omega) / (2.0 * kQ);
            const double inverse_a0 = 1.0 / (1.0 + alpha);
            Band& band = bands_[index];
            // RBJ bandpass, 0 dB peak: b1=0 and b2=-b0.
            band.b0 = static_cast<float>(alpha * inverse_a0);
            band.a1 = static_cast<float>(-2.0 * std::cos(omega) * inverse_a0);
            band.a2 = static_cast<float>((1.0 - alpha) * inverse_a0);
        }
        // An approximately 60 ms power envelope follows each full input block.
        retention_ = std::exp(-static_cast<float>(block_frames) /
                              (static_cast<float>(sample_rate) * 0.060F));
        power_scale_ = 1.0F /
            (2.0F * static_cast<float>(block_frames) * 32768.0F * 32768.0F);
    }

    void reset() {
        for (Band& band : bands_) {
            band.left_z1 = band.left_z2 = 0.0F;
            band.right_z1 = band.right_z2 = 0.0F;
            band.power = 0.0F;
        }
    }

    // Call for every contiguous PCM block, using the constructor's block size.
    void process(const int16_t* interleaved, size_t frames) {
        for (Band& band : bands_) {
            float left_z1 = band.left_z1;
            float left_z2 = band.left_z2;
            float right_z1 = band.right_z1;
            float right_z2 = band.right_z2;
            const float b0 = band.b0;
            const float a1 = band.a1;
            const float a2 = band.a2;
            float energy = 0.0F;
            for (size_t frame = 0; frame < frames; ++frame) {
                const float left_drive = b0 * interleaved[frame * 2];
                const float right_drive = b0 * interleaved[frame * 2 + 1];
                const float left = left_drive + left_z1;
                const float right = right_drive + right_z1;
                left_z1 = left_z2 - a1 * left;
                right_z1 = right_z2 - a1 * right;
                left_z2 = -left_drive - a2 * left;
                right_z2 = -right_drive - a2 * right;
                // Separate channel energies preserve antiphase stereo audio.
                energy += left * left + right * right;
            }
            band.left_z1 = left_z1;
            band.left_z2 = left_z2;
            band.right_z1 = right_z1;
            band.right_z2 = right_z2;
            band.power = retention_ * band.power +
                         (1.0F - retention_) * energy * power_scale_;
            // Avoid tiny residual states after a long period of digital silence.
            if (band.power < 1.0e-30F) {
                band.left_z1 = band.left_z2 = 0.0F;
                band.right_z1 = band.right_z2 = 0.0F;
                band.power = 0.0F;
            }
        }
    }

    std::array<float, kBandCount> levels_dbfs() const {
        std::array<float, kBandCount> levels = {};
        for (size_t index = 0; index < bands_.size(); ++index) {
            const float power = bands_[index].power;
            levels[index] = power <= 1.0e-12F
                ? -96.0F : std::max(-96.0F, 10.0F * std::log10(power));
        }
        return levels;
    }

private:
    struct Band {
        float b0 = 0.0F;
        float a1 = 0.0F;
        float a2 = 0.0F;
        float left_z1 = 0.0F;
        float left_z2 = 0.0F;
        float right_z1 = 0.0F;
        float right_z2 = 0.0F;
        float power = 0.0F;
    };
    std::array<Band, kBandCount> bands_ = {};
    float retention_ = 0.0F;
    float power_scale_ = 0.0F;
};

}  // namespace cdts
