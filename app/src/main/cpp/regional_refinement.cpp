#include "regional_reconstruction.h"
#include <limits>
#include <opencv2/imgproc.hpp>

namespace fusion {
namespace {
struct Observation {
    int index[16];
    float weight[16];
    cv::Vec3f color;
};
cv::Point2d project(const cv::Matx33d &h, double x, double y) {
    const auto p = h * cv::Vec3d(x, y, 1);
    if (!std::isfinite(p[2]) || std::abs(p[2]) < 1e-9)
        return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    return {p[0] / p[2], p[1] / p[2]};
}
bool referencePoint(const SamplingPlate &plate, const cv::Matx33d &forward, double x, double y,
                    cv::Point2d &r) {
    const auto initial = project(forward, x, y);
    if (!std::isfinite(initial.x) || !std::isfinite(initial.y))
        return false;
    r = initial;
    for (int iteration = 0; iteration < 3; ++iteration) {
        cv::Point2d delta;
        float confidence;
        if (plate.local && !plate.local->offset(r.x, r.y, delta, confidence))
            return false;
        r = initial - delta;
        if (!std::isfinite(r.x) || !std::isfinite(r.y))
            return false;
    }
    if (plate.local && !plate.local->field.empty()) {
        cv::Point2d p;
        float confidence;
        if (!detailMapping(plate, r.x, r.y, p, confidence) || cv::norm(p - cv::Point2d(x, y)) > .15)
            return false;
        if (!plate.local->gate.at<uchar>(std::clamp(int(std::round(r.y)), 0, plate.size.height - 1),
                                         std::clamp(int(std::round(r.x)), 0, plate.size.width - 1)))
            return false;
    }
    return std::isfinite(r.x) && std::isfinite(r.y);
}
double cost(const cv::Mat &estimate, const std::vector<Observation> &observations) {
    const auto *p = estimate.ptr<cv::Vec3f>();
    double error = 0;
    for (const auto &o : observations) {
        cv::Vec3f value(0, 0, 0);
        for (int k = 0; k < 16; ++k)
            value += p[o.index[k]] * o.weight[k];
        for (int c = 0; c < 3; ++c) {
            const double d = std::clamp(double(o.color[c] - value[c]), -14., 14.);
            error += d * d;
        }
    }
    return error / std::max(size_t(1), observations.size() * 3);
}
bool fitPatch(const std::vector<SamplingPlate> &plates, unsigned mask, const cv::Rect &patch,
              cv::Mat &estimate, double &before, double &after) {
    cv::resize((*plates[0].image)(patch), estimate, {patch.width * 2, patch.height * 2}, 0, 0,
               cv::INTER_CUBIC);
    estimate.convertTo(estimate, CV_32FC3);
    const cv::Mat initial = estimate.clone();
    std::vector<Observation> observations;
    int observedSources = 0;
    for (size_t ai = 0; ai < plates.size(); ++ai)
        if (mask & (1u << ai)) {
            const auto &plate = plates[ai];
            if (!plate.image)
                continue;
            const cv::Matx33d forward = plate.inverse.inv();
            double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
            bool finiteBounds = true;
            for (double y : {double(patch.y), double(patch.br().y - 1)})
                for (double x : {double(patch.x), double(patch.br().x - 1)}) {
                    const auto p = project(plate.inverse, x, y);
                    if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                        finiteBounds = false;
                        continue;
                    }
                    minX = std::min(minX, p.x);
                    minY = std::min(minY, p.y);
                    maxX = std::max(maxX, p.x);
                    maxY = std::max(maxY, p.y);
                }
            if (!finiteBounds)
                continue;
            const int left =
                std::clamp(int(std::floor(std::clamp(minX, 0., double(plate.size.width - 1)))) - 10, 0,
                           plate.size.width - 1);
            const int top =
                std::clamp(int(std::floor(std::clamp(minY, 0., double(plate.size.height - 1)))) - 10, 0,
                           plate.size.height - 1);
            const int right =
                std::clamp(int(std::ceil(std::clamp(maxX, 0., double(plate.size.width - 1)))) + 10, 0,
                           plate.size.width - 1);
            const int bottom =
                std::clamp(int(std::ceil(std::clamp(maxY, 0., double(plate.size.height - 1)))) + 10, 0,
                           plate.size.height - 1);
            if (right - left >= 128 || bottom - top >= 128)
                continue;
            const auto startCount = observations.size();
            for (int sy = top; sy <= bottom; ++sy)
                for (int sx = left; sx <= right; ++sx) {
                    const auto pixel = plate.image->at<cv::Vec3b>(sy, sx);
                    if (std::max({pixel[0], pixel[1], pixel[2]}) >= 247)
                        continue;
                    Observation o{};
                    o.color = cv::Vec3f(pixel[0], pixel[1], pixel[2]);
                    bool valid = true;
                    int k = 0;
                    for (double dy : {-.25, .25})
                        for (double dx : {-.25, .25}) {
                            cv::Point2d r;
                            if (!referencePoint(plate, forward, sx + dx, sy + dy, r)) {
                                valid = false;
                                break;
                            }
                            const double x = 2 * (r.x - patch.x) + .5, y = 2 * (r.y - patch.y) + .5;
                            if (x < 0 || y < 0 || x >= estimate.cols - 1 || y >= estimate.rows - 1) {
                                valid = false;
                                break;
                            }
                            const int ix = int(x), iy = int(y);
                            const float fx = x - ix, fy = y - iy;
                            for (int yy = 0; yy < 2; ++yy)
                                for (int xx = 0; xx < 2; ++xx) {
                                    o.index[k] = (iy + yy) * estimate.cols + ix + xx;
                                    o.weight[k++] = .25f * (xx ? fx : 1 - fx) * (yy ? fy : 1 - fy);
                                }
                        }
                    if (valid && k == 16) {
                        observations.push_back(o);
                        if (observations.size() > 50000)
                            return false;
                    }
                }
            if (observations.size() - startCount >= 256)
                ++observedSources;
        }
    if (observedSources < 3)
        return false;
    before = after = cost(estimate, observations);
    if (before <= 1)
        return false;
    for (int iteration = 0; iteration < 8; ++iteration) {
        cv::Mat gradient = cv::Mat::zeros(estimate.size(), CV_32FC3),
                norm = cv::Mat::zeros(estimate.size(), CV_32F);
        const auto *values = estimate.ptr<cv::Vec3f>();
        auto *grad = gradient.ptr<cv::Vec3f>();
        auto *denom = norm.ptr<float>();
        for (const auto &o : observations) {
            cv::Vec3f prediction(0, 0, 0);
            for (int k = 0; k < 16; ++k)
                prediction += values[o.index[k]] * o.weight[k];
            cv::Vec3f residual;
            for (int c = 0; c < 3; ++c)
                residual[c] = std::clamp(o.color[c] - prediction[c], -14.f, 14.f);
            for (int k = 0; k < 16; ++k) {
                grad[o.index[k]] += residual * o.weight[k];
                denom[o.index[k]] += o.weight[k];
            }
        }
        cv::Mat next = estimate.clone();
        auto *corrected = next.ptr<cv::Vec3f>();
        const auto *start = initial.ptr<cv::Vec3f>();
        for (size_t i = 0; i < next.total(); ++i)
            if (denom[i] > 1e-5f)
                for (int c = 0; c < 3; ++c)
                    corrected[i][c] =
                        std::clamp(values[i][c] + .8f * grad[i][c] / denom[i],
                                   std::max(0.f, start[i][c] - 24), std::min(255.f, start[i][c] + 24));
        const double error = cost(next, observations);
        if (error >= after)
            break;
        estimate = std::move(next);
        after = error;
    }
    return after <= before * .95;
}
} // namespace
RefinementResult refineRegions(const std::vector<SamplingPlate> &plates, const RegionalPlan &plan,
                               cv::Mat &output, const cv::Mat &preserveNative) {
    RefinementResult result;
    long long pixels = 0;
    int fitted = 0;
    double initialError = 0, finalError = 0;
#pragma omp parallel for schedule(dynamic) reduction(+ : pixels, fitted, initialError, finalError)
    for (int ty = 0; ty < plan.candidates.rows; ++ty)
        for (int tx = 0; tx < plan.candidates.cols; ++tx) {
            const unsigned mask = plan.candidates.at<uchar>(ty, tx);
            if (!mask)
                continue;
            const cv::Rect tile(plan.crop.x + tx * 64, plan.crop.y + ty * 64,
                                std::min(64, plan.crop.width - tx * 64),
                                std::min(64, plan.crop.height - ty * 64));
            const cv::Rect patch = (cv::Rect(tile.x - 8, tile.y - 8, tile.width + 16, tile.height + 16) &
                                    cv::Rect(0, 0, plates[0].size.width, plates[0].size.height));
            cv::Mat estimate, gray, contrast;
            double before = 0, after = 0, minimumContrast = 0;
            try {
                if (!fitPatch(plates, mask, patch, estimate, before, after))
                    continue;
                cv::cvtColor((*plates[0].image)(patch), gray, cv::COLOR_BGR2GRAY);
                minimumContrast = std::max(12., 6 * estimateNoiseSigma(gray));
                cv::morphologyEx(gray, contrast, cv::MORPH_GRADIENT,
                                 cv::getStructuringElement(cv::MORPH_RECT, {5, 5}));
            } catch (const std::exception &) {
                continue;
            }
            ++fitted;
            initialError += before;
            finalError += after;
            for (int oy = (tile.y - plan.crop.y) * 2; oy < (tile.br().y - plan.crop.y) * 2; ++oy)
                for (int ox = (tile.x - plan.crop.x) * 2; ox < (tile.br().x - plan.crop.x) * 2; ++ox) {
                    if (!preserveNative.empty() &&
                        preserveNative.at<uchar>(
                            std::clamp(int(std::round(sourceCoordinate(oy, 2))), 0, preserveNative.rows - 1),
                            std::clamp(int(std::round(sourceCoordinate(ox, 2))), 0, preserveNative.cols - 1)))
                        continue;
                    const double rx = plan.crop.x + sourceCoordinate(ox, 2),
                                 ry = plan.crop.y + sourceCoordinate(oy, 2);
                    if (contrast.at<uchar>(std::clamp(int(std::round(ry - patch.y)), 0, contrast.rows - 1),
                                           std::clamp(int(std::round(rx - patch.x)), 0, contrast.cols - 1)) <
                        minimumContrast)
                        continue;
                    std::array<double, 2> phases[7];
                    int count = 0;
                    for (size_t ai = 1; ai < plates.size(); ++ai)
                        if (mask & (1u << ai)) {
                            const auto &plate = plates[ai];
                            cv::Point2d p;
                            float confidence;
                            const int gx = std::clamp(int(std::round(rx)), 0, plate.size.width - 1),
                                      gy = std::clamp(int(std::round(ry)), 0, plate.size.height - 1);
                            if (!plate.local->gate.at<uchar>(gy, gx) ||
                                !detailMapping(plate, rx, ry, p, confidence))
                                continue;
                            const double dx = p.x - rx, dy = p.y - ry;
                            phases[count++] = {dx - std::round(dx), dy - std::round(dy)};
                        }
                    if (!phasesCover2D(phases, count))
                        continue;
                    const int px = ox + (plan.crop.x - patch.x) * 2, py = oy + (plan.crop.y - patch.y) * 2;
                    const auto value = estimate.at<cv::Vec3f>(py, px);
                    const double edge = std::min(
                        {rx - tile.x + .5, ry - tile.y + .5, tile.br().x - rx - .5, tile.br().y - ry - .5});
                    const float blend = std::clamp(float(edge / 4), 0.f, 1.f);
                    if (blend <= 0)
                        continue;
                    auto &pixel = output.at<cv::Vec3b>(oy, ox);
                    for (int c = 0; c < 3; ++c)
                        pixel[c] = cv::saturate_cast<uchar>(pixel[c] * (1 - blend) + value[c] * blend);
                    ++pixels;
                }
        }
    result.pixels = pixels;
    result.fittedTiles = fitted;
    result.initialError = initialError / std::max(1, fitted);
    result.finalError = finalError / std::max(1, fitted);
    return result;
}
} // namespace fusion
