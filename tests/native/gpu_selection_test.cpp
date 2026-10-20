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

void runGpuPhysicalTests() {
    cv::RNG rng(338191);cv::Mat storage(351,359,CV_8U),ha(351,359,CV_32F),hb(351,359,CV_32F);
    rng.fill(storage,cv::RNG::UNIFORM,0,256);rng.fill(ha,cv::RNG::NORMAL,0,12);rng.fill(hb,cv::RNG::NORMAL,0,12);
    const cv::Rect padded(3,2,349,347);const cv::Mat reference=storage(padded),a=ha(padded);
    cv::Mat validity(reference.size(),CV_8U,cv::Scalar(255));validity(cv::Rect(19,20,17,27)).setTo(0);
    for(bool highlight:{false,true})for(float shift:{-7.99f,-.015625f,0.f,.015625f,.25049f,7.99f}) {
        std::vector<PhysicalPatch> patches;
        for(int i=0;i<70;++i)patches.push_back({{i%7,i%5,321-i%3,321-i%4},{shift,0},{0,0}});
        patches.push_back({{0,0,35,36},{shift,0},{0,0}}); // <256 interior observations
        const cv::Mat source=hb(padded);std::vector<cv::Vec2d> got;GpuPhysicalStats stats;std::string reason;
        require(gpuPhysicalChecks(reference,a,source,validity,patches,highlight,got,stats,reason),"Physical GPU unavailable: "+reason);
        require(stats.patches==int(patches.size()),"CPU disguised as GPU physical checks");
        for(size_t i=0;i<patches.size();++i) {
            const auto expected=cpuPhysicalCheck(reference,a,source,validity,patches[i]);
            if(cv::norm(got[i]-expected)>=.002)std::cerr<<"physical mismatch highlight="<<highlight<<" shift="<<shift<<" i="<<i<<" got="<<got[i]<<" expected="<<expected<<"\n";
            require(cv::norm(got[i]-expected)<.002,"GPU physical subpixel/stride/count mismatch");
        }
    }
    cv::Mat referenceFlat(321,321,CV_8U,cv::Scalar(128)),signal(321,321,CV_32F),independent(signal.size(),CV_32F);
    rng.fill(signal,cv::RNG::NORMAL,0,12);rng.fill(independent,cv::RNG::NORMAL,0,12);
    for(bool highlight:{false,true})for(double rho:{.7995,.8,.8005,.8995,.9,.9005,1.}) {
        cv::Mat b=signal*rho+independent*std::sqrt(1-rho*rho);
        for(double multiplier:{.68,1.}) {
            const cv::Mat scaled=b*multiplier;const std::vector<PhysicalPatch> patches{{{0,0,321,321},{0,0},{0,0}}};
            std::vector<cv::Vec2d> got;GpuPhysicalStats stats;std::string reason;
            require(gpuPhysicalChecks(referenceFlat,signal,scaled,{},patches,highlight,got,stats,reason),"Physical boundary GPU unavailable");
            const auto expected=cpuPhysicalCheck(referenceFlat,signal,scaled,{},patches[0]);
            require((got[0][0]>=(highlight ? .8 : .9))==(expected[0]>=(highlight ? .8 : .9)) &&
                (got[0][1]>=.68)==(expected[1]>=.68),"Physical boundary decision mismatch");
        }
    }
    // Android's OpenCV 4.10 path must execute even when host OpenCV is 5.
    for(float dx:{-7.99f,-.0162f,-.015625f,0.f,.015625f,.0162f,7.99f}) {
        const std::vector<PhysicalPatch> probes{{{0,0,321,321},{dx,.249f},{0,0}}};
        std::vector<cv::Vec2d> measured;
        require(gpuTestQuantizedPhysical(referenceFlat,signal,independent,{},probes,measured),"Android quantized GPU kernel not executed");
        auto authority=probes[0];
        auto quantize=[](float x){const int fixed=cvRound(x*1024)+16;return float(int(std::floor(double(fixed)/32)))/32;};
        authority.shift={quantize(dx),quantize(.249f)};
        const auto expected=cpuPhysicalCheck(referenceFlat,signal,independent,{},authority);
        require(cv::norm(measured[0]-expected)<.002,"Android quantized interpolation mismatch");
    }
    cv::Mat zero=cv::Mat::zeros(321,321,CV_32F);std::vector<cv::Vec2d> got;GpuPhysicalStats stats;std::string reason;
    const std::vector<PhysicalPatch> flat{{{0,0,321,321},{0,0},{0,0}}};
    require(gpuPhysicalChecks(referenceFlat,zero,zero,{},flat,false,got,stats,reason)&&got[0][0]<.8&&stats.cpuPatches==1,"Flat physical evidence admitted");
    for(const auto bad:std::vector<PhysicalPatch>{{{INT_MAX,0,321,321},{0,0},{0,0}},{{0,0,321,321},{NAN,0},{0,0}},{{0,0,321,321},{8.01f,0},{0,0}},{{0,0,321,321},{0,0},{-1,0}}})
        require(!gpuPhysicalChecks(referenceFlat,signal,signal,{}, {bad},true,got,stats,reason),"Invalid physical workload accepted");
    const auto state=gpuTestState();require(state.healthy&&!state.inFlight&&state.allocation<=64ULL*1024*1024,"Physical pool exceeded");
    std::cout<<"GPU physical verification: >64 batches, padded stride, clipped masks, quantized shifts, flat/count/threshold/failure boundaries PASS\n";
}
