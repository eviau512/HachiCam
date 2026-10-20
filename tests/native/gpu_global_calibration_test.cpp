#include "gpu_compute.h"
#include "local_support.h"
#include <opencv2/video.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
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
    cv::Mat initialPatch(8,8,CV_32FC3,cv::Scalar::all(128));
    std::vector<fusion::RegionalObservation> observations(512);
    for(auto& o:observations){o={};o.color={140,140,140};o.weight[0]=1;}
#ifdef _WIN32
    _putenv_s("HACHICAM_GPU_TEST_FAULT","solver_numeric");
#else
    setenv("HACHICAM_GPU_TEST_FAULT","solver_numeric",1);
#endif
    fusion::RegionalSolve solved;bool used=false;
    const auto expectedPatch=fusion::solveRegionalCpu(initialPatch,observations);
    if(!fusion::gpuRegionalSolve(initialPatch,observations,solved,used,reason)||used||reason.find("numeric-block")==std::string::npos||cv::norm(solved.image,expectedPatch.image,cv::NORM_INF)!=0)throw std::runtime_error("Solver numerical rejection did not retain CPU calibration result");
#ifdef _WIN32
    _putenv_s("HACHICAM_GPU_TEST_FAULT","");
#else
    unsetenv("HACHICAM_GPU_TEST_FAULT");
#endif
    for(int i=0;i<12;++i)if(fusion::gpuRegionalSolve(initialPatch,observations,solved,used,reason))throw std::runtime_error("Solver numeric class was re-probed");
    cv::Mat active(2,2,CV_8U,cv::Scalar(255));
    if(!fusion::gpuNoiseGrid(reference,256,active,noise,reason)||!fusion::gpuTestState().healthy)throw std::runtime_error("Solver numerical rejection disabled unrelated stages");
    std::cout<<"PASS actual regional calibration numeric rejection, CPU reuse and stage isolation\n";
    cv::Mat mapImage(192,192,CV_8UC3,cv::Scalar::all(130));fusion::LocalRegistration mapModel;mapModel.domain=mapImage.size();
    mapModel.field=cv::Mat(2,2,CV_32FC3,cv::Scalar(.31,-.19,.9));mapModel.detail=cv::Mat(2,2,CV_8U,cv::Scalar(255));mapModel.gate=cv::Mat(mapImage.size(),CV_8U,cv::Scalar(255));
    fusion::SamplingPlate mapPlate{&mapModel,cv::Matx33d::eye(),mapImage.size(),&mapImage};const cv::Rect sourceRoi(30,30,100,100),patch(40,40,80,80);
    std::vector<fusion::MappedObservation> mapped;int completed=0;
    const auto expectedMap=fusion::mapRegionalCpu(mapPlate,sourceRoi,patch);
#ifdef _WIN32
    _putenv_s("HACHICAM_GPU_TEST_FAULT","mapping_numeric");
#else
    setenv("HACHICAM_GPU_TEST_FAULT","mapping_numeric",1);
#endif
    if(!fusion::gpuObservationMapping(mapPlate,sourceRoi,patch,mapped,used,completed,reason)||used||reason.find("numeric-block")==std::string::npos)throw std::runtime_error("Mapping numeric disagreement was admitted");
    for(size_t i=0;i<mapped.size();++i){if(mapped[i].valid!=expectedMap[i].valid)throw std::runtime_error("Mapping CPU reuse lost status");if(mapped[i].valid)for(int k=0;k<4;++k)if(mapped[i].local[k]!=expectedMap[i].local[k])throw std::runtime_error("Mapping CPU reuse lost precision");}
#ifdef _WIN32
    _putenv_s("HACHICAM_GPU_TEST_FAULT","");
#else
    unsetenv("HACHICAM_GPU_TEST_FAULT");
#endif
    for(int i=0;i<12;++i)if(fusion::gpuObservationMapping(mapPlate,sourceRoi,patch,mapped,used,completed,reason))throw std::runtime_error("Mapping numeric class was re-probed");
    cv::Mat otherProfile(1025,1025,CV_8U,cv::Scalar(128));
    if(!fusion::gpuNoiseGrid(otherProfile,256,{},noise,reason)||!fusion::gpuTestState().healthy)throw std::runtime_error("Mapping numerical rejection disabled unrelated stage");
    std::cout<<"PASS actual mapping calibration numeric rejection, exact CPU reuse and stage isolation\n";
    std::cout<<"PASS actual global calibration numeric rejection, exact CPU reuse and permanent profile isolation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
