#include "fusion_math.h"
#include <iostream>
#include <stdexcept>

static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

int main() {
    using namespace fusion;
    const Color text{20, 35, 40};
    check(recoverHighlight(text, {5, 8, 10}, 16) == text, "Normal text must be unchanged");
    const Color white{255, 255, 255};
    check(recoverHighlight(white, {255, 255, 255}, 1) == white, "Ignored exposure must not invent highlight data");
    check(recoverHighlight(white, {0, 0, 0}, 64) == white, "Black/no-signal sample must not darken highlights");
    const auto low = recoverHighlight(white, {80, 80, 80}, 8);
    const auto high = recoverHighlight(white, {150, 150, 150}, 8);
    check(low[0] > 100 && low[0] < high[0] && high[0] < 250, "Clipped bulb must recover monotonic interior contrast below clipping");
    const auto color = recoverHighlight(white, {50, 100, 180}, 8);
    check(color[0] < color[1] && color[1] < color[2], "Clipped reference must recover the shorter exposure color");
    for (int v = 0; v <= 255; ++v) check(std::abs(encoded(linear(float(v))) - v) < .001f, "sRGB round trip");
    check(sourceCoordinate(0, 2) == -.25 && sourceCoordinate(1, 2) == .25, "Pixel-centre mapping");
    check(sampleWeight(.25, .25) > 0 && sampleWeight(.75, .25) == 0, "Distinct sensor phases must use compact support");

    // A 0.7 cycle/source-pixel signal aliases in one frame, but is observable
    // with half-pixel-shifted measurements. Use the production sampling kernel.
    auto signal = [](double x) { return 128.0 + 80.0 * std::sin(2 * 3.141592653589793 * .7 * x); };
    double reconstructedError = 0, resizedError = 0;
    for (int x = 4; x < 120; ++x) {
        double p = sourceCoordinate(x, 2), total = 0, weight = 0;
        for (double offset : {0.0, .5}) {
            double sensor = std::round(p - offset);
            double w = sampleWeight(p - offset - sensor, 0);
            total += w * signal(sensor + offset); weight += w;
        }
        double lo = std::floor(p), alpha = p - lo;
        double resized = (1 - alpha) * signal(lo) + alpha * signal(lo + 1);
        reconstructedError += std::pow(total / weight - signal(p), 2);
        resizedError += std::pow(resized - signal(p), 2);
    }
    check(reconstructedError < resizedError * .6, "Multi-phase measurements must beat single-frame resizing on above-Nyquist detail");
    std::cout << "PASS: highlight recovery, color, clipping guards, pixel centres and phase reconstruction; error ratio="
              << reconstructedError / resizedError << '\n';
}
