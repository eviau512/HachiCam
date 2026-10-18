#pragma once
#include "local_registration.h"
#include <cmath>
#include <string>
#include <array>

namespace fusion {
inline cv::Mat localFitMask(const cv::Mat& raw,const cv::Mat& validity,const cv::Rect& patch) {
    cv::Mat mask=(raw(patch)>8)&(raw(patch)<247);
    if(!validity.empty())mask &= validity(patch);
    if(mask.rows<32 || mask.cols<32){mask.setTo(0);return mask;}
    mask.rowRange(0,10).setTo(0);mask.rowRange(mask.rows-10,mask.rows).setTo(0);
    mask.colRange(0,10).setTo(0);mask.colRange(mask.cols-10,mask.cols).setTo(0);
    return mask;
}
inline bool strongPatchEligible(const cv::Mat& reference,const cv::Mat& source,
    const cv::Mat& refHP,const cv::Mat& srcHP,const cv::Mat& validity,const cv::Rect& patch,cv::Vec2d noise) {
    cv::Mat mask=localFitMask(source,validity,patch);
    mask &= (reference(patch)>8)&(reference(patch)<247);
    if(cv::countNonZero(mask)<256)return false;
    cv::Scalar ma,sa,mb,sb;cv::meanStdDev(refHP(patch),ma,sa,mask);cv::meanStdDev(srcHP(patch),mb,sb,mask);
    const double va=std::max(0.,sa[0]*sa[0]-noise[0]),vb=std::max(0.,sb[0]*sb[0]-noise[1]);
    const double ratio=std::sqrt(vb/std::max(va,.01));
    const double score=(cv::mean(refHP(patch).mul(srcHP(patch)),mask)[0]-ma[0]*mb[0])/std::max(std::sqrt(va*vb),.01);
    return score>=.90 && ratio>=.68;
}
struct GpuSelectionStats {int patches=0,cpuPatches=0;double ms=0,estimateMs=0;};
bool gpuStrongSelection(const cv::Mat& reference,const cv::Mat& source,const cv::Mat& refHP,const cv::Mat& srcHP,
    const cv::Mat& validity,const std::vector<cv::Rect>& patches,const std::vector<cv::Vec2d>& noise,
    std::vector<uchar>& eligible,GpuSelectionStats& stats,std::string& reason);
inline unsigned char supportPixel(const cv::Mat& ref, const cv::Mat& src,
    const LocalRegistration& model, int x, int y, bool highlight,
    const cv::Mat& validity = cv::Mat()) {
    cv::Point2d d; float confidence;
    if (!model.offset(x, y, d, confidence)) return 0;
    const double sx=x+d.x, sy=y+d.y;
    if (!std::isfinite(sx) || !std::isfinite(sy) || sx<0 || sy<0 ||
        sx>src.cols-1 || sy>src.rows-1) return 0;
    const int ix=int(sx), iy=int(sy), nx=std::min(ix+1,src.cols-1), ny=std::min(iy+1,src.rows-1);
    const float fx=sx-ix, fy=sy-iy;
    const float observed=(1-fy)*((1-fx)*src.at<uchar>(iy,ix)+fx*src.at<uchar>(iy,nx))+
        fy*((1-fx)*src.at<uchar>(ny,ix)+fx*src.at<uchar>(ny,nx));
    const bool clipped=highlight && !validity.empty() && !validity.at<uchar>(y,x);
    return clipped || std::abs(observed-ref.at<uchar>(y,x)) <= (highlight ? 18 : 14) ? 255 : 0;
}
// Resolved GPU gates use the same byte semantics as the CPU gate.
bool gpuLocalSupport(const cv::Mat& ref, const cv::Mat& src, const LocalRegistration& model,
                     cv::Mat& output, std::string& reason,const cv::Mat& validity=cv::Mat());
bool gpuPatchFits(const cv::Mat& refAlign,const cv::Mat& srcAlign,const cv::Mat& rawSrc,
    const cv::Mat& validity,const std::vector<cv::Rect>& patches,bool highlight,
    std::vector<cv::Vec4f>& fits,std::string& reason,double selectionMs=0);
bool gpuFittingAvailable(size_t sourcePixels,bool highlight);
#ifdef HACHICAM_GPU_TEST_CONTROL
bool gpuTestCountLimitedFits(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& raw,const cv::Mat& validity,
    const std::vector<cv::Rect>& patches,int iterations,std::vector<cv::Vec4f>& fits,int& bounded);
struct GpuTestState { bool healthy=false,inFlight=false; unsigned long long allocation=0; };
GpuTestState gpuTestState();
bool gpuTestPreparedPlanes(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& raw,
    const cv::Mat& validity,const std::vector<cv::Rect>& patches,std::vector<std::array<cv::Mat,5>>& planes);
#endif
}
