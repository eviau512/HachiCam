#include "perspective_corrector.h"
#include <cmath>
#include <algorithm>
#include "paper_math.h"

cv::Mat PerspectiveCorrector::correctPerspective(const cv::Mat& src, const std::vector<cv::Point2f>& srcCorners, float targetAspectRatio) {
    if (src.empty() || srcCorners.size() != 4) {
        return src;
    }

    const cv::Point2f& tl = srcCorners[0];
    const cv::Point2f& tr = srcCorners[1];
    const cv::Point2f& br = srcCorners[2];
    const cv::Point2f& bl = srcCorners[3];

    double widthA = std::hypot(br.x - bl.x, br.y - bl.y);
    double widthB = std::hypot(tr.x - tl.x, tr.y - tl.y);
    double heightA = std::hypot(tr.x - br.x, tr.y - br.y);
    double heightB = std::hypot(tl.x - bl.x, tl.y - bl.y);

    double maxW = std::max(widthA, widthB);
    double minW = std::min(widthA, widthB);
    double maxH = std::max(heightA, heightB);
    double minH = std::min(heightA, heightB);

    int maxWidth = std::max(10, static_cast<int>(std::round(maxW)));
    int maxHeight = std::max(10, static_cast<int>(std::round(maxH)));

    if (targetAspectRatio > 0.01f) {
        // Enforce target aspect ratio (W / H) strictly without orientation flipping
        float effRatio = targetAspectRatio;
        if (maxW / effRatio >= maxH) {
            maxWidth = std::max(10, static_cast<int>(std::round(maxW)));
            maxHeight = std::max(10, static_cast<int>(std::round(maxWidth / effRatio)));
        } else {
            maxHeight = std::max(10, static_cast<int>(std::round(maxH)));
            maxWidth = std::max(10, static_cast<int>(std::round(maxHeight * effRatio)));
        }
    } else {
        // Free mode: Projective foreshortening vertical recovery estimation
        // Document camera tilt causes perspective convergence of opposite sides and foreshortening along tilt axis.
        double ratioW = (maxW > 1.0) ? (minW / maxW) : 1.0;
        double ratioH = (maxH > 1.0) ? (minH / maxH) : 1.0;

        if (ratioW < ratioH && ratioW > 0.1) {
            // Vertical foreshortening recovery factor: gamma_v = sqrt(1 / ratioW)
            double gammaV = std::clamp(std::sqrt(1.0 / ratioW), 1.0, 1.6);
            maxHeight = std::max(10, static_cast<int>(std::round(maxH * gammaV)));
        } else if (ratioH < ratioW && ratioH > 0.1) {
            // Horizontal foreshortening recovery factor: gamma_h = sqrt(1 / ratioH)
            double gammaH = std::clamp(std::sqrt(1.0 / ratioH), 1.0, 1.6);
            maxWidth = std::max(10, static_cast<int>(std::round(maxW * gammaH)));
        }
    }

    std::vector<cv::Point2f> dstCorners = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(maxWidth - 1), 0.0f),
        cv::Point2f(static_cast<float>(maxWidth - 1), static_cast<float>(maxHeight - 1)),
        cv::Point2f(0.0f, static_cast<float>(maxHeight - 1))
    };

    cv::Mat transformMatrix = cv::getPerspectiveTransform(srcCorners, dstCorners);
    cv::Mat warped;
    cv::warpPerspective(src, warped, transformMatrix, cv::Size(maxWidth, maxHeight), cv::INTER_LINEAR);

    return warped;
}

namespace {
cv::Mat paperRendering(const cv::Mat& input, bool gentle) {
    if (input.empty()) return {};
    CV_Assert(input.depth() == CV_8U);
    cv::Mat src;
    if (input.channels() == 1) cv::cvtColor(input, src, cv::COLOR_GRAY2BGR);
    else if (input.channels() == 4) cv::cvtColor(input, src, cv::COLOR_BGRA2BGR);
    else { CV_Assert(input.channels() == 3); src = input; }
    const double scale = std::min(1.0, 512.0 / std::max(src.cols, src.rows));
    cv::Mat small, lab, luma, background;
    cv::resize(src, small, cv::Size(std::max(1, int(std::round(src.cols * scale))),
        std::max(1, int(std::round(src.rows * scale)))), 0, 0, cv::INTER_AREA);
    cv::cvtColor(small, lab, cv::COLOR_BGR2Lab);
    cv::extractChannel(lab, luma, 0);
    const int kernel = std::max(3, (std::max(small.cols, small.rows) / 25) | 1);
    cv::morphologyEx(luma, background, cv::MORPH_CLOSE,
        cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kernel, kernel)));
    cv::GaussianBlur(background, background, cv::Size(0, 0), std::max(.6, kernel * .35), 0, cv::BORDER_REPLICATE);
    cv::Mat result(src.size(), CV_8UC3);
    // Tile conversion bounds scratch memory even for a 50 MP source. Sample the
    // small illumination field directly; no full-size float/integral images.
    for (int y = 0; y < src.rows; y += 256) {
        for (int x = 0; x < src.cols; x += 512) {
            const cv::Rect roi(x, y, std::min(512, src.cols - x), std::min(256, src.rows - y));
            cv::Mat tile, rendered;
            cv::cvtColor(src(roi), tile, cv::COLOR_BGR2Lab);
            for (int ty = 0; ty < tile.rows; ++ty) {
                auto* row = tile.ptr<cv::Vec3b>(ty);
                const double sy = std::clamp((y + ty + .5) * background.rows / src.rows - .5, 0.0, double(background.rows - 1));
                const int y0 = int(sy), y1 = std::min(y0 + 1, background.rows - 1);
                const float fy = sy - y0;
                for (int tx = 0; tx < tile.cols; ++tx) {
                    const double sx = std::clamp((x + tx + .5) * background.cols / src.cols - .5, 0.0, double(background.cols - 1));
                    const int x0 = int(sx), x1 = std::min(x0 + 1, background.cols - 1);
                    const float fx = sx - x0;
                    const float bg = (1 - fy) * ((1 - fx) * background.at<uchar>(y0, x0) + fx * background.at<uchar>(y0, x1)) +
                        fy * ((1 - fx) * background.at<uchar>(y1, x0) + fx * background.at<uchar>(y1, x1));
                    const float chroma = std::hypot(float(row[tx][1]) - 128, float(row[tx][2]) - 128);
                    row[tx][0] = cv::saturate_cast<uchar>(paper::luminance(row[tx][0], bg, chroma, gentle));
                }
            }
            cv::cvtColor(tile, rendered, cv::COLOR_Lab2BGR);
            rendered.copyTo(result(roi));
        }
    }
    return result;
}
}

cv::Mat PerspectiveCorrector::enhanceMagicColor(const cv::Mat& src) { return paperRendering(src, false); }
cv::Mat PerspectiveCorrector::enhancePaper(const cv::Mat& src) { return paperRendering(src, true); }

cv::Mat PerspectiveCorrector::processDocument(const cv::Mat& src, const std::vector<cv::Point2f>& srcCorners, FilterType filter, float targetAspectRatio) {
    cv::Mat warped = correctPerspective(src, srcCorners, targetAspectRatio);
    if (warped.empty()) return src;

    switch (filter) {
        case FilterType::MAGIC_COLOR:
            return enhanceMagicColor(warped);
        case FilterType::PAPER:
            return enhancePaper(warped);
        case FilterType::ORIGINAL:
        default:
            return warped;
    }
}
