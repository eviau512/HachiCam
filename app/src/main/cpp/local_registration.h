#pragma once
#include <opencv2/core.hpp>

namespace fusion {
struct LocalRegistration {
    cv::Mat field; // CV_32FC3: reference -> globally warped source dx,dy,confidence
    cv::Mat gate;  // CV_8U, source-resolution local photometric support
    int spacing = 256;
    float supportedFraction = 0;
    float detailFraction = 0;
    float maxOffset = 0;
    bool offset(double x, double y, cv::Point2d& delta, float& confidence) const;
};
LocalRegistration registerLocal(const cv::Mat& referenceGray, const cv::Mat& warpedGray, bool highlight = false);
double referenceQuality(const cv::Mat& image);
}
