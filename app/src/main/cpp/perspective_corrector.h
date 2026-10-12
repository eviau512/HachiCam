#ifndef PERSPECTIVE_CORRECTOR_H
#define PERSPECTIVE_CORRECTOR_H

#include <opencv2/opencv.hpp>
#include <vector>

enum class FilterType {
    ORIGINAL = 0,
    MAGIC_COLOR = 1,
    PAPER = 2
};

class PerspectiveCorrector {
public:
    // 执行四点透视变换并拉正 (支持目标纸张比例与自由模式透视俯仰压缩恢复)
    cv::Mat correctPerspective(const cv::Mat& src, const std::vector<cv::Point2f>& srcCorners, float targetAspectRatio = 0.0f);

    // Retinex 照度除法 + Lab 色彩空间保护 (公章/印记保留)
    cv::Mat enhanceMagicColor(const cv::Mat& src);

    // Continuous-tone paper rendering with protected faint strokes and color.
    cv::Mat enhancePaper(const cv::Mat& src);

    // 统一处理接口：透视校正 + 滤镜增强一步到位
    cv::Mat processDocument(const cv::Mat& src, const std::vector<cv::Point2f>& srcCorners, FilterType filter, float targetAspectRatio = 0.0f);
};

#endif // PERSPECTIVE_CORRECTOR_H
