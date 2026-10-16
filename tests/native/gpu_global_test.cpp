#include "gpu_compute.h"
#include "local_support.h"
#include <opencv2/video.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>
static void requireGlobal(bool value,const std::string& reason){if(!value)throw std::runtime_error(reason);}
void runGpuGlobalTests(){
    cv::RNG rng(391822);
    for(auto size:{cv::Size(193,137),cv::Size(641,479),cv::Size(1280,960)}){
        cv::Mat storage(size.height+2,size.width+9,CV_8U);rng.fill(storage,cv::RNG::UNIFORM,30,220);
        cv::GaussianBlur(storage,storage,{0,0},1.3);auto reference=storage(cv::Rect(3,1,size.width,size.height));
        reference.rowRange(0,12).setTo(0);reference.rowRange(size.height-12,size.height).setTo(0);
        reference.colRange(0,12).setTo(0);reference.colRange(size.width-12,size.width).setTo(0);
        for(int model:{cv::MOTION_EUCLIDEAN,cv::MOTION_HOMOGRAPHY})for(int scenario=0;scenario<3;++scenario){
            const cv::Mat truth=(cv::Mat_<float>(3,3)<<1,scenario==1 ? -.001f : 0.f,.25f,scenario==1 ? .001f : 0.f,1,-.375f,model==cv::MOTION_HOMOGRAPHY&&scenario==2 ? 1e-6f : 0.f,0,1);
            cv::Mat source;cv::warpPerspective(reference,source,truth,reference.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
            if(scenario==2)source.convertTo(source,CV_8U,.8,17);
            cv::Mat initial=cv::Mat::eye(model==cv::MOTION_HOMOGRAPHY ? 3 : 2,3,CV_32F);if(scenario==1){initial.at<float>(0,2)=.125f;initial.at<float>(1,2)=-.125f;}
            const cv::TermCriteria criteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,40,1e-5);
            cv::Mat actual,expected=initial.clone();double score=-1;std::string reason;
            requireGlobal(fusion::gpuGlobalECC(reference,source,initial,model,criteria,.5,actual,score,reason),"Global GPU did not execute: "+reason);
            requireGlobal(reason=="validated GPU resident global ECC","Global GPU test was hidden by CPU fallback: "+reason);
            const double cpu=cv::findTransformECC(reference,source,expected,model,criteria);
            double disagreement=0;
            for(int y=0;y<5;++y)for(int x=0;x<5;++x){const cv::Vec3f p((size.width-1)*x/4.f,(size.height-1)*y/4.f,1);auto map=[&](const cv::Mat& h){cv::Vec3f r(0,0,1);for(int a=0;a<h.rows;++a)r[a]=h.at<float>(a,0)*p[0]+h.at<float>(a,1)*p[1]+h.at<float>(a,2);return cv::Point2d(r[0]/r[2],r[1]/r[2]);};disagreement=std::max(disagreement,cv::norm(map(actual)-map(expected))/.5);}
            std::cout<<"GPU global model="<<model<<" size="<<size<<" scenario="<<scenario<<" disagreement="<<disagreement<<" cc="<<score<<" cpu="<<cpu<<std::endl;
            requireGlobal(disagreement<=.05&&std::abs(score-cpu)<=.002,"Global optimizer did not meet original-source tolerance");
        }
    }
    cv::Mat large(1440,1920,CV_8U);rng.fill(large,cv::RNG::UNIFORM,30,220);cv::Mat identity=cv::Mat::eye(2,3,CV_32F),out;double score=-1;std::string reason;
    requireGlobal(fusion::gpuGlobalECC(large,large,identity,cv::MOTION_EUCLIDEAN,{cv::TermCriteria::COUNT|cv::TermCriteria::EPS,4,1e-5},.46875,out,score,reason)&&score>.9999,"Maximum global resident layout failed");
    const auto state=fusion::gpuTestState();requireGlobal(state.healthy&&!state.inFlight&&state.allocation<=64ULL*1024*1024,"Global pool cap/fence invariant failed");
    cv::Mat flat(193,137,CV_8U,cv::Scalar(128));requireGlobal(!fusion::gpuGlobalECC(flat,flat,identity,cv::MOTION_EUCLIDEAN,{cv::TermCriteria::COUNT|cv::TermCriteria::EPS,4,1e-5},1,out,score,reason),"Flat global image produced geometric evidence");
    requireGlobal(fusion::gpuTestState().healthy,"Numerical global failure disabled unrelated kernels");
    cv::Mat unsupported=cv::Mat::eye(3,3,CV_32F);unsupported.at<float>(2,2)=.5f;
    requireGlobal(!fusion::gpuGlobalECC(flat,flat,unsupported,cv::MOTION_HOMOGRAPHY,{cv::TermCriteria::COUNT|cv::TermCriteria::EPS,4,1e-5},1,out,score,reason),"Unnormalized global homography bypassed CPU semantics");
    requireGlobal(!fusion::gpuGlobalECC(flat,flat,identity,cv::MOTION_EUCLIDEAN,{cv::TermCriteria::EPS,4,1e-5},1,out,score,reason),"Unbounded criteria bypassed global CPU fallback");
    std::cout<<"PASS resident global ECC original-source mapping, flat rejection and maximum shared layout"<<std::endl;
}
