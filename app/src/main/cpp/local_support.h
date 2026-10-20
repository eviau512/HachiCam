#pragma once
#include "local_registration.h"
#include <cmath>
#include <string>
#include <array>
#include <opencv2/imgproc.hpp>

namespace fusion {
inline cv::Rect supportRegion(const LocalRegistration& model,cv::Size domain) {
    const cv::Rect whole({},domain);if(!model.patchSupport || model.field.empty())return whole;
    cv::Rect bounds;
    for(int y=0;y<model.field.rows;++y)for(int x=0;x<model.field.cols;++x)if(model.field.at<cv::Vec3f>(y,x)[2]>0) {
        const int cx=std::min(x*model.spacing,domain.width-1),cy=std::min(y*model.spacing,domain.height-1);
        const auto r=cv::Rect(cx-140,cy-140,281,281)&whole;
        bounds=bounds.empty() ? r : bounds|r;
    }
    return bounds;
}
inline cv::Mat geometricSupport(const LocalRegistration& model,const cv::Rect& bounds) {
    cv::Mat mask=cv::Mat::zeros(bounds.size(),CV_8U);
    std::vector<cv::Rect> footprints;std::vector<cv::Vec3f> nodes;
    for(int y=0;y<model.field.rows;++y)for(int x=0;x<model.field.cols;++x)if(model.field.at<cv::Vec3f>(y,x)[2]>0) {
        const int cx=std::min(x*model.spacing,model.domain.width-1),cy=std::min(y*model.spacing,model.domain.height-1);
        const auto r=cv::Rect(cx-140,cy-140,281,281)&bounds;
        if(r.empty())continue;footprints.push_back(r);nodes.push_back(model.field.at<cv::Vec3f>(y,x));
        mask(cv::Rect(r.tl()-bounds.tl(),r.size())).setTo(255);
    }
    for(size_t i=0;i<nodes.size();++i)for(size_t j=0;j<i;++j) {
        const auto r=footprints[i]&footprints[j];if(r.empty())continue;
        if(std::hypot(nodes[i][0]-nodes[j][0],nodes[i][1]-nodes[j][1])>1.5)
            mask(cv::Rect(r.tl()-bounds.tl(),r.size())).setTo(0);
    }
    return mask;
}
inline bool cannotRetainDetail(const cv::Mat& reference,const cv::Mat& refHP,const cv::Mat& srcHP,
    const cv::Mat& validity,const cv::Rect& roi,cv::Vec2d noise) {
    cv::Mat mask=(reference(roi)>8)&(reference(roi)<247);if(!validity.empty())mask &= validity(roi);
    mask.rowRange(0,10).setTo(0);mask.rowRange(mask.rows-10,mask.rows).setTo(0);
    mask.colRange(0,10).setTo(0);mask.colRange(mask.cols-10,mask.cols).setTo(0);
    const int n=cv::countNonZero(mask);if(n<256)return true;
    cv::Scalar mean,sd;cv::meanStdDev(refHP(roi),mean,sd,mask);
    const double va=std::max(0.,sd[0]*sd[0]-noise[0]);
    // A bilinear translate of the 10px interior by <=8px samples only this
    // patch. Convexity bounds its masked squared energy by full-patch energy.
    const double energy=cv::norm(srcHP(roi),cv::NORM_L2SQR)/n;
    const double upper=std::max(0.,energy*1.0001+1e-4-noise[1]);
    return std::isfinite(upper) && upper<(.68-.002)*(.68-.002)*std::max(va,.01);
}
struct PhysicalPatch {cv::Rect roi;cv::Point2f shift;cv::Vec2d noise;};
inline cv::Vec2d physicalMetrics(double count,double va,double vb,double covariance,cv::Vec2d noise) {
    if(count<256)return {-1,0};
    va=std::max(0.,va-noise[0]);vb=std::max(0.,vb-noise[1]);
    return {covariance/std::max(std::sqrt(va*vb),.01),std::sqrt(vb/std::max(va,.01))};
}
inline cv::Vec2d cpuPhysicalCheck(const cv::Mat& reference,const cv::Mat& refHP,const cv::Mat& srcHP,
    const cv::Mat& validity,const PhysicalPatch& p) {
    cv::Mat corrected,transform=(cv::Mat_<float>(2,3)<<1,0,p.shift.x,0,1,p.shift.y);
    cv::warpAffine(srcHP(p.roi),corrected,transform,p.roi.size(),cv::INTER_LINEAR|cv::WARP_INVERSE_MAP,cv::BORDER_REFLECT_101);
    cv::Mat mask=(reference(p.roi)>8)&(reference(p.roi)<247);
    if(!validity.empty())mask &= validity(p.roi);
    mask.rowRange(0,10).setTo(0);mask.rowRange(mask.rows-10,mask.rows).setTo(0);
    mask.colRange(0,10).setTo(0);mask.colRange(mask.cols-10,mask.cols).setTo(0);
    const int count=cv::countNonZero(mask);if(count<256)return {-1,0};
    const auto a=refHP(p.roi);cv::Scalar ma,sa,mb,sb;
    cv::meanStdDev(a,ma,sa,mask);cv::meanStdDev(corrected,mb,sb,mask);
    return physicalMetrics(count,sa[0]*sa[0],sb[0]*sb[0],cv::mean(a.mul(corrected),mask)[0]-ma[0]*mb[0],p.noise);
}
struct GpuPhysicalStats {int patches=0,cpuPatches=0;double ms=0;};
bool gpuPhysicalChecks(const cv::Mat& reference,const cv::Mat& refHP,const cv::Mat& srcHP,
    const cv::Mat& validity,const std::vector<PhysicalPatch>& patches,bool highlight,
    std::vector<cv::Vec2d>& metrics,GpuPhysicalStats& stats,std::string& reason);
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
bool gpuTestQuantizedPhysical(const cv::Mat&,const cv::Mat&,const cv::Mat&,const cv::Mat&,const std::vector<PhysicalPatch>&,std::vector<cv::Vec2d>&);
bool gpuTestCountLimitedFits(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& raw,const cv::Mat& validity,
    const std::vector<cv::Rect>& patches,int iterations,std::vector<cv::Vec4f>& fits,int& bounded);
struct GpuTestState { bool healthy=false,inFlight=false; unsigned long long allocation=0; };
GpuTestState gpuTestState();
bool gpuTestPreparedPlanes(const cv::Mat& ref,const cv::Mat& src,const cv::Mat& raw,
    const cv::Mat& validity,const std::vector<cv::Rect>& patches,std::vector<std::array<cv::Mat,5>>& planes);
#endif
}
