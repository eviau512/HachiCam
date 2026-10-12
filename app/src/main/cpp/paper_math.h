#pragma once
#include <algorithm>
#include <cmath>

namespace paper {
// L and background are 8-bit Lab luminance. No classification or hard-white plateau.
inline float luminance(float l, float background, float chroma, bool gentle) {
    const float paperSupport = std::clamp((background - 95.0f) / 90.0f, 0.0f, 1.0f);
    const float colorProtection = 1.0f - .55f * std::clamp(chroma / 48.0f, 0.0f, 1.0f);
    const float strength = (gentle ? .72f : .90f) * paperSupport * colorProtection;
    const float ratio = std::max(0.0f, l / std::max(background, 1.0f));
    // Extend above the paper point with a soft shoulder, retaining tonal order.
    const float white = gentle ? 245.0f : 248.0f;
    const float value = white * std::pow(std::min(ratio, 1.0f), gentle ? 1.10f : 1.24f);
    const float excess = std::max(0.0f, ratio - 1.0f) * white;
    const float corrected = value + (255.0f - white) * excess / (excess + 255.0f - white);
    return std::clamp(l * (1.0f - strength) + corrected * strength, 0.0f, 255.0f);
}
}
