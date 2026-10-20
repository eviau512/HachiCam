#pragma once
#include "regional_reconstruction.h"
#include "regional_solver.h"
#include "regional_mapping.h"
#include <string>

namespace fusion {
void resetGpuDiagnostics();
void recordFusionDiagnostic(const std::string& line);
std::string gpuDiagnostics();
struct GpuGlobalStats {int calls=0,iterations=0;double ms=0;};
GpuGlobalStats gpuGlobalStats();
bool gpuRegionalSolve(const cv::Mat& initial,const std::vector<RegionalObservation>& observations,
                      RegionalSolve& result,bool& usedGpu,std::string& reason);
bool gpuObservationMapping(const SamplingPlate& plate,const cv::Rect& source,const cv::Rect& patch,
                           std::vector<MappedObservation>& result,bool& usedGpu,int& completed,std::string& reason);
bool gpuGlobalECC(const cv::Mat& reference,const cv::Mat& source,const cv::Mat& initial,
    int model,cv::TermCriteria criteria,double sourceScale,cv::Mat& result,double& correlation,std::string& reason);
struct GpuComputeStats {
    int mergeTiles=0;
    long long cpuMergePixels=0;
    double mergeMs=0;
};
bool gpuNoiseGrid(const cv::Mat& gray,int spacing,const cv::Mat& active,
                  cv::Mat& sigma,std::string& reason);
bool gpuMergeAnchors(const std::vector<SamplingPlate>& plates,const cv::Mat& ref,
    const cv::Rect& crop,cv::Mat& output,long long& contributors,long long& multi,
    GpuComputeStats& stats,std::string& reason);
// Shared unchanged CPU baseline, also used for ambiguous GPU pixels.
inline cv::Vec3b mergePixel(const std::vector<SamplingPlate>& plates,const cv::Mat& ref,
                           int x,int y,int& contributors) {
    Color base,sum{};const auto original=ref.at<cv::Vec3b>(y,x);
    for(int c=0;c<3;++c)base[c]=original[c];
    float weights=0;contributors=0;
    for(const auto& p:plates) {
        if(!p.local->gate.empty() && !p.local->gate.at<uchar>(y,x))continue;
        cv::Point2d delta;float confidence;
        if(!p.local->offset(x,y,delta,confidence))continue;
        const cv::Vec3d q=p.inverse*cv::Vec3d(x+delta.x,y+delta.y,1.);
        if(!std::isfinite(q[2]) || std::abs(q[2])<1e-9)continue;
        const double sx=q[0]/q[2],sy=q[1]/q[2];
        if(!std::isfinite(sx)||!std::isfinite(sy)||sx<0||sy<0||sx>p.size.width-1||sy>p.size.height-1)continue;
        const int ix=int(sx),iy=int(sy),nx=std::min(ix+1,p.size.width-1),ny=std::min(iy+1,p.size.height-1);
        const float fx=sx-ix,fy=sy-iy;
        for(int c=0;c<3;++c) {
            const float color=(1-fy)*((1-fx)*p.image->at<cv::Vec3b>(iy,ix)[c]+fx*p.image->at<cv::Vec3b>(iy,nx)[c])+
                fy*((1-fx)*p.image->at<cv::Vec3b>(ny,ix)[c]+fx*p.image->at<cv::Vec3b>(ny,nx)[c]);
            sum[c]+=color*confidence;
        }
        weights+=confidence;++contributors;
    }
    if(weights>1e-5f)for(int c=0;c<3;++c)base[c]=sum[c]/weights;
    return {cv::saturate_cast<uchar>(base[0]),cv::saturate_cast<uchar>(base[1]),cv::saturate_cast<uchar>(base[2])};
}
}
