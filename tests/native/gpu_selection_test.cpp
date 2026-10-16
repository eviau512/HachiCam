#include "local_support.h"
#include "gpu_compute.h"
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <iostream>
#include <thread>
using namespace fusion;
static void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
void runGpuSelectionTests() {
    cv::RNG rng(93819);
    for(auto size:{cv::Size(351,349),cv::Size(1027,769),cv::Size(4096,3072),cv::Size(4000,4000)}) {
        cv::Mat bytes(size.height+2,size.width+9,CV_8U),a(size.height+2,size.width+9,CV_32F),b(a.size(),a.type());
        rng.fill(bytes,cv::RNG::UNIFORM,0,256);rng.fill(a,cv::RNG::NORMAL,0,12);rng.fill(b,cv::RNG::NORMAL,0,12);
        auto reference=bytes(cv::Rect(3,1,size.width,size.height)),refHP=a(cv::Rect(3,1,size.width,size.height));
        auto independent=b(cv::Rect(3,1,size.width,size.height));cv::Mat source=reference.clone(),srcHP,validity(size,CV_8U,cv::Scalar(255));
        validity(cv::Rect(19,20,17,27)).setTo(0);
        std::vector<cv::Rect> patches{{0,0,321,321},{31,19,257,193},{0,0,32,32},{0,0,36,36}};
        for(int i=0;i<17;++i)patches.emplace_back(std::min(size.width-321,i*31),std::min(size.height-321,i*19),321,321);
        std::vector<cv::Vec2d> noise(patches.size(),{0,0});
        for(double rho:{.75,.8995,.9000,.9005,.96,1.}) {
            srcHP=refHP*rho+independent*std::sqrt(1-rho*rho);
            std::vector<uchar> actual;GpuSelectionStats stats;std::string reason;
            require(gpuStrongSelection(reference,source,refHP,srcHP,validity,patches,noise,actual,stats,reason),"Actual selector unavailable: "+reason);
            for(size_t i=0;i<patches.size();++i)require(actual[i]==strongPatchEligible(reference,source,refHP,srcHP,validity,patches[i],noise[i]),"GPU selector correlation/mask parity failed");
            require(stats.patches==int(patches.size())&&stats.cpuPatches<=stats.patches,"CPU selector disguised as GPU execution");
        }
        srcHP=refHP*.68;std::vector<uchar> flags;GpuSelectionStats stats;std::string reason;
        require(gpuStrongSelection(reference,source,refHP,srcHP,validity,patches,noise,flags,stats,reason)&&stats.cpuPatches>0,"Ratio boundary was not CPU-completed");
        for(size_t i=0;i<patches.size();++i)require(flags[i]==strongPatchEligible(reference,source,refHP,srcHP,validity,patches[i],noise[i]),"GPU ratio boundary mismatch");
        srcHP=refHP.clone();std::vector<cv::Vec2d> nearNoise(patches.size(),{145,145});
        require(gpuStrongSelection(reference,source,refHP,srcHP,validity,patches,nearNoise,flags,stats,reason)&&stats.cpuPatches>0,"Near-noise cancellation was not CPU-completed");
        for(size_t i=0;i<patches.size();++i)require(flags[i]==strongPatchEligible(reference,source,refHP,srcHP,validity,patches[i],nearNoise[i]),"GPU noise cancellation parity failed");
        const auto state=gpuTestState();require(state.healthy&&!state.inFlight&&state.allocation<=64ULL*1024*1024,"Selector exceeded shared pool");
        std::cout<<"GPU strong selector "<<size<<" padded mask, count/ratio/correlation boundaries, mixed >16 batch PASS\n";
    }
    resetGpuDiagnostics();require(gpuDiagnostics().empty(),"Per-result trace did not reset");
    cv::Mat flat(97,97,CV_8U,cv::Scalar(128)),hp(97,97,CV_32F,cv::Scalar(0));std::vector<uchar> flags;GpuSelectionStats stats;std::string reason;
    require(gpuStrongSelection(flat,flat,hp,hp,{},{{0,0,97,97}},{{0,0}},flags,stats,reason)&&!flags[0],"Flat selector admitted structure");
    const auto before=gpuDiagnostics();std::string other;
    std::thread thread([&]{resetGpuDiagnostics();cv::Mat output;std::string r;gpuNoiseGrid(flat,256,{},output,r);other=gpuDiagnostics();});thread.join();
    require(!other.empty()&&gpuDiagnostics()==before,"Parallel native result traces mixed jobs");
    require(!gpuStrongSelection(flat,flat,hp,hp,{},{{INT_MAX,0,97,97}},{{0,0}},flags,stats,reason),"Invalid selector ROI accepted");
    std::cout<<"GPU flat/invalid selector and per-thread decision trace PASS\n";
}
