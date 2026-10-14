#include "local_support.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>
#include <iostream>
#include <stdexcept>

void runGpuFittingTests() {
    cv::Mat reference(383,511,CV_8U);cv::RNG rng(53919);
    rng.fill(reference,cv::RNG::UNIFORM,40,200);cv::GaussianBlur(reference,reference,{0,0},1.2);
    const std::vector<cv::Rect> patches{{0,0,321,321},{160,62,321,321},{33,18,257,193}};
    int accepted=0;
    for(auto shift:{cv::Point2f(0,0),cv::Point2f(.25f,-.125f),cv::Point2f(.7f,.4f),cv::Point2f(2.25f,-1.5f)}) {
        cv::Mat source;const cv::Mat h=(cv::Mat_<float>(2,3)<<1,0,shift.x,0,1,shift.y);
        cv::warpAffine(reference,source,h,reference.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
        cv::Mat validity(reference.size(),CV_8U,cv::Scalar(255));
        cv::rectangle(validity,{200,180,37,43},cv::Scalar(0),cv::FILLED);
        std::vector<cv::Vec4f> got;std::string reason;
        if(!fusion::gpuPatchFits(reference,source,source,validity,patches,false,got,reason))
            throw std::runtime_error("GPU fitting not executed: "+reason);
        for(size_t i=0;i<patches.size();++i) {
            cv::Mat transform=cv::Mat::eye(2,3,CV_32F);
            cv::Mat mask=fusion::localFitMask(source,validity,patches[i]);
            const double cc=cv::findTransformECC(reference(patches[i]),source(patches[i]),transform,cv::MOTION_TRANSLATION,
                cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.0001),mask,3);
            const double disagreement=std::hypot(got[i][0]-transform.at<float>(0,2),got[i][1]-transform.at<float>(1,2));
            std::cout<<"GPU ECC shift="<<shift.x<<","<<shift.y<<" patch="<<i<<" gpu="<<got[i]<<" cpu="<<transform.at<float>(0,2)<<","<<transform.at<float>(1,2)<<" cc="<<cc<<" difference="<<disagreement<<"\n";
            if(got[i][3]!=1 || disagreement>.05 || std::abs(got[i][2]-cc)>.002)throw std::runtime_error("GPU fitting discrepancy");
            ++accepted;
        }
    }
    cv::Mat flat(321,321,CV_8U,cv::Scalar(80));std::vector<cv::Vec4f> got;std::string reason;
    if(!fusion::gpuPatchFits(flat,flat,flat,cv::Mat(),{{0,0,321,321}},false,got,reason))throw std::runtime_error("Flat solver not executed");
    if(got[0][3])throw std::runtime_error("Flat patch falsely solved");
    if(fusion::gpuPatchFits(flat,flat,flat,cv::Mat(),{{INT_MAX,0,321,321}},false,got,reason))throw std::runtime_error("Overflowing ROI accepted");
    std::cout<<"GPU ECC usable comparisons="<<accepted<<"; flat fallback PASS\n";
}
