#include "burst_fusion.h"
#include "fusion_math.h"
#include "local_registration.h"
#include "regional_reconstruction.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HachiCam-Fusion", __VA_ARGS__)

namespace {
struct Plate {
    cv::Mat image;
    cv::Mat highlightNeighborhood;
    fusion::LocalRegistration local;
    cv::Matx33d inverse;
    float gain = 1.0f;
};

cv::Point2d project(const cv::Matx33d &h, double x, double y) {
    const cv::Vec3d p = h * cv::Vec3d(x, y, 1.0);
    if (!std::isfinite(p[2]) || std::abs(p[2]) < 1e-9)
        return {-1, -1};
    return {p[0] / p[2], p[1] / p[2]};
}

bool sample(const cv::Mat &image, double x, double y, fusion::Color &color) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > image.cols - 1 || y > image.rows - 1)
        return false;
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, image.cols - 1), y1 = std::min(y0 + 1, image.rows - 1);
    const float fx = x - x0, fy = y - y0;
    for (int c = 0; c < 3; ++c) {
        color[c] =
            (1 - fy) * ((1 - fx) * image.at<cv::Vec3b>(y0, x0)[c] + fx * image.at<cv::Vec3b>(y0, x1)[c]) +
            fy * ((1 - fx) * image.at<cv::Vec3b>(y1, x0)[c] + fx * image.at<cv::Vec3b>(y1, x1)[c]);
    }
    return true;
}

Plate prepare(const cv::Mat &image, const cv::Mat &h) {
    cv::Mat hd;
    h.convertTo(hd, CV_64F);
    cv::Mat inverse = hd.inv();
    cv::Matx33d matrix;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            matrix(y, x) = inverse.at<double>(y, x);
    Plate plate;
    plate.image = image;
    plate.inverse = matrix;
    return plate;
}

bool rectangleSupported(const Plate &plate, const cv::Rect &crop) {
    const double offset = plate.local.maxOffset;
    for (double y : {double(crop.y), crop.y + (crop.height - 1) * .5, double(crop.y + crop.height - 1)})
        for (double x : {double(crop.x), crop.x + (crop.width - 1) * .5, double(crop.x + crop.width - 1)}) {
            // Expand in reference space before projection; source-space offsets
            // would incorrectly assume a unit homography Jacobian.
            for (double dy : {-offset, offset})
                for (double dx : {-offset, offset}) {
                    const auto p = project(plate.inverse, x + dx, y + dy);
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 2 || p.y < 2 ||
                        p.x > plate.image.cols - 3 || p.y > plate.image.rows - 3)
                        return false;
                }
        }
    return true;
}

bool cropForPlate(const Plate &plate, cv::Rect &crop) {
    auto candidate = crop;
    while (!rectangleSupported(plate, candidate)) {
        candidate = cv::Rect(candidate.x + 1, candidate.y + 1, candidate.width - 2, candidate.height - 2);
        if (candidate.width < 32 || candidate.height < 32 || candidate.x > plate.image.cols * .05 ||
            candidate.y > plate.image.rows * .05 ||
            plate.image.cols - candidate.br().x > plate.image.cols * .05 ||
            plate.image.rows - candidate.br().y > plate.image.rows * .05)
            return false;
    }
    crop = candidate;
    return true;
}

} // namespace

cv::Mat BurstFusionEngine::fuseBurstFrames(const std::vector<cv::Mat> &frames, bool removeGlare,
                                           bool isScreenMode, bool superResolution,
                                           const std::vector<int> &tiers,
                                           const std::vector<double> &exposureProducts, Report *report) {
    if (frames.empty())
        return {};
    CV_Assert(tiers.empty() || tiers.size() == frames.size());
    CV_Assert(exposureProducts.empty() || exposureProducts.size() == frames.size());
    size_t reference = 0;
    double bestQuality = -1;
    for (size_t i = 0; i < frames.size(); ++i) {
        if ((!tiers.empty() && tiers[i] != 0) || frames[i].empty() || frames[i].type() != CV_8UC3 ||
            frames[i].size() != frames.front().size())
            continue;
        double quality = fusion::referenceQuality(frames[i]);
        if (!exposureProducts.empty() && exposureProducts[i] > 0 && exposureProducts[0] > 0)
            quality /=
                1.0 + std::max(0.0, std::abs(std::log(exposureProducts[i] / exposureProducts[0])) - .15);
        LOGI("Reference candidate %zu quality=%.4f", i, quality);
        if (quality > bestQuality * 1.000001) {
            bestQuality = quality;
            reference = i;
        }
    }
    const auto &ref = frames[reference];
    CV_Assert(!ref.empty() && ref.type() == CV_8UC3);
    CV_Assert(tiers.empty() || tiers.size() == frames.size());
    CV_Assert(exposureProducts.empty() || exposureProducts.size() == frames.size());
    CV_Assert(tiers.empty() || tiers.front() == 0);

    std::vector<Plate> anchors{prepare(ref, cv::Mat::eye(3, 3, CV_64F))};
    std::vector<Plate> highlights;
    cv::Mat refGray;
    cv::cvtColor(ref, refGray, cv::COLOR_BGR2GRAY);
    const cv::Mat highlightValidity = fusion::unclippedReference(ref);
    cv::Rect commonCrop(0, 0, ref.cols, ref.rows);
    cv::Matx33d firstToReference = cv::Matx33d::eye();
    bool geometryKnown = reference == 0;
    std::vector<std::array<double, 2>> phases;
    double reliableCoverage = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (i == reference)
            continue;
        if (frames[i].empty() || frames[i].size() != ref.size() || frames[i].type() != ref.type())
            continue;
        const int tier = tiers.empty() ? 0 : tiers[i];
        const double exposureGain = (!exposureProducts.empty() && exposureProducts[i] > 0)
                                        ? exposureProducts[reference] / exposureProducts[i]
                                        : 0;
        if (tier != 0 && (!isScreenMode || exposureGain <= 1.25 || !std::isfinite(exposureGain)))
            continue;
        cv::Mat warped, h;
        bool aligned;
        if (tier == 0) {
            aligned = alignBaseFrame(frames[i], ref, warped, h);
        } else {
            aligned = alignHighlightFrame(frames[i], ref, warped, h, exposureGain);
        }
        if (!aligned || warped.empty() || h.empty() || !cv::checkRange(h) ||
            std::abs(cv::determinant(h)) < 1e-9) {
            LOGI("Frame %zu tier=%d rejected: alignment failed", i, tier);
            continue;
        }
        Plate plate = prepare(frames[i], h);
        if (i == 0) {
            cv::Mat hd;
            h.convertTo(hd, CV_64F);
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    firstToReference(r, c) = hd.at<double>(r, c);
            geometryKnown = true;
        }
        cv::Mat gray;
        if (tier != 0)
            plate.gain = fusion::responseGain(ref, warped, exposureGain);
        if (tier != 0)
            LOGI("Frame %zu short responseGain=%.3f metadataGain=%.3f", i, plate.gain, exposureGain);
        cv::cvtColor(tier != 0 ? fusion::normalizeExposure(warped, plate.gain) : warped, gray,
                     cv::COLOR_BGR2GRAY);
        plate.local =
            fusion::registerLocal(refGray, gray, tier != 0, false, tier != 0 ? highlightValidity : cv::Mat());
        LOGI("Frame %zu tier=%d localSupport=%.3f detailNodes=%.3f denoiseOnly=%.3f noiseSigma=%.2f "
             "maxRefinement=%.3fpx",
             i, tier, plate.local.supportedFraction, plate.local.detailFraction,
             plate.local.denoiseOnlyFraction, plate.local.noiseSigma, plate.local.maxOffset);
        if (tier == 0) {
            if (plate.local.supportedFraction < .005f || cv::countNonZero(plate.local.gate) < 1024) {
                LOGI("Frame %zu rejected: insufficient local support", i);
                continue;
            }
            auto crop = commonCrop;
            if (!cropForPlate(plate, crop)) {
                LOGI("Frame %zu rejected: source border loss exceeds 5 percent per edge", i);
                continue;
            }
            commonCrop = crop;
            const auto centre = project(plate.inverse, ref.cols * .5, ref.rows * .5);
            const double dx = centre.x - ref.cols * .5, dy = centre.y - ref.rows * .5;
            if (plate.local.detailFraction >= .15f)
                phases.push_back({dx - std::round(dx), dy - std::round(dy)});
            reliableCoverage += plate.local.supportedFraction;
            anchors.push_back(std::move(plate));
        } else if (isScreenMode && !exposureProducts.empty() && exposureProducts[i] > 0) {
            const double gain = exposureProducts[reference] / exposureProducts[i];
            // Reject an ignored manual exposure request rather than invent HDR.
            if (gain <= 1.25)
                continue;
            if (plate.gain > 1.1f && cv::countNonZero(plate.local.gate) >= 1024) {
                plate.highlightNeighborhood = fusion::highlightNeighborhood(plate.image, plate.gain);
                highlights.push_back(std::move(plate));
            }
        }
    }
    // Prefer the least underexposed valid plate (better SNR); use the deeper
    // bracket only where the brighter plate is still clipped.
    std::stable_sort(highlights.begin(), highlights.end(),
                     [](const Plate &a, const Plate &b) { return a.gain < b.gain; });
    std::vector<fusion::SamplingPlate> sampling;
    for (const auto &plate : anchors)
        sampling.push_back({&plate.local, plate.inverse, plate.image.size(), &plate.image});
    const auto regions = superResolution && fusion::reconstructionFits(ref.size(), commonCrop)
                             ? fusion::qualifyRegions(sampling, commonCrop)
                             : fusion::RegionalPlan{};
    const double meanSupport = reliableCoverage / std::max(size_t(1), anchors.size() - 1);
    const bool centrePhases = fusion::phasesCover2D(phases);
    const bool evaluated =
        superResolution && fusion::reconstructionFits(ref.size(), commonCrop) && anchors.size() >= 3;
    const int denial =
        (!superResolution ? 1 : 0) | (anchors.size() < 3 ? 2 : 0) |
        (evaluated && regions.qualified < fusion::RegionalPlan::minimumTiles ? 4 : 0) |
        (evaluated && regions.fraction < fusion::RegionalPlan::minimumArea ? 8 : 0) |
        (ref.total() > 16000000 ? 16 : 0) |
        (static_cast<long long>(commonCrop.width) * commonCrop.height * 4 > 56000000 ? 128 : 0);
    int scale = denial == 0 ? 2 : 1;
    LOGI("SR decision v1: requested=%d anchors=%zu centrePhases=%d meanSupport=%.4f qualifiedTiles=%d "
         "totalTiles=%d regionalCoverage=%.4f minTiles=4 minArea=0.02 tileSupport=0.70 phaseDet=0.04 "
         "denied=%d sourcePixels=%zu scale=%d",
         superResolution, anchors.size(), centrePhases, meanSupport, regions.qualified, regions.total,
         regions.fraction, denial, ref.total(), scale);
    if (report) {
        report->anchors = int(anchors.size());
        report->highlights = int(highlights.size());
        report->scale = scale;
        report->reference = int(reference);
        report->crop = commonCrop;
        report->sourceSize = ref.size();
        report->firstToReference = firstToReference;
        report->geometryKnown = geometryKnown;
        report->srRequested = superResolution;
        report->srDenied = denial;
        report->centrePhases = centrePhases;
        report->meanSupport = meanSupport;
        report->regionTiles = regions.total;
        report->qualifiedTiles = regions.qualified;
        report->regionFraction = regions.fraction;
    }
    LOGI("Fusion: reference=%zu anchors=%zu highlights=%zu phases2D=%d scale=%d crop=%d,%d,%d,%d "
         "source=%dx%d; glare flag=%d",
         reference, anchors.size(), highlights.size(), centrePhases, scale, commonCrop.x, commonCrop.y,
         commonCrop.width, commonCrop.height, ref.cols, ref.rows, removeGlare);
    cv::Mat native(commonCrop.height, commonCrop.width, CV_8UC3), result;
    long long contributorCount = 0, multiFramePixels = 0, recoveredPixels = 0, clippedPixels = 0,
              reconstructedPixels = 0;
    unsigned usedHighlightMask = 0;
#pragma omp parallel for schedule(static) reduction(+ : contributorCount, multiFramePixels)
    for (int y = 0; y < native.rows; ++y)
        for (int x = 0; x < native.cols; ++x) {
            const double rx = commonCrop.x + x, ry = commonCrop.y + y;
            fusion::Color base, sum{};
            sample(ref, rx, ry, base);
            float weights = 0;
            int contributors = 0;
            for (const auto &plate : anchors) {
                if (!plate.local.gate.empty() && !plate.local.gate.at<uchar>(int(ry), int(rx)))
                    continue;
                cv::Point2d delta;
                float confidence;
                if (!plate.local.offset(rx, ry, delta, confidence))
                    continue;
                const auto point = project(plate.inverse, rx + delta.x, ry + delta.y);
                fusion::Color color;
                if (!sample(plate.image, point.x, point.y, color))
                    continue;
                for (int c = 0; c < 3; ++c)
                    sum[c] += color[c] * confidence;
                weights += confidence;
                ++contributors;
            }
            if (weights > 1e-5f)
                for (int c = 0; c < 3; ++c)
                    base[c] = sum[c] / weights;
            for (int c = 0; c < 3; ++c)
                native.at<cv::Vec3b>(y, x)[c] = cv::saturate_cast<uchar>(base[c]);
            contributorCount += std::max(1, contributors);
            if (contributors >= 2)
                ++multiFramePixels;
        }
    const double nativePixels = native.total();
    cv::Mat preserve = scale == 2 ? cv::Mat::zeros(native.size(), CV_8U) : cv::Mat();
#pragma omp parallel for schedule(static) reduction(+ : recoveredPixels, clippedPixels)                      \
    reduction(| : usedHighlightMask)
    for (int y = 0; y < native.rows; ++y)
        for (int x = 0; x < native.cols; ++x) {
            auto &pixel = native.at<cv::Vec3b>(y, x);
            fusion::Color base{float(pixel[0]), float(pixel[1]), float(pixel[2])};
            const double rx = commonCrop.x + x, ry = commonCrop.y + y;
            const int gx = std::clamp(int(std::round(rx)), 0, ref.cols - 1),
                      gy = std::clamp(int(std::round(ry)), 0, ref.rows - 1);
            if (*std::max_element(base.begin(), base.end()) >= 250)
                ++clippedPixels;
            const auto original = ref.at<cv::Vec3b>(int(ry), int(rx));
            if (!preserve.empty() && (*std::max_element(base.begin(), base.end()) >= 247 ||
                                      std::max({original[0], original[1], original[2]}) >= 247))
                preserve.at<uchar>(y, x) = 255;
            if (*std::max_element(base.begin(), base.end()) > 230)
                for (size_t hi = 0; hi < highlights.size(); ++hi) {
                    const auto &plate = highlights[hi];
                    if (!plate.local.gate.at<uchar>(gy, gx))
                        continue;
                    cv::Point2d delta;
                    float confidence;
                    if (!plate.local.offset(rx, ry, delta, confidence))
                        continue;
                    const auto point = project(plate.inverse, rx + delta.x, ry + delta.y);
                    fusion::Color color;
                    if (!sample(plate.image, point.x, point.y, color))
                        continue;
                    const float peak = *std::max_element(color.begin(), color.end());
                    const bool observedDark =
                        *std::max_element(base.begin(), base.end()) >= 247 &&
                        plate.highlightNeighborhood.at<uchar>(
                            std::clamp(int(std::round(point.y)), 0, plate.image.rows - 1),
                            std::clamp(int(std::round(point.x)), 0, plate.image.cols - 1));
                    if (peak >= 246 ||
                        (!observedDark && (peak < 3 || fusion::linear(peak) * plate.gain < .60f)) ||
                        !fusion::compatibleHighlight(base, color, plate.gain))
                        continue;
                    const auto recovered = fusion::recoverHighlight(base, color, plate.gain, observedDark);
                    float change = 0;
                    for (int c = 0; c < 3; ++c)
                        change = std::max(change, std::abs(base[c] - recovered[c]));
                    if (change < .5f)
                        continue;
                    base = recovered;
                    ++recoveredPixels;
                    if (!preserve.empty())
                        preserve.at<uchar>(y, x) = 255;
                    usedHighlightMask |= 1u << hi;
                    break;
                }
            for (int c = 0; c < 3; ++c)
                pixel[c] = cv::saturate_cast<uchar>(base[c]);
        }
    if (!preserve.empty())
        cv::dilate(preserve, preserve, cv::getStructuringElement(cv::MORPH_RECT, {3, 3}));
    if (scale == 2) {
        cv::resize(native, result, {native.cols * 2, native.rows * 2}, 0, 0, cv::INTER_CUBIC);
        const auto fitted = fusion::refineRegions(sampling, regions, result, preserve);
        reconstructedPixels = fitted.pixels;
        if (report) {
            report->fittedTiles = fitted.fittedTiles;
            report->initialFitError = fitted.initialError;
            report->finalFitError = fitted.finalError;
        }
        LOGI("Regional fit: tiles=%d initialError=%.6f finalError=%.6f pixels=%lld", fitted.fittedTiles,
             fitted.initialError, fitted.finalError, fitted.pixels);
        if (!reconstructedPixels) {
            result = native;
            scale = 1;
            if (report) {
                report->scale = 1;
                report->srDenied |= 64;
            }
        }
    } else
        result = native;
    native.release();
    const double multiFraction = double(multiFramePixels) / nativePixels;
    const double meanContributors = double(contributorCount) / nativePixels;
    int usedHighlights = 0;
    for (unsigned bits = usedHighlightMask; bits; bits >>= 1)
        usedHighlights += bits & 1u;
    if (report) {
        report->multiFrameFraction = multiFraction;
        report->meanContributors = meanContributors;
        report->highlights = usedHighlights;
        report->recoveredFraction = double(recoveredPixels) / nativePixels;
        report->clippedFraction = double(clippedPixels) / nativePixels;
        report->reconstructedFraction = double(reconstructedPixels) / result.total();
    }
    LOGI("Regional reconstruction: tiles=%d/%d eligibleArea=%.6f actualPixelFraction=%.6f", regions.qualified,
         regions.total, regions.fraction, double(reconstructedPixels) / result.total());
    LOGI("Highlight pixels: framesUsed=%d admitted=%zu recovered=%lld clipped=%lld fraction=%.6f",
         usedHighlights, highlights.size(), recoveredPixels, clippedPixels,
         double(recoveredPixels) / nativePixels);
    LOGI("Contribution: multiFrameFraction=%.4f meanContributors=%.3f", multiFraction, meanContributors);
    return result;
}
