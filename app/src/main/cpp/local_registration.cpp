#include "local_registration.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace fusion {
namespace {
cv::Mat highPass(const cv::Mat& gray) {
    cv::Mat f, low, band;
    gray.convertTo(f, CV_32F);
    // Compare a subpixel-stable band. Raw high-pass energy collapses under
    // legitimate interpolation and would reject the very phases SR needs.
    cv::GaussianBlur(f, band, cv::Size(0, 0), .8);
    cv::GaussianBlur(f, low, cv::Size(0, 0), 2.0);
    return band - low;
}
double correlation(const cv::Mat& a, const cv::Mat& b, const cv::Mat& mask, double& ratio) {
    if (cv::countNonZero(mask) < 256) { ratio = 0; return -1; }
    cv::Scalar ma, sa, mb, sb;
    cv::meanStdDev(a, ma, sa, mask); cv::meanStdDev(b, mb, sb, mask);
    ratio = sb[0] / std::max(sa[0], .01);
    return (cv::mean(a.mul(b), mask)[0] - ma[0] * mb[0]) / std::max(sa[0] * sb[0], .01);
}
float graySample(const cv::Mat& image, double x, double y) {
    if (x < 0 || y < 0 || x > image.cols - 1 || y > image.rows - 1) return -1000;
    const int ix = int(x), iy = int(y);
    const int nx = std::min(ix + 1, image.cols - 1), ny = std::min(iy + 1, image.rows - 1);
    const float fx = x - ix, fy = y - iy;
    return (1 - fy) * ((1 - fx) * image.at<uchar>(iy, ix) + fx * image.at<uchar>(iy, nx)) +
        fy * ((1 - fx) * image.at<uchar>(ny, ix) + fx * image.at<uchar>(ny, nx));
}
}

double referenceQuality(const cv::Mat& image) {
    cv::Mat gray, smooth, lap;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, smooth, cv::Size(0, 0), .7);
    cv::Laplacian(smooth, lap, CV_32F, 3);
    std::vector<double> tiles;
    for (int y = 0; y < gray.rows; y += 256) for (int x = 0; x < gray.cols; x += 256) {
        const cv::Rect r(x, y, std::min(256, gray.cols - x), std::min(256, gray.rows - y));
        cv::Scalar mean, sd;
        cv::meanStdDev(lap(r), mean, sd);
        tiles.push_back(sd[0] * sd[0]);
    }
    std::sort(tiles.begin(), tiles.end());
    const double detail = tiles[std::min(tiles.size() - 1, tiles.size() * 3 / 4)];
    const double clipped = double(cv::countNonZero((gray <= 3) | (gray >= 252))) / gray.total();
    return detail * (1 - .5 * clipped);
}

bool LocalRegistration::offset(double x, double y, cv::Point2d& delta, float& confidence) const {
    if (field.empty()) { delta = {0, 0}; confidence = 1; return true; }
    const double gx = std::clamp(x / spacing, 0.0, double(field.cols - 1));
    const double gy = std::clamp(y / spacing, 0.0, double(field.rows - 1));
    const int x0 = int(gx), y0 = int(gy), x1 = std::min(x0 + 1, field.cols - 1), y1 = std::min(y0 + 1, field.rows - 1);
    const float fx = gx - x0, fy = gy - y0;
    const cv::Vec3f p[] = {field.at<cv::Vec3f>(y0, x0), field.at<cv::Vec3f>(y0, x1),
                          field.at<cv::Vec3f>(y1, x0), field.at<cv::Vec3f>(y1, x1)};
    const float weights[] = {(1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy};
    cv::Vec3f sum(0, 0, 0);
    for (int i = 0; i < 4; ++i) {
        if (weights[i] <= .001f) continue;
        if (p[i][2] <= 0) return false;
        for (int j = 0; j < i; ++j) {
            if (weights[j] > .001f && std::hypot(p[i][0]-p[j][0], p[i][1]-p[j][1]) > 1.5f)
                return false; // Do not interpolate a motion discontinuity across text.
        }
        sum += p[i] * weights[i];
    }
    delta = {sum[0], sum[1]}; confidence = sum[2];
    return confidence > 0;
}

LocalRegistration registerLocal(const cv::Mat& reference, const cv::Mat& warped, bool highlight) {
    CV_Assert(reference.type() == CV_8UC1 && reference.size() == warped.size() && warped.type() == CV_8UC1);
    LocalRegistration model;
    const int nx = (reference.cols - 1 + model.spacing - 1) / model.spacing + 1;
    const int ny = (reference.rows - 1 + model.spacing - 1) / model.spacing + 1;
    model.field = cv::Mat::zeros(ny, nx, CV_32FC3);
    const cv::Mat refHP = highPass(reference), srcHP = highPass(warped);
    #pragma omp parallel for schedule(dynamic)
    for (int gy = 0; gy < ny; ++gy) for (int gx = 0; gx < nx; ++gx) {
        const int cx = std::min(gx * model.spacing, reference.cols - 1), cy = std::min(gy * model.spacing, reference.rows - 1);
        const cv::Rect roi(std::max(0, cx - 160), std::max(0, cy - 160),
            std::min(reference.cols, cx + 161) - std::max(0, cx - 160),
            std::min(reference.rows, cy + 161) - std::max(0, cy - 160));
        if (roi.width < 32 || roi.height < 32) continue;
        cv::Mat ref = reference(roi), src = warped(roi);
        cv::Scalar mean, sd;
        cv::meanStdDev(refHP(roi), mean, sd);
        if (sd[0] < 1.5) {
            // Flat regions have no subpixel evidence. They may reduce noise,
            // but cannot count as a local detail validation.
            if (!highlight && std::abs(cv::mean(ref)[0] - cv::mean(src)[0]) < 8)
                model.field.at<cv::Vec3f>(gy, gx) = {0, 0, .35f};
            continue;
        }
        try {
            cv::Mat transform = cv::Mat::eye(2, 3, CV_32F);
            const cv::Mat mask = (src > 8) & (src < 247);
            const double cc = cv::findTransformECC(ref, src, transform, cv::MOTION_TRANSLATION,
                cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, .0001), mask, 3);
            const float dx = transform.at<float>(0, 2), dy = transform.at<float>(1, 2);
            if (!std::isfinite(cc) || cc < .85 || std::hypot(dx, dy) > 8) continue;
            cv::Mat correctedHP;
            cv::warpAffine(srcHP(roi), correctedHP, transform, roi.size(), cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REFLECT_101);
            cv::Mat valid(roi.size(), CV_8U, cv::Scalar(255));
            valid.rowRange(0, 10).setTo(0); valid.rowRange(valid.rows-10, valid.rows).setTo(0);
            valid.colRange(0, 10).setTo(0); valid.colRange(valid.cols-10, valid.cols).setTo(0);
            valid &= (ref > 8) & (ref < 247);
            double detailRatio = 0;
            const double score = correlation(refHP(roi), correctedHP, valid, detailRatio);
            if (score < (highlight ? .80 : .90) || (!highlight && detailRatio < .68)) continue;
            model.field.at<cv::Vec3f>(gy, gx) = {dx, dy, float(std::clamp(score, 0.0, 1.0))};
        } catch (const cv::Exception&) { /* Insufficient local evidence. */ }
    }
    int detailNodes = 0;
    for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        const auto p = model.field.at<cv::Vec3f>(y, x);
        if (p[2] >= .80f) ++detailNodes;
        if (p[2] > 0) model.maxOffset = std::max(model.maxOffset, std::hypot(p[0], p[1]));
    }
    model.detailFraction = float(detailNodes) / (nx * ny);
    model.gate = cv::Mat::zeros(reference.size(), CV_8U);
    cv::Mat refSmooth, srcSmooth;
    cv::GaussianBlur(reference, refSmooth, cv::Size(0, 0), .6);
    cv::GaussianBlur(warped, srcSmooth, cv::Size(0, 0), .6);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < reference.rows; ++y) {
        auto* row = model.gate.ptr<uchar>(y);
        for (int x = 0; x < reference.cols; ++x) {
            cv::Point2d d; float confidence;
            if (!model.offset(x, y, d, confidence)) continue;
            const float observed = graySample(srcSmooth, x + d.x, y + d.y);
            // Same-exposure photometric check protects occlusions and fine strokes.
            // Cross-exposure support is structural; brightness is intentionally different.
            if (observed >= 0 && (highlight || std::abs(observed - refSmooth.at<uchar>(y, x)) <= 14)) row[x] = 255;
        }
    }
    model.supportedFraction = float(cv::countNonZero(model.gate)) / model.gate.total();
    return model;
}
}
