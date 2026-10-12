#include "edge_detector.h"
#include <algorithm>
#include <numeric>
#include <cmath>

std::vector<cv::Point2f> EdgeDetector::sortCorners(const std::vector<cv::Point2f>& points) {
    if (points.size() != 4) return points;

    // 1. 计算 4 角点质心 (Centroid)
    cv::Point2f center(0.0f, 0.0f);
    for (const auto& pt : points) {
        center += pt;
    }
    center.x /= 4.0f;
    center.y /= 4.0f;

    // 2. 按相对于质心的极角升序排序（逆时针或顺时针连续连线，杜绝任何对角交叉）
    struct PolarPoint {
        cv::Point2f pt;
        float angle;
    };

    std::vector<PolarPoint> polarPts;
    polarPts.reserve(4);
    for (const auto& pt : points) {
        float ang = std::atan2(pt.y - center.y, pt.x - center.x);
        polarPts.push_back({pt, ang});
    }

    std::sort(polarPts.begin(), polarPts.end(), [](const PolarPoint& a, const PolarPoint& b) {
        return a.angle < b.angle;
    });

    // 3. 寻找离屏幕坐标原点 (0, 0) 最近的点，作为 Top-Left (左上点)
    int tlIndex = 0;
    float minDistSq = std::numeric_limits<float>::max();
    for (int i = 0; i < 4; ++i) {
        float distSq = polarPts[i].pt.x * polarPts[i].pt.x + polarPts[i].pt.y * polarPts[i].pt.y;
        if (distSq < minDistSq) {
            minDistSq = distSq;
            tlIndex = i;
        }
    }

    // 4. 从左上点开始，沿顺时针方向依次输出: Top-Left, Top-Right, Bottom-Right, Bottom-Left
    // 逆时针排序在屏幕坐标系 (Y向下) 中：angle 递增恰好为顺时针方向
    std::vector<cv::Point2f> sorted(4);
    for (int i = 0; i < 4; ++i) {
        sorted[i] = polarPts[(tlIndex + i) % 4].pt;
    }

    // 验证旋转方向：计算向量叉积判断是否顺时针，如果逆时针则反转
    cv::Point2f v01 = sorted[1] - sorted[0];
    cv::Point2f v03 = sorted[3] - sorted[0];
    float crossProduct = v01.x * v03.y - v01.y * v03.x;
    if (crossProduct < 0) {
        std::swap(sorted[1], sorted[3]);
    }

    return sorted;
}

std::vector<cv::Point> EdgeDetector::findMagneticSnapPoint(const cv::Mat& edgeMat, const cv::Point2f& touchPoint, float radius) {
    std::vector<cv::Point> candidates;
    if (edgeMat.empty() || radius <= 0) return candidates;

    int cx = static_cast<int>(touchPoint.x);
    int cy = static_cast<int>(touchPoint.y);
    int r = static_cast<int>(radius);

    int minX = std::max(0, cx - r);
    int maxX = std::min(edgeMat.cols - 1, cx + r);
    int minY = std::max(0, cy - r);
    int maxY = std::min(edgeMat.rows - 1, cy + r);

    float bestDistSq = radius * radius;
    cv::Point bestPt(-1, -1);

    for (int y = minY; y <= maxY; ++y) {
        const uchar* ptr = edgeMat.ptr<uchar>(y);
        for (int x = minX; x <= maxX; ++x) {
            if (ptr[x] > 180) { // 强边缘点
                float dx = static_cast<float>(x - cx);
                float dy = static_cast<float>(y - cy);
                float distSq = dx * dx + dy * dy;
                if (distSq < bestDistSq) {
                    bestDistSq = distSq;
                    bestPt = cv::Point(x, y);
                }
            }
        }
    }

    if (bestPt.x != -1) {
        candidates.push_back(bestPt);
    }
    return candidates;
}

float EdgeDetector::findMagneticLineOffset(const cv::Mat& edgeMat, const cv::Point2f& p1, const cv::Point2f& p2, float maxOffset) {
    if (edgeMat.empty() || maxOffset <= 0.0f) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float len = std::hypot(dx, dy);
    if (len < 1e-4f) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    // Line unit normal (nx, ny) corresponding to Ax + By + C = 0 where A = dy/len, B = -dx/len
    float nx = dy / len;
    float ny = -dx / len;

    const int N = 25;
    std::vector<cv::Point2f> samplePoints(N);
    for (int k = 0; k < N; ++k) {
        float t = (k + 0.5f) / static_cast<float>(N);
        samplePoints[k] = cv::Point2f(p1.x + t * dx, p1.y + t * dy);
    }

    int stepCount = static_cast<int>(std::floor(maxOffset));
    if (stepCount < 1) stepCount = 1;

    int totalSteps = 2 * stepCount + 1;
    std::vector<float> scores(totalSteps, 0.0f);
    std::vector<float> deltas(totalSteps, 0.0f);

    float globalMaxScore = 0.0f;

    for (int i = 0; i < totalSteps; ++i) {
        int s = -stepCount + i;
        float delta = static_cast<float>(s);
        deltas[i] = delta;

        float score = 0.0f;
        for (int k = 0; k < N; ++k) {
            float px = samplePoints[k].x + delta * nx;
            float py = samplePoints[k].y + delta * ny;

            int ix = static_cast<int>(std::round(px));
            int iy = static_cast<int>(std::round(py));

            if (ix >= 0 && ix < edgeMat.cols && iy >= 0 && iy < edgeMat.rows) {
                score += edgeMat.ptr<uchar>(iy)[ix];
            }
        }
        scores[i] = score;
        if (score > globalMaxScore) {
            globalMaxScore = score;
        }
    }

    // Peak threshold check per SPEC_05 Rev 2 §3.1: 0.10f * N * 255.0f (sensitive to small targets)
    const float minThreshold = 0.10f * N * 255.0f;
    if (globalMaxScore < minThreshold) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    // Find ridge center (scores within 95% of peak) closest to delta = 0
    float bestOffset = 0.0f;
    float minAbsOffset = std::numeric_limits<float>::max();
    bool foundRidge = false;

    int i = 0;
    while (i < totalSteps) {
        if (scores[i] >= 0.95f * globalMaxScore && scores[i] >= minThreshold) {
            int startIdx = i;
            while (i + 1 < totalSteps && scores[i + 1] >= 0.95f * globalMaxScore) {
                i++;
            }
            int endIdx = i;
            float ridgeOffset = 0.5f * (deltas[startIdx] + deltas[endIdx]);
            float absOffset = std::abs(ridgeOffset);
            if (absOffset < minAbsOffset) {
                minAbsOffset = absOffset;
                bestOffset = ridgeOffset;
                foundRidge = true;
            }
        }
        i++;
    }

    if (foundRidge) {
        return bestOffset;
    }

    return std::numeric_limits<float>::quiet_NaN();
}

std::vector<cv::Point2f> EdgeDetector::findContourAtPoint(const cv::Mat& grayMat, float touchX, float touchY) {
    std::vector<cv::Point2f> result;
    if (grayMat.empty() || touchX < 0 || touchX >= grayMat.cols || touchY < 0 || touchY >= grayMat.rows) {
        return result;
    }

    // 1. 降采样至适中尺寸进行快速轮廓匹配
    int maxDim = std::max(grayMat.cols, grayMat.rows);
    float scale = (maxDim > 640) ? 640.0f / maxDim : 1.0f;
    cv::Mat smallMat;
    if (scale < 1.0f) {
        cv::resize(grayMat, smallMat, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        smallMat = grayMat;
    }

    cv::Point2f scaledTouch(touchX * scale, touchY * scale);

    // 2. CLAHE + GaussianBlur + Sensitive Canny (25.0, 70.0) + Close
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
    cv::Mat claheMat;
    clahe->apply(smallMat, claheMat);

    cv::Mat blurred;
    cv::GaussianBlur(claheMat, blurred, cv::Size(5, 5), 1.0);

    cv::Mat edges;
    cv::Canny(blurred, edges, 25.0, 70.0);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(edges, edges, cv::MORPH_CLOSE, kernel);

    // 3. 提取轮廓
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    if (contours.empty()) return result;

    double frameArea = smallMat.cols * smallMat.rows;
    double minArea = 0.005 * frameArea;
    double maxArea = 0.98 * frameArea;

    std::vector<cv::Point> bestApprox;
    double bestScore = -1.0;

    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < minArea || area > maxArea) continue;

        cv::Rect bRect = cv::boundingRect(contour);
        // 快速包围盒外剔除
        if (scaledTouch.x < bRect.x - 5 || scaledTouch.x > bRect.x + bRect.width + 5 ||
            scaledTouch.y < bRect.y - 5 || scaledTouch.y > bRect.y + bRect.height + 5) {
            continue;
        }

        double perimeter = cv::arcLength(contour, true);
        std::vector<cv::Point> approx;

        for (double epsFactor : {0.02, 0.025, 0.03, 0.035, 0.04}) {
            cv::approxPolyDP(contour, approx, epsFactor * perimeter, true);
            if (approx.size() == 4) break;
        }

        if (approx.size() != 4) {
            std::vector<cv::Point> hull;
            cv::convexHull(contour, hull);
            double hullPerimeter = cv::arcLength(hull, true);
            for (double epsFactor : {0.02, 0.03, 0.04, 0.05}) {
                cv::approxPolyDP(hull, approx, epsFactor * hullPerimeter, true);
                if (approx.size() == 4) break;
            }
        }

        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            // 确保点击点在凸四边形内部或边界附近
            double dist = cv::pointPolygonTest(approx, scaledTouch, true);
            if (dist < -5.0) continue;

            // 内角校验
            bool validAngles = true;
            for (int i = 0; i < 4; ++i) {
                cv::Point p0 = approx[i];
                cv::Point p1 = approx[(i + 1) % 4];
                cv::Point p2 = approx[(i + 2) % 4];

                cv::Point2f v1(static_cast<float>(p0.x - p1.x), static_cast<float>(p0.y - p1.y));
                cv::Point2f v2(static_cast<float>(p2.x - p1.x), static_cast<float>(p2.y - p1.y));

                double dot = v1.x * v2.x + v1.y * v2.y;
                double norm = std::hypot(v1.x, v1.y) * std::hypot(v2.x, v2.y);
                if (norm > 0) {
                    double cosVal = dot / norm;
                    if (std::abs(cosVal) > 0.77) {
                        validAngles = false;
                        break;
                    }
                }
            }
            if (!validAngles) continue;

            cv::RotatedRect r = cv::minAreaRect(approx);
            float w = r.size.width;
            float h = r.size.height;
            if (w <= 0.0f || h <= 0.0f) continue;
            float ratio = std::max(w, h) / std::min(w, h);
            if (ratio > 12.0f) continue;

            double rectArea = w * h;
            double rectangularity = (rectArea > 0) ? (area / rectArea) : 0.0;
            if (rectangularity < 0.65) continue;

            // 优先匹配包含触摸点且形状最规整的目标
            double areaRatio = area / frameArea;
            double score = rectangularity / (std::sqrt(areaRatio) + 0.05);

            if (score > bestScore) {
                bestScore = score;
                bestApprox = approx;
            }
        }
    }

    if (bestScore > 0.0 && bestApprox.size() == 4) {
        std::vector<cv::Point2f> approxFloat;
        float invScale = (scale < 1.0f) ? (1.0f / scale) : 1.0f;
        for (const auto& pt : bestApprox) {
            approxFloat.emplace_back(pt.x * invScale, pt.y * invScale);
        }
        result = sortCorners(approxFloat);
    }

    return result;
}

EdgeDetector::StructuralLinesResult EdgeDetector::detectStructuralLines(const cv::Mat& grayImage, float origWidth, float origHeight) {
    StructuralLinesResult result;
    if (grayImage.empty()) {
        return result;
    }

    float origW = (origWidth > 0.0f) ? origWidth : static_cast<float>(grayImage.cols);
    float origH = (origHeight > 0.0f) ? origHeight : static_cast<float>(grayImage.rows);

    float maxDim = std::max(grayImage.cols, grayImage.rows);
    float scale = 1.0f;
    cv::Mat lsdInput;

    if (maxDim > 1024.0f) {
        scale = 1024.0f / maxDim;
        cv::resize(grayImage, lsdInput, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        lsdInput = grayImage;
    }

    cv::Ptr<cv::LineSegmentDetector> lsd = cv::createLineSegmentDetector(cv::LSD_REFINE_STD);
    std::vector<cv::Vec4f> detectedLines;
    lsd->detect(lsdInput, detectedLines);

    // 将 lsdInput 坐标系统直接映射至原图坐标系 (origW, origH)
    float scaleX = origW / static_cast<float>(lsdInput.cols);
    float scaleY = origH / static_cast<float>(lsdInput.rows);
    float minLength = 0.015f * std::min(origW, origH);

    for (const auto& line : detectedLines) {
        float x1 = line[0] * scaleX;
        float y1 = line[1] * scaleY;
        float x2 = line[2] * scaleX;
        float y2 = line[3] * scaleY;

        float dx = std::abs(x2 - x1);
        float dy = std::abs(y2 - y1);
        float len = std::hypot(dx, dy);

        if (len < minLength) {
            continue;
        }

        if (dx >= dy) {
            // Horizontal line (|dx| >= |dy|, angle <= 45 deg)
            result.horizontalLines.push_back(x1);
            result.horizontalLines.push_back(y1);
            result.horizontalLines.push_back(x2);
            result.horizontalLines.push_back(y2);
        } else {
            // Vertical line (|dy| > |dx|, angle > 45 deg)
            result.verticalLines.push_back(x1);
            result.verticalLines.push_back(y1);
            result.verticalLines.push_back(x2);
            result.verticalLines.push_back(y2);
        }
    }

    // Invariant: Append the 4 image boundaries (SPEC_06 §2.2)
    // Top border: [0, 0, W, 0]
    result.horizontalLines.push_back(0.0f);
    result.horizontalLines.push_back(0.0f);
    result.horizontalLines.push_back(origW);
    result.horizontalLines.push_back(0.0f);

    // Bottom border: [0, H, W, H]
    result.horizontalLines.push_back(0.0f);
    result.horizontalLines.push_back(origH);
    result.horizontalLines.push_back(origW);
    result.horizontalLines.push_back(origH);

    // Left border: [0, 0, 0, H]
    result.verticalLines.push_back(0.0f);
    result.verticalLines.push_back(0.0f);
    result.verticalLines.push_back(0.0f);
    result.verticalLines.push_back(origH);

    // Right border: [W, 0, W, H]
    result.verticalLines.push_back(origW);
    result.verticalLines.push_back(0.0f);
    result.verticalLines.push_back(origW);
    result.verticalLines.push_back(origH);

    return result;
}


