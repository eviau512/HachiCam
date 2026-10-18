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
    {
        cv::Mat source;const cv::Mat h=(cv::Mat_<float>(2,3)<<1,0,.7f,0,1,.4f);
        cv::warpAffine(reference,source,h,reference.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
        for(int limit:{1,2}) {
            std::vector<cv::Vec4f> got;int bounded=0;
            if(!fusion::gpuTestCountLimitedFits(reference,source,source,{},patches,limit,got,bounded) || !bounded)
                throw std::runtime_error("No actual bounded-count GPU return");
            for(size_t i=0;i<patches.size();++i) {
                cv::Mat transform=cv::Mat::eye(2,3,CV_32F);
                const double cc=cv::findTransformECC(reference(patches[i]),source(patches[i]),transform,cv::MOTION_TRANSLATION,
                    cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,limit,.0001),fusion::localFitMask(source,{},patches[i]),3);
                if(got[i][3]!=1 || std::hypot(got[i][0]-transform.at<float>(0,2),got[i][1]-transform.at<float>(1,2))>.05 || std::abs(got[i][2]-cc)>.002)
                    throw std::runtime_error("Bounded-count final update/correlation mismatch");
            }
        }
        std::cout<<"Actual GPU COUNT-bound final-update return parity PASS\n";
    }
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
    {
        cv::Mat source;const cv::Mat h=(cv::Mat_<float>(2,3)<<1,0,.25f,0,1,-.125f);
        cv::warpAffine(reference,source,h,reference.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
        std::vector<cv::Rect> mixed;
        for(int i=0;i<16;++i)mixed.emplace_back(i*3,i*2,321-(i%4)*16,321-(i%3)*17);
        cv::Mat valid(reference.size(),CV_8U,cv::Scalar(255));
        valid(cv::Rect(180,170,15,13)).setTo(0);
        std::vector<std::array<cv::Mat,5>> prepared;
        if(!fusion::gpuTestPreparedPlanes(reference,source,source,valid,mixed,prepared))
            throw std::runtime_error("GPU preprocessing inspection failed");
        for(size_t i=0;i<mixed.size();++i) {
            cv::Mat t,s,m,mask,gx,gy;
            reference(mixed[i]).convertTo(t,CV_32F);source(mixed[i]).convertTo(s,CV_32F);
            cv::GaussianBlur(t,t,{3,3},0);cv::GaussianBlur(s,s,{3,3},0);
            mask=fusion::localFitMask(source,valid,mixed[i]);
            mask.convertTo(m,CV_32F,1./255);cv::GaussianBlur(m,m,{3,3},0);m*=.5/.95;
            m.convertTo(mask,CV_8U);mask.convertTo(m,CV_32F);
            const cv::Matx13f derivative(-.5f,0,.5f);
            cv::filter2D(s,gx,-1,derivative);cv::filter2D(s,gy,-1,derivative.t());gx=gx.mul(m);gy=gy.mul(m);
            const std::array<cv::Mat,5> expected{t,s,gx,gy,m};
            for(int plane=0;plane<5;++plane)if(cv::norm(prepared[i][plane],expected[plane],cv::NORM_INF)!=0)
                throw std::runtime_error("GPU preprocessing must exactly match CPU float planes");
        }
        std::cout<<"GPU actual five-plane preparation parity PASS\n";
        if(!fusion::gpuPatchFits(reference,source,source,cv::Mat(),mixed,false,got,reason))
            throw std::runtime_error("Mixed tiled batch did not execute: "+reason);
        if(got.size()!=16)throw std::runtime_error("Mixed tiled result count");
        for(size_t i=0;i<mixed.size();++i) {
            cv::Mat transform=cv::Mat::eye(2,3,CV_32F);
            const double cc=cv::findTransformECC(reference(mixed[i]),source(mixed[i]),transform,cv::MOTION_TRANSLATION,
                cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.0001),fusion::localFitMask(source,cv::Mat(),mixed[i]),3);
            if(got[i][3]!=1 || std::hypot(got[i][0]-transform.at<float>(0,2),got[i][1]-transform.at<float>(1,2))>.05 || std::abs(got[i][2]-cc)>.002)
                throw std::runtime_error("Mixed non-square/chunk-edge tiled fit discrepancy");
        }
        if(!fusion::gpuPatchFits(reference,source,source,cv::Mat(),{{0,0,32,32}},false,got,reason) || got[0][3])
            throw std::runtime_error("Insufficient original mask must fall back");
        std::cout<<"GPU tiled mixed 16-patch, partial chunks and insufficient-mask checks PASS\n";
    }
    if(!fusion::gpuPatchFits(flat,flat,flat,cv::Mat(),{{0,0,321,321}},false,got,reason))throw std::runtime_error("Flat solver not executed");
    if(got[0][3])throw std::runtime_error("Flat patch falsely solved");
    if(fusion::gpuPatchFits(flat,flat,flat,cv::Mat(),{{INT_MAX,0,321,321}},false,got,reason))throw std::runtime_error("Overflowing ROI accepted");
    std::cout<<"GPU ECC usable comparisons="<<accepted<<"; flat fallback PASS\n";
}
