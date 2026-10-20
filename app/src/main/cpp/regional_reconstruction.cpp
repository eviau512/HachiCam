#include "regional_reconstruction.h"

namespace fusion {
unsigned RegionalPlan::at(double x, double y) const {
    if (candidates.empty() || x < crop.x || y < crop.y || x >= crop.br().x || y >= crop.br().y)
        return 0;
    return candidates.at<uchar>(std::min(int((y - crop.y) / tileSize), candidates.rows - 1),
                                std::min(int((x - crop.x) / tileSize), candidates.cols - 1));
}
bool detailMapping(const SamplingPlate &plate, double x, double y, cv::Point2d &point, float &confidence) {
    if (!plate.local || !plate.local->hasDetail(x, y))
        return false;
    cv::Point2d delta;
    if (!plate.local->offset(x, y, delta, confidence))
        return false;
    const auto p = plate.inverse * cv::Vec3d(x + delta.x, y + delta.y, 1);
    if (!std::isfinite(p[2]) || std::abs(p[2]) < 1e-9)
        return false;
    point = {p[0] / p[2], p[1] / p[2]};
    return std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0 && point.y >= 0 &&
           point.x <= plate.size.width - 1 && point.y <= plate.size.height - 1;
}
RegionalPlan qualifyRegions(const std::vector<SamplingPlate> &plates, const cv::Rect &crop) {
    RegionalPlan result;
    result.crop = crop;
    if (plates.size() < 3 || plates.size() > 7 || crop.width < 32 || crop.height < 32)
        return result;
    result.candidates = cv::Mat::zeros((crop.height + 63) / 64, (crop.width + 63) / 64, CV_8U);
    result.total = result.candidates.rows * result.candidates.cols;
    long long eligibleArea = 0;
    for (int ty = 0; ty < result.candidates.rows; ++ty)
        for (int tx = 0; tx < result.candidates.cols; ++tx) {
            const cv::Rect tile(crop.x + tx * 64, crop.y + ty * 64, std::min(64, crop.width - tx * 64),
                                std::min(64, crop.height - ty * 64));
            if (tile.width < 32 || tile.height < 32)
                continue;
            unsigned candidates = 1;
            for (size_t i = 1; i < plates.size(); ++i) {
                const auto &plate = plates[i];
                if (!plate.local || plate.local->gate.empty() ||
                    double(cv::countNonZero(plate.local->gate(tile))) / tile.area() <
                        RegionalPlan::minimumSupport)
                    continue;
                bool valid = true;
                for (double fy : {.2, .5, .8})
                    for (double fx : {.2, .5, .8}) {
                        cv::Point2d p;
                        float confidence;
                        if (!detailMapping(plate, tile.x + fx * (tile.width - 1),
                                           tile.y + fy * (tile.height - 1), p, confidence))
                            valid = false;
                    }
                if (valid)
                    candidates |= 1u << i;
            }
            bool diverse = true;
            for (double fy : {.2, .5, .8})
                for (double fx : {.2, .5, .8}) {
                    const double x = tile.x + fx * (tile.width - 1), y = tile.y + fy * (tile.height - 1);
                    std::vector<std::array<double, 2>> phases;
                    for (size_t i = 1; i < plates.size(); ++i)
                        if (candidates & (1u << i)) {
                            cv::Point2d p;
                            float confidence;
                            if (detailMapping(plates[i], x, y, p, confidence)) {
                                const double dx = p.x - x, dy = p.y - y;
                                phases.push_back({dx - std::round(dx), dy - std::round(dy)});
                            }
                        }
                    if (!phasesCover2D(phases))
                        diverse = false;
                }
            if (diverse) {
                result.candidates.at<uchar>(ty, tx) = candidates;
                ++result.qualified;
                eligibleArea += tile.area();
            }
        }
    result.fraction = double(eligibleArea) / crop.area();
    return result;
}
} // namespace fusion
