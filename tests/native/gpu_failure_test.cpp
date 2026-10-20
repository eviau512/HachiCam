#include "local_support.h"
#include "gpu_compute.h"
#include <opencv2/video.hpp>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
    try {
        if(argc!=2)return 2;_putenv_s("HACHICAM_GPU_TEST_FAULT",argv[1]);
        cv::Mat ref(65,65,CV_8U,cv::Scalar(100)),src(ref.size(),CV_8U,cv::Scalar(100));
        fusion::LocalRegistration model;model.domain=ref.size();model.field=cv::Mat(2,2,CV_32FC3,cv::Scalar(0,0,1));
        cv::Mat output;std::string reason;
        if(std::string(argv[1])=="global_shared"){
            cv::Mat transform=cv::Mat::eye(2,3,CV_32F);double cc=-1;
            if(fusion::gpuGlobalECC(ref,src,transform,cv::MOTION_EUCLIDEAN,{cv::TermCriteria::COUNT|cv::TermCriteria::EPS,4,1e-5},1,output,cc,reason))throw std::runtime_error("Unsupported global shared memory admitted");
            if(!fusion::gpuLocalSupport(ref,src,model,output,reason)||!fusion::gpuTestState().healthy)throw std::runtime_error("Global capability disabled an unrelated kernel");
            std::cout<<"PASS global-only shared-memory fallback retains existing compute\n";return 0;
        }
        for(int i=0;i<2;++i)if(fusion::gpuLocalSupport(ref,src,model,output,reason))throw std::runtime_error("fault published GPU output");
        if(fusion::gpuFittingAvailable(ref.total(),false))throw std::runtime_error("failed context admitted fitting");
        if(fusion::gpuNoiseGrid(ref,256,{},output,reason))throw std::runtime_error("failed context admitted noise");
        cv::Mat transform=cv::Mat::eye(2,3,CV_32F);double cc=-1;
        if(fusion::gpuGlobalECC(ref,src,transform,cv::MOTION_EUCLIDEAN,{cv::TermCriteria::COUNT|cv::TermCriteria::EPS,4,1e-5},1,output,cc,reason))throw std::runtime_error("failed context admitted global optimization");
        cv::Mat hp;ref.convertTo(hp,CV_32F);std::vector<uchar> flags;fusion::GpuSelectionStats selection;
        if(fusion::gpuStrongSelection(ref,src,hp,hp,{},{{0,0,65,65}},{{0,0}},flags,selection,reason))throw std::runtime_error("failed context admitted selector");
        std::vector<cv::Vec2d> metrics;fusion::GpuPhysicalStats physical;
        if(fusion::gpuPhysicalChecks(ref,hp,hp,{},{{{0,0,65,65},{0,0},{0,0}}},false,metrics,physical,reason))throw std::runtime_error("failed context admitted physical checks");
        cv::Mat color(ref.size(),CV_8UC3,cv::Scalar(100,100,100));
        std::vector<fusion::SamplingPlate> plates(2,{&model,cv::Matx33d::eye(),ref.size(),&color});
        long long n=0,m=0;fusion::GpuComputeStats stats;
        if(fusion::gpuMergeAnchors(plates,color,cv::Rect({},ref.size()),output,n,m,stats,reason))throw std::runtime_error("failed context admitted merge");
        const auto state=fusion::gpuTestState();
        if(state.healthy || state.allocation>64ULL*1024*1024)throw std::runtime_error("failed state/budget violation");
        if(std::string(argv[1])=="timeout" && (!state.inFlight || !state.allocation))throw std::runtime_error("unresolved resources freed");
        if(std::string(argv[1])!="timeout" && (state.inFlight || state.allocation))throw std::runtime_error("resolved failed initialization leaked allocations");
        std::cout<<"PASS injected "<<argv[1]<<" fallback; retained="<<state.allocation<<" inFlight="<<state.inFlight<<"\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
