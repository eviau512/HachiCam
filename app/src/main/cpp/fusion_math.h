#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace fusion {
using Color = std::array<float, 3>;

inline float linear(float value) {
    const float s = std::clamp(value / 255.0f, 0.0f, 1.0f);
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

inline float encoded(float value) {
    const float s = std::clamp(value, 0.0f, 1.0f);
    return 255.0f * (s <= 0.0031308f ? 12.92f * s : 1.055f * std::pow(s, 1.0f / 2.4f) - 0.055f);
}

// Source pixel centres map to scale * (x + 0.5) - 0.5 on the output grid.
inline double sourceCoordinate(int output, int scale) {
    return (output + 0.5) / scale - 0.5;
}

// Compact point-sample kernel for drizzle reconstruction, in source pixels.
// Unlike resizing each frame first, distinct sampling phases retain their values.
inline float sampleWeight(double dx, double dy) {
    return std::max(0.0, 1.0 - std::abs(dx) / 0.5) *
           std::max(0.0, 1.0 - std::abs(dy) / 0.5);
}

inline bool phasesCover2D(const std::vector<std::array<double, 2>>& phases) {
    for (size_t i = 0; i < phases.size(); ++i)
        for (size_t j = i + 1; j < phases.size(); ++j)
            if (std::abs(phases[i][0] * phases[j][1] - phases[i][1] * phases[j][0]) >= .04)
                return true;
    return false;
}

inline Color recoverHighlight(const Color& base, const Color& shorter, float gain) {
    const float peak = *std::max_element(base.begin(), base.end());
    const float shortPeak = *std::max_element(shorter.begin(), shorter.end());
    if (peak <= 230.0f || shortPeak < 3.0f || shortPeak >= 246.0f || gain <= 1.1f)
        return base;
    Color radiance;
    for (int c = 0; c < 3; ++c) radiance[c] = linear(shorter[c]) * gain;
    const float maxRadiance = *std::max_element(radiance.begin(), radiance.end());
    // Continuous shoulder with unit slope at the knee, asymptotic ceiling < 255.
    constexpr float knee = 0.60f, ceiling = 0.92f;
    if (maxRadiance < knee) return base;
    const float excess = std::max(0.0f, maxRadiance - knee);
    const float target = maxRadiance <= knee ? maxRadiance :
        knee + (ceiling - knee) * excess / (excess + ceiling - knee);
    const float scale = target / std::max(maxRadiance, 1e-6f);
    const float u = std::clamp((peak - 230.0f) / 20.0f, 0.0f, 1.0f);
    const float weight = u * u * (3.0f - 2.0f * u);
    Color out;
    for (int c = 0; c < 3; ++c)
        out[c] = base[c] * (1.0f - weight) + encoded(radiance[c] * scale) * weight;
    return out;
}
} // namespace fusion
