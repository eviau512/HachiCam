#include "local_support.h"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace fusion;
void runGpuFittingTests();
void runGpuComputeTests();
static void compare(const cv::Mat& ref,const cv::Mat& src,const LocalRegistration& m,const cv::Mat& validity={}) {
    cv::Mat actual;std::string reason;
    if(!gpuLocalSupport(ref,src,m,actual,reason,validity))throw std::runtime_error("GPU not executed: "+reason);
    size_t mismatch=0;
    for(int y=0;y<ref.rows;++y)for(int x=0;x<ref.cols;++x)
        mismatch+=actual.at<uchar>(y,x)!=supportPixel(ref,src,m,x,y,m.patchSupport,validity);
    if(mismatch)throw std::runtime_error("gate mismatch count="+std::to_string(mismatch));
}
int main() {
    try {
        cv::RNG rng(73919);
        for(auto size:{cv::Size(65,37),cv::Size(321,321),cv::Size(1027,769),cv::Size(4096,3072)}) {
            // Non-contiguous gray ROIs exercise source row-stride handling.
            cv::Mat a(size.height+2,size.width+11,CV_8U),b(a.size(),a.type());
            rng.fill(a,cv::RNG::UNIFORM,0,256);rng.fill(b,cv::RNG::UNIFORM,0,256);
            const auto ref=a(cv::Rect(3,1,size.width,size.height));const auto src=b(cv::Rect(5,1,size.width,size.height));
            LocalRegistration m;m.domain=size;m.spacing=256;
            m.field=cv::Mat((size.height-1+255)/256+1,(size.width-1+255)/256+1,CV_32FC3,cv::Scalar(.25,-.125,.9));
            compare(ref,src,m);
            m.field.at<cv::Vec3f>(0,1)={1.75f,-.125f,.9f};compare(ref,src,m);
            m.field.at<cv::Vec3f>(0,1)={1.75001f,-.125f,.9f};compare(ref,src,m);
            m.field.at<cv::Vec3f>(1,0)={0,0,0};compare(ref,src,m);
            m.patchSupport=true;
            cv::Mat validity(size,CV_8U,cv::Scalar(255));validity(cv::Rect(0,0,size.width/2,size.height)).setTo(0);
            compare(ref,src,m,validity);
            m.field.at<cv::Vec3f>(1,0)={-7.9f,7.9f,.9};compare(ref,src,m,validity);
            std::cout<<"GPU gate parity: "<<size.width<<"x"<<size.height<<" padded stride, subpixel, discontinuity, missing nodes PASS\n";
        }
        cv::Mat reference(257,513,CV_8U,cv::Scalar(100)),source(reference.size(),reference.type(),cv::Scalar(114));
        LocalRegistration m;m.domain=reference.size();m.field=cv::Mat(3,4,CV_32FC3,cv::Scalar(0,0,.9));
        compare(reference,source,m);source.setTo(115);compare(reference,source,m);
        m.field.at<cv::Vec3f>(0,0)={-7.9f,7.9f,.9f};compare(reference,source,m);
        std::string reason;cv::Mat output;
        m.patchSupport=true;
        compare(reference,source,m);
        m.patchSupport=false;m.field.at<cv::Vec3f>(0,0)[0]=NAN;
        if(gpuLocalSupport(reference,source,m,output,reason))throw std::runtime_error("Non-finite field accepted");
        std::cout<<"GPU threshold/boundary and unsupported-workload tests PASS\n";
        runGpuFittingTests();
        runGpuComputeTests();
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
