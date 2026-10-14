#include "local_support.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
    try {
        if(argc!=2)return 2;_putenv_s("HACHICAM_GPU_TEST_FAULT",argv[1]);
        cv::Mat ref(65,65,CV_8U,cv::Scalar(100)),src(ref.size(),CV_8U,cv::Scalar(100));
        fusion::LocalRegistration model;model.domain=ref.size();model.field=cv::Mat(2,2,CV_32FC3,cv::Scalar(0,0,1));
        cv::Mat output;std::string reason;
        for(int i=0;i<2;++i)if(fusion::gpuLocalSupport(ref,src,model,output,reason))throw std::runtime_error("fault published GPU output");
        if(fusion::gpuFittingAvailable(ref.total(),false))throw std::runtime_error("failed context admitted fitting");
        const auto state=fusion::gpuTestState();
        if(state.healthy || state.allocation>64ULL*1024*1024)throw std::runtime_error("failed state/budget violation");
        if(std::string(argv[1])=="timeout" && (!state.inFlight || !state.allocation))throw std::runtime_error("unresolved resources freed");
        if(std::string(argv[1])!="timeout" && (state.inFlight || state.allocation))throw std::runtime_error("resolved failed initialization leaked allocations");
        std::cout<<"PASS injected "<<argv[1]<<" fallback; retained="<<state.allocation<<" inFlight="<<state.inFlight<<"\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
