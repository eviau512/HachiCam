#include "gpu_compute.h"
#include "local_support.h"
#include <opencv2/video.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>
int main(){try{
    cv::setNumThreads(4);cv::RNG rng(391822);cv::Mat storage(139,202,CV_8U);
    rng.fill(storage,cv::RNG::UNIFORM,30,220);cv::GaussianBlur(storage,storage,{0,0},1.3);
    const cv::Mat reference=storage(cv::Rect(3,1,193,137));
    const cv::Mat truth=(cv::Mat_<float>(3,3)<<1,0,.25,0,1,-.375,0,0,1);cv::Mat source;
    cv::warpPerspective(reference,source,truth,reference.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
    const cv::Mat initial=cv::Mat::eye(3,3,CV_32F);const cv::TermCriteria criteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,40,1e-5);
    cv::Mat result,cpu=initial.clone();double cc=-1;std::string reason;fusion::resetGpuDiagnostics();
    if(!fusion::gpuGlobalECC(reference,source,initial,cv::MOTION_HOMOGRAPHY,criteria,.5,result,cc,reason))throw std::runtime_error("Real global calibration did not reuse CPU output: "+reason);
    const double expected=cv::findTransformECC(reference,source,cpu,cv::MOTION_HOMOGRAPHY,criteria);
    const auto trace=fusion::gpuDiagnostics();
    if(trace.find("numeric-block")==std::string::npos||reason!="global calibration reused CPU"||fusion::gpuGlobalStats().calls!=0||cc!=expected||cv::norm(cpu,result,cv::NORM_INF)!=0)throw std::runtime_error("Border-sensitive disagreement was admitted or CPU calibration output was lost");
    for(int i=0;i<12;++i)if(fusion::gpuGlobalECC(reference,source,initial,cv::MOTION_HOMOGRAPHY,criteria,.5,result,cc,reason))throw std::runtime_error("Numerical global class was re-probed");
    cv::Mat noise;if(!fusion::gpuNoiseGrid(reference,256,{},noise,reason)||!fusion::gpuTestState().healthy)throw std::runtime_error("Global numerical rejection disabled unrelated stages");
    std::cout<<"PASS actual global calibration numeric rejection, exact CPU reuse and permanent profile isolation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
