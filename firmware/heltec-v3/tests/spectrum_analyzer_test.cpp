// Portable test: c++ -std=c++17 -O2 spectrum_analyzer_test.cpp -o spectrum_test
#include "../main/spectrum_analyzer.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr size_t kFrames = 1024;
constexpr uint32_t kRate = 44100;
constexpr double kPi = 3.14159265358979323846;
using Samples = std::array<int16_t, kFrames * 2>;

void require(bool passed, const char* message) {
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void tone(Samples& samples, size_t first_frame, float frequency,
          float right_gain = 1.0F) {
    for (size_t frame = 0; frame < kFrames; ++frame) {
        const double phase = 2.0 * kPi * frequency * (first_frame + frame) / kRate;
        const int16_t value = static_cast<int16_t>(std::lround(16384 * std::sin(phase)));
        samples[2 * frame] = value;
        samples[2 * frame + 1] = static_cast<int16_t>(value * right_gain);
    }
}

void assert_silence(const cdts::SpectrumAnalyzer& analyzer) {
    for (float value : analyzer.levels_dbfs()) {
        require(std::isfinite(value) && value == -96.0F,
                "silence/reset must produce finite floor values");
    }
}
}

int main() {
    static_assert(cdts::SpectrumAnalyzer::kBandCount == 31);
    static_assert(sizeof(cdts::SpectrumAnalyzer) < 1200,
                  "filter state must remain small on no-PSRAM devices");
    cdts::SpectrumAnalyzer in_phase(kRate, kFrames);
    cdts::SpectrumAnalyzer anti_phase(kRate, kFrames);
    cdts::SpectrumAnalyzer left_only(kRate, kFrames);
    Samples input = {};
    assert_silence(in_phase);
    for (size_t block = 0; block < 44; ++block) {
        tone(input, block * kFrames, 1000.0F);
        in_phase.process(input.data(), kFrames);
        tone(input, block * kFrames, 1000.0F, -1.0F);
        anti_phase.process(input.data(), kFrames);
        tone(input, block * kFrames, 1000.0F, 0.0F);
        left_only.process(input.data(), kFrames);
    }
    const auto stereo = in_phase.levels_dbfs();
    const auto inverted = anti_phase.levels_dbfs();
    const auto mono = left_only.levels_dbfs();
    constexpr size_t kOneKhzBand = 17;
    require(std::abs(stereo[kOneKhzBand] + 9.0309F) < 0.3F,
            "half-scale sine must have approximately -9.03 dBFS band RMS");
    require(std::abs(mono[kOneKhzBand] - stereo[kOneKhzBand] + 3.0103F) < 0.1F,
            "single-channel input must retain half the stereo energy");
    for (size_t band = 0; band < stereo.size(); ++band) {
        require(std::isfinite(stereo[band]) && std::isfinite(inverted[band]),
                "filter outputs must stay finite");
        require(std::abs(stereo[band] - inverted[band]) < 0.05F,
                "antiphase stereo must not cancel in the spectrum");
    }

    cdts::SpectrumAnalyzer between_bands(kRate, kFrames);
    for (size_t block = 0; block < 44; ++block) {
        tone(input, block * kFrames, 1100.0F);
        between_bands.process(input.data(), kFrames);
    }
    const auto spread = between_bands.levels_dbfs();
    require(std::max(spread[17], spread[18]) > -16.0F,
            "tone between band centers must retain visible band energy");

    // Low bands keep filter state between blocks and must also settle correctly.
    cdts::SpectrumAnalyzer low_band(kRate, kFrames);
    for (size_t block = 0; block < 88; ++block) {
        tone(input, block * kFrames, 20.0F);
        low_band.process(input.data(), kFrames);
    }
    require(std::abs(low_band.levels_dbfs()[0] + 9.0309F) < 1.0F,
            "20 Hz band must remain stable across short PCM blocks");

    input.fill(0);
    for (size_t block = 0; block < 100; ++block) {
        in_phase.process(input.data(), kFrames);
    }
    assert_silence(in_phase);
    low_band.reset();
    assert_silence(low_band);
    std::puts("PASS: band gain, stereo phase, channel energy, off-center tones, low band, decay, reset");
}
