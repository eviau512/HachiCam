#pragma once
#include "fusion_math.h"
#include "local_registration.h"

namespace fusion {
inline bool reconstructionFits(cv::Size source, cv::Rect crop) {
    return source.width > 0 && source.height > 0 && crop.width > 0 && crop.height > 0 &&
           static_cast<long long>(source.width) * source.height <= 16000000 &&
           static_cast<long long>(crop.width) * crop.height * 4 <= 56000000;
}
struct SamplingPlate {
    const LocalRegistration *local;
    cv::Matx33d inverse;
    cv::Size size;
    const cv::Mat *image = nullptr;
};
struct RegionalPlan {
    static constexpr int tileSize = 64, minimumTiles = 4;
    static constexpr double minimumArea = .02, minimumSupport = .70;
    cv::Mat candidates; // byte bitmask of qualified plate indices, including reference 0
    cv::Rect crop;
    int qualified = 0, total = 0;
    double fraction = 0;
    bool eligible() const { return qualified >= minimumTiles && fraction >= minimumArea; }
    unsigned at(double x, double y) const;
};
bool detailMapping(const SamplingPlate &plate, double x, double y, cv::Point2d &point, float &confidence);
RegionalPlan qualifyRegions(const std::vector<SamplingPlate> &plates, const cv::Rect &crop);
struct RefinementResult {
    long long pixels = 0;
    int fittedTiles = 0;
    double initialError = 0, finalError = 0;
};
RefinementResult refineRegions(const std::vector<SamplingPlate> &plates, const RegionalPlan &plan,
                               cv::Mat &output, const cv::Mat &preserveNative);
} // namespace fusion
