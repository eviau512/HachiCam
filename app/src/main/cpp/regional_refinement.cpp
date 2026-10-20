#include "regional_reconstruction.h"
#include "gpu_compute.h"
#include <set>
#include <chrono>
#include <cstdio>
#include <limits>
#include <opencv2/imgproc.hpp>

namespace fusion {
namespace {
cv::Point2d project(const cv::Matx33d &h, double x, double y) {
    const auto p = h * cv::Vec3d(x, y, 1);
    if (!std::isfinite(p[2]) || std::abs(p[2]) < 1e-9)
        return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    return {p[0] / p[2], p[1] / p[2]};
}
bool fitPatch(const std::vector<SamplingPlate> &plates, unsigned mask, const cv::Rect &patch,
              cv::Mat &estimate, double &before, double &after, bool& usedGpu, std::string& solverReason,
              double& observationMs,double& solverMs,int& gpuMapJobs,int& cpuMapJobs,int& mapCompletions) {
    const auto started=std::chrono::steady_clock::now();
    auto elapsed=[&]{return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();};
    cv::resize((*plates[0].image)(patch), estimate, {patch.width * 2, patch.height * 2}, 0, 0,
               cv::INTER_CUBIC);
    estimate.convertTo(estimate, CV_32FC3);
    std::vector<RegionalObservation> observations;
    observations.reserve(std::min(size_t(50000),size_t(patch.area())*plates.size()*2));
    int observedSources = 0;std::string mappingNote;
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
            const cv::Rect sourceRoi(left,top,right-left+1,bottom-top+1);
            std::vector<MappedObservation> mapped;bool usedMapping=false;int completed=0;std::string mappingReason;
            if(!gpuObservationMapping(plate,sourceRoi,patch,mapped,usedMapping,completed,mappingReason))mapped.clear();
            if(usedMapping){++gpuMapJobs;mapCompletions+=completed;}else ++cpuMapJobs;
            if(mappingNote.empty()&&plate.local&&!plate.local->field.empty())mappingNote=mappingReason;
            const auto startCount = observations.size();
            for (int sy = top; sy <= bottom; ++sy)
                for (int sx = left; sx <= right; ++sx) {
                    const auto pixel = plate.image->at<cv::Vec3b>(sy, sx);
                    if (std::max({pixel[0], pixel[1], pixel[2]}) >= 247)
                        continue;
                    RegionalObservation o{};
                    o.color = cv::Vec3f(pixel[0], pixel[1], pixel[2]);
                    const auto mapping=mapped.empty()?mapRegionalObservationCpu(plate,forward,patch,sx,sy,true):mapped[(sy-top)*sourceRoi.width+sx-left];
                    if(!mapping.valid)continue;
                    int k=0;
                    for(const auto& r:mapping.local) {
                        const double x=2*r.x+.5,y=2*r.y+.5;
                        const int ix=int(x),iy=int(y);const float fx=x-ix,fy=y-iy;
                        for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx) {
                            o.index[k]=(iy+yy)*estimate.cols+ix+xx;
                            o.weight[k++]=.25f*(xx?fx:1-fx)*(yy?fy:1-fy);
                        }
                    }
                    if (k == 16) {
                        observations.push_back(o);
                        if (observations.size() > 50000){observationMs=elapsed();return false;}
                    }
                }
            if (observations.size() - startCount >= 256)
                ++observedSources;
        }
    observationMs=elapsed();
    if (observedSources < 3)
        return false;
    RegionalSolve solved;
    if(!gpuRegionalSolve(estimate,observations,solved,usedGpu,solverReason))
        solved=solveRegionalCpu(estimate,observations);
    solverMs=elapsed()-observationMs;
    if(!mappingNote.empty())solverReason=mappingNote+"; "+solverReason;
    before=solved.before;after=solved.after;estimate=std::move(solved.image);
    return solved.accepted;
}
} // namespace
RefinementResult refineRegions(const std::vector<SamplingPlate> &plates, const RegionalPlan &plan,
                               cv::Mat &output, const cv::Mat &preserveNative) {
    RefinementResult result;
    const auto started=std::chrono::steady_clock::now();
    long long pixels = 0;
    int fitted = 0,gpuSolvers=0,cpuSolvers=0,gpuMapJobs=0,cpuMapJobs=0,mapCompletions=0;
    std::vector<std::string> solverNotes(plan.candidates.total());
    double initialError = 0, finalError = 0, observationWorkerMs=0,solverWorkerMs=0;
#pragma omp parallel for schedule(dynamic) reduction(+ : pixels, fitted, initialError, finalError, gpuSolvers, cpuSolvers, observationWorkerMs, solverWorkerMs, gpuMapJobs, cpuMapJobs, mapCompletions)
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
                bool usedGpu=false;std::string reason;double observationMs=0,solverMs=0;
                const bool accepted=fitPatch(plates,mask,patch,estimate,before,after,usedGpu,reason,observationMs,solverMs,gpuMapJobs,cpuMapJobs,mapCompletions);
                observationWorkerMs+=observationMs;solverWorkerMs+=solverMs;
                if(!reason.empty()) {solverNotes[ty*plan.candidates.cols+tx]=reason;if(usedGpu)++gpuSolvers;else ++cpuSolvers;}
                if(!accepted)continue;
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
    char timing[384];std::snprintf(timing,sizeof(timing),"regional-solver-v1 gpuPatches=%d cpuPatches=%d gpuMapJobs=%d cpuMapJobs=%d mapCpuCompletions=%d wallMs=%.3f sumWorkerObservationMs=%.3f sumWorkerSolverMs=%.3f",gpuSolvers,cpuSolvers,gpuMapJobs,cpuMapJobs,mapCompletions,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count(),observationWorkerMs,solverWorkerMs);
    recordFusionDiagnostic(timing);
    std::set<std::string> notes;
    for(const auto& note:solverNotes)if(!note.empty()&&notes.size()<8)notes.insert(note);
    for(const auto& note:notes)recordFusionDiagnostic("regional-solver "+note);
    result.pixels = pixels;
    result.fittedTiles = fitted;
    result.initialError = initialError / std::max(1, fitted);
    result.finalError = finalError / std::max(1, fitted);
    return result;
}
} // namespace fusion
