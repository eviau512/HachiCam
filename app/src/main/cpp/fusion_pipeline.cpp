#include "burst_fusion.h"
#include "fusion_math.h"
#include "local_registration.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HachiCam-Fusion", __VA_ARGS__)

namespace {
struct Plate {
    cv::Mat image;
    fusion::LocalRegistration local;
    cv::Matx33d inverse;
    float gain = 1.0f;
};

cv::Point2d project(const cv::Matx33d& h, double x, double y) {
    const cv::Vec3d p = h * cv::Vec3d(x, y, 1.0);
    if (!std::isfinite(p[2]) || std::abs(p[2]) < 1e-9) return {-1, -1};
    return {p[0] / p[2], p[1] / p[2]};
}

bool sample(const cv::Mat& image, double x, double y, fusion::Color& color) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
        x > image.cols - 1 || y > image.rows - 1) return false;
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, image.cols - 1), y1 = std::min(y0 + 1, image.rows - 1);
    const float fx = x - x0, fy = y - y0;
    for (int c = 0; c < 3; ++c) {
        color[c] = (1 - fy) * ((1 - fx) * image.at<cv::Vec3b>(y0, x0)[c] + fx * image.at<cv::Vec3b>(y0, x1)[c]) +
                         fy * ((1 - fx) * image.at<cv::Vec3b>(y1, x0)[c] + fx * image.at<cv::Vec3b>(y1, x1)[c]);
    }
    return true;
}

Plate prepare(const cv::Mat& image, const cv::Mat& h) {
    cv::Mat hd; h.convertTo(hd, CV_64F);
    cv::Mat inverse = hd.inv();
    cv::Matx33d matrix;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) matrix(y, x) = inverse.at<double>(y, x);
    return {image, {}, matrix, 1.0f};
}

bool rectangleSupported(const Plate& plate, const cv::Rect& crop) {
    const double offset = plate.local.maxOffset;
    for (double y : {double(crop.y), crop.y + (crop.height - 1) * .5, double(crop.y + crop.height - 1)})
        for (double x : {double(crop.x), crop.x + (crop.width - 1) * .5, double(crop.x + crop.width - 1)}) {
            // Expand in reference space before projection; source-space offsets
            // would incorrectly assume a unit homography Jacobian.
            for (double dy : {-offset, offset}) for (double dx : {-offset, offset}) {
                const auto p = project(plate.inverse, x + dx, y + dy);
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 2 || p.y < 2 ||
                    p.x > plate.image.cols - 3 || p.y > plate.image.rows - 3) return false;
            }
        }
    return true;
}

bool cropForPlate(const Plate& plate, cv::Rect& crop) {
    auto candidate = crop;
    while (!rectangleSupported(plate, candidate)) {
        candidate = cv::Rect(candidate.x + 1, candidate.y + 1, candidate.width - 2, candidate.height - 2);
        if (candidate.width < 32 || candidate.height < 32 ||
            candidate.x > plate.image.cols * .05 || candidate.y > plate.image.rows * .05 ||
            plate.image.cols - candidate.br().x > plate.image.cols * .05 ||
            plate.image.rows - candidate.br().y > plate.image.rows * .05) return false;
    }
    crop = candidate;
    return true;
}

// Estimate the effective JPEG response from shared, unclipped scene samples.
// Camera JPEGs are already ISP tone-mapped: exposure metadata alone is not a
// calibrated radiometric response. Use the measured ratio when well supported.
float responseGain(const cv::Mat& base, const cv::Mat& aligned, double fallback) {
    std::vector<float> ratios;
    const int step = std::max(1, std::max(base.rows, base.cols) / 100);
    for (int y = 0; y < base.rows; y += step) {
        for (int x = 0; x < base.cols; x += step) {
            const auto b = base.at<cv::Vec3b>(y, x);
            const auto s = aligned.at<cv::Vec3b>(y, x);
            const int bmax = std::max({b[0], b[1], b[2]}), smax = std::max({s[0], s[1], s[2]});
            if (bmax < 40 || bmax > 220 || smax < 12 || smax > 220) continue;
            const float bl = 0.114f * fusion::linear(b[0]) + 0.587f * fusion::linear(b[1]) + 0.299f * fusion::linear(b[2]);
            const float sl = 0.114f * fusion::linear(s[0]) + 0.587f * fusion::linear(s[1]) + 0.299f * fusion::linear(s[2]);
            if (sl > 1e-5f) ratios.push_back(bl / sl);
        }
    }
    if (ratios.size() >= 64) {
        const auto mid = ratios.begin() + ratios.size() / 2;
        std::nth_element(ratios.begin(), mid, ratios.end());
        return std::clamp(*mid, 1.0f, 256.0f);
    }
    return std::clamp(static_cast<float>(fallback), 1.0f, 256.0f);
}
} // namespace

cv::Mat BurstFusionEngine::fuseBurstFrames(const std::vector<cv::Mat>& frames,
        bool removeGlare, bool isScreenMode, bool superResolution,
        const std::vector<int>& tiers, const std::vector<double>& exposureProducts, Report* report) {
    if (frames.empty()) return {};
    CV_Assert(tiers.empty() || tiers.size() == frames.size());
    CV_Assert(exposureProducts.empty() || exposureProducts.size() == frames.size());
    size_t reference = 0;
    double bestQuality = -1;
    for (size_t i = 0; i < frames.size(); ++i) {
        if ((!tiers.empty() && tiers[i] != 0) || frames[i].empty() || frames[i].type() != CV_8UC3 || frames[i].size() != frames.front().size()) continue;
        double quality = fusion::referenceQuality(frames[i]);
        if (!exposureProducts.empty() && exposureProducts[i] > 0 && exposureProducts[0] > 0)
            quality /= 1.0 + std::max(0.0, std::abs(std::log(exposureProducts[i] / exposureProducts[0])) - .15);
        LOGI("Reference candidate %zu quality=%.4f", i, quality);
        if (quality > bestQuality * 1.000001) { bestQuality = quality; reference = i; }
    }
    const auto& ref = frames[reference];
    CV_Assert(!ref.empty() && ref.type() == CV_8UC3);
    CV_Assert(tiers.empty() || tiers.size() == frames.size());
    CV_Assert(exposureProducts.empty() || exposureProducts.size() == frames.size());
    CV_Assert(tiers.empty() || tiers.front() == 0);

    std::vector<Plate> anchors{prepare(ref, cv::Mat::eye(3, 3, CV_64F))};
    std::vector<Plate> highlights;
    cv::Mat refGray;
    cv::cvtColor(ref, refGray, cv::COLOR_BGR2GRAY);
    cv::Rect commonCrop(0, 0, ref.cols, ref.rows);
    cv::Matx33d firstToReference = cv::Matx33d::eye();
    bool geometryKnown = reference == 0;
    std::vector<std::array<double, 2>> phases;
    double reliableCoverage = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (i == reference) continue;
        if (frames[i].empty() || frames[i].size() != ref.size() || frames[i].type() != ref.type()) continue;
        const int tier = tiers.empty() ? 0 : tiers[i];
        cv::Mat warped, h;
        bool aligned;
        if (tier == 0) {
            aligned = alignFrameECC(frames[i], ref, warped, h) ||
                      alignFrameHomography(frames[i], ref, warped, h) ||
                      alignGradientPhaseCorrelation(frames[i], ref, warped, h);
        } else {
            aligned = alignHighlightFrame(frames[i], ref, warped, h);
        }
        if (!aligned || warped.empty() || h.empty() || !cv::checkRange(h) || std::abs(cv::determinant(h)) < 1e-9) {
            LOGI("Frame %zu tier=%d rejected: alignment failed", i, tier);
            continue;
        }
        Plate plate = prepare(frames[i], h);
        if (i == 0) {
            cv::Mat hd; h.convertTo(hd, CV_64F);
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) firstToReference(r, c) = hd.at<double>(r, c);
            geometryKnown = true;
        }
        cv::Mat gray;
        cv::cvtColor(warped, gray, cv::COLOR_BGR2GRAY);
        plate.local = fusion::registerLocal(refGray, gray, tier != 0);
        LOGI("Frame %zu tier=%d localSupport=%.3f detailNodes=%.3f maxRefinement=%.3fpx", i, tier, plate.local.supportedFraction, plate.local.detailFraction, plate.local.maxOffset);
        if (tier == 0) {
            if (plate.local.supportedFraction < .20f) {
                LOGI("Frame %zu rejected: insufficient local support", i); continue;
            }
            auto crop = commonCrop;
            if (!cropForPlate(plate, crop)) {
                LOGI("Frame %zu rejected: source border loss exceeds 5 percent per edge", i); continue;
            }
            commonCrop = crop;
            const auto centre = project(plate.inverse, ref.cols * .5, ref.rows * .5);
            const double dx = centre.x - ref.cols * .5, dy = centre.y - ref.rows * .5;
            if (plate.local.detailFraction >= .15f) phases.push_back({dx - std::round(dx), dy - std::round(dy)});
            reliableCoverage += plate.local.supportedFraction;
            anchors.push_back(std::move(plate));
        } else if (isScreenMode && !exposureProducts.empty() && exposureProducts[i] > 0) {
            const double gain = exposureProducts[reference] / exposureProducts[i];
            // Reject an ignored manual exposure request rather than invent HDR.
            if (gain <= 1.25) continue;
            plate.gain = responseGain(ref, warped, gain);
            if (plate.gain > 1.1f && plate.local.supportedFraction >= .05f) highlights.push_back(std::move(plate));
        }
    }
    // Prefer the least underexposed valid plate (better SNR); use the deeper
    // bracket only where the brighter plate is still clipped.
    std::stable_sort(highlights.begin(), highlights.end(), [](const Plate& a, const Plate& b) { return a.gain < b.gain; });
    const bool diversePhases = anchors.size() >= 3 && fusion::phasesCover2D(phases) &&
        reliableCoverage / std::max(size_t(1), anchors.size() - 1) >= .50;
    const int scale = superResolution && diversePhases && ref.total() <= 16000000 ? 2 : 1;
    if (report) {
        report->anchors = int(anchors.size()); report->highlights = int(highlights.size()); report->scale = scale;
        report->reference = int(reference); report->crop = commonCrop; report->sourceSize = ref.size();
        report->firstToReference = firstToReference; report->geometryKnown = geometryKnown;
    }
    LOGI("Fusion: reference=%zu anchors=%zu highlights=%zu phases2D=%d scale=%d crop=%d,%d,%d,%d source=%dx%d; glare flag=%d",
         reference, anchors.size(), highlights.size(), diversePhases, scale,
         commonCrop.x, commonCrop.y, commonCrop.width, commonCrop.height, ref.cols, ref.rows, removeGlare);
    cv::Mat result(commonCrop.height * scale, commonCrop.width * scale, CV_8UC3);
    // Only the 8-bit result is allocated at output resolution. No 50MP float
    // accumulation, gradient pyramids or per-channel temporaries are retained.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < result.rows; ++y) {
        auto* out = result.ptr<cv::Vec3b>(y);
        const double ry = commonCrop.y + fusion::sourceCoordinate(y, scale);
        for (int x = 0; x < result.cols; ++x) {
            const double rx = commonCrop.x + fusion::sourceCoordinate(x, scale);
            fusion::Color base;
            sample(ref, std::clamp(rx, 0.0, double(ref.cols - 1)), std::clamp(ry, 0.0, double(ref.rows - 1)), base);
            fusion::Color sum{};
            float weights = 0;
            const int gx = std::clamp(int(std::round(rx)), 0, ref.cols - 1);
            const int gy = std::clamp(int(std::round(ry)), 0, ref.rows - 1);
            for (const auto& plate : anchors) {
                if (!plate.local.gate.empty() && !plate.local.gate.at<uchar>(gy, gx)) continue;
                cv::Point2d delta; float confidence;
                if (!plate.local.offset(rx, ry, delta, confidence)) continue;
                const auto p = project(plate.inverse, rx + delta.x, ry + delta.y);
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
                const int sx = int(std::round(p.x)), sy = int(std::round(p.y));
                if (sx < 0 || sy < 0 || sx >= ref.cols || sy >= ref.rows) continue;
                const float w = confidence * (scale == 2 ? fusion::sampleWeight(p.x - sx, p.y - sy) : 1.0f);
                if (w <= 0) continue;
                fusion::Color color;
                if (scale == 2) {
                    const auto pixel = plate.image.at<cv::Vec3b>(sy, sx);
                    for (int c = 0; c < 3; ++c) color[c] = pixel[c];
                } else if (!sample(plate.image, p.x, p.y, color)) continue;
                for (int c = 0; c < 3; ++c) sum[c] += color[c] * w;
                weights += w;
            }
            if (weights > 1e-5f)
                for (int c = 0; c < 3; ++c) base[c] = sum[c] / weights;
            if (*std::max_element(base.begin(), base.end()) > 230) {
                for (const auto& plate : highlights) {
                    if (!plate.local.gate.at<uchar>(gy, gx)) continue;
                    cv::Point2d delta; float confidence;
                    if (!plate.local.offset(rx, ry, delta, confidence)) continue;
                    const auto p = project(plate.inverse, rx + delta.x, ry + delta.y);
                    fusion::Color color;
                    if (!sample(plate.image, p.x, p.y, color)) continue;
                    const float peak = *std::max_element(color.begin(), color.end());
                    if (peak < 3 || peak >= 246 || fusion::linear(peak) * plate.gain < 0.60f) continue;
                    base = fusion::recoverHighlight(base, color, plate.gain);
                    break;
                }
            }
            for (int c = 0; c < 3; ++c) out[x][c] = cv::saturate_cast<uchar>(base[c]);
        }
    }
    return result;
}
