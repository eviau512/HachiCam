#include "regional_reconstruction.h"
#include "burst_fusion.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void requireRegion(bool pass,const char* message) { if(!pass)throw std::runtime_error(message); }
fusion::LocalRegistration model(bool detail=true) {
    fusion::LocalRegistration m;m.domain={1024,1024};m.spacing=256;
    m.field=cv::Mat(5,5,CV_32FC3,cv::Scalar(0,0,.98));
    m.detail=cv::Mat(5,5,CV_8U,cv::Scalar(detail ? 255 : 0));
    m.gate=cv::Mat::zeros(m.domain,CV_8U);m.gate(cv::Rect(256,256,320,320)).setTo(255);
    return m;
}
}
void runRegionalTests() {
    requireRegion(!fusion::reconstructionFits({4000,4000},{0,0,4000,4000}),"A legal 16 MP source may still exceed the 56 MP output budget");
    requireRegion(fusion::reconstructionFits({4000,4000},{190,190,3620,3620}),"A bounded common crop may retain legal reconstruction");
    const cv::Rect crop(64,64,896,896);
    auto a=model(),b=model();
    std::vector<fusion::SamplingPlate> plates{
        {nullptr,cv::Matx33d::eye(),{1024,1024}},
        {&a,cv::Matx33d(1,0,.32,0,1,.08,0,0,1),{1024,1024}},
        {&b,cv::Matx33d(1,0,.08,0,1,.36,0,0,1),{1024,1024}}};
    const auto valid=fusion::qualifyRegions(plates,crop);
    requireRegion(valid.eligible() && valid.fraction<.5,"Text with invalid background can qualify regionally");
    requireRegion(valid.at(350,350)==7 && valid.at(100,100)==0,"Eligibility is spatial, not whole-frame authority");
    b.detail.setTo(0);
    requireRegion(!fusion::qualifyRegions(plates,crop).eligible(),"Denoise-only samples cannot qualify reconstruction");
    b.detail.setTo(255);plates[2].inverse=cv::Matx33d(1,0,.16,0,1,.04,0,0,1);
    requireRegion(!fusion::qualifyRegions(plates,crop).eligible(),"Collinear phases cannot qualify two-dimensional detail");
    plates[1].inverse=plates[2].inverse=cv::Matx33d::eye();
    requireRegion(!fusion::qualifyRegions(plates,crop).eligible(),"Identical samples cannot qualify detail");
    plates[1].inverse=cv::Matx33d(1,0,.32,0,1,.08,0,0,1);
    plates[2].inverse=cv::Matx33d(1,0,.08,0,1,.36,0,0,1);
    a.gate.setTo(0);
    requireRegion(!fusion::qualifyRegions(plates,crop).eligible(),"Local photometric mismatch cannot qualify text");
    a.gate.setTo(255);a.detail.setTo(0);b.gate.setTo(255);b.detail.setTo(0);
    requireRegion(!fusion::qualifyRegions(plates,crop).eligible(),"Valid background support without detail cannot qualify text");

    // Independent high-resolution chart, observed at half resolution with known sensor phases.
    cv::Mat truth(1536,1536,CV_8UC3,cv::Scalar(220,220,220));
    for(int y=160;y<1380;y+=64)cv::putText(truth,"012345 HachiCam - 8.6 / 1.25",{140,y},cv::FONT_HERSHEY_SIMPLEX,1.1,{30,30,30},1,cv::LINE_AA);
    for(int x=180;x<1350;x+=13)cv::line(truth,{x,140},{x,230},{45,45,45},1);
    std::vector<cv::Point2d> phases{{0,0},{.36,.08},{.08,.36},{-.32,.28}};
    std::vector<cv::Mat> source,ideal;
    for(const auto& phase:phases) {
        cv::Mat hi,lo;cv::Mat transform=(cv::Mat_<double>(2,3)<<1,0,phase.x*2,0,1,phase.y*2);
        cv::warpAffine(truth,hi,transform,truth.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
        cv::resize(hi,lo,{768,768},0,0,cv::INTER_AREA);source.push_back(lo);ideal.push_back(hi);
    }
    BurstFusionEngine engine;BurstFusionEngine::Report report;
    const auto output=engine.fuseBurstFrames(source,false,false,true,{0,0,0,0},{100,100,100,100},&report);
    requireRegion(report.scale==2 && report.reconstructedFraction>0,"Independent chart needs actual qualified reconstruction");
    const cv::Rect highCrop(report.crop.x*2,report.crop.y*2,report.crop.width*2,report.crop.height*2);
    cv::Mat interpolated;cv::resize(source[report.reference](report.crop),interpolated,output.size(),0,0,cv::INTER_CUBIC);
    const cv::Rect interior(100,100,output.cols-200,output.rows-200);
    const double reconstructedError=cv::norm(output(interior),ideal[report.reference](highCrop)(interior),cv::NORM_L2SQR)/interior.area();
    const double interpolationError=cv::norm(interpolated(interior),ideal[report.reference](highCrop)(interior),cv::NORM_L2SQR)/interior.area();
    std::cout<<"regional chart squared error="<<reconstructedError<<" interpolation="<<interpolationError
             <<" qualified="<<report.regionFraction<<" actual="<<report.reconstructedFraction<<'\n';
    requireRegion(reconstructedError<interpolationError,"Known-phase chart reconstruction must beat matched interpolation");
    auto known=model();known.domain={768,768};known.field=cv::Mat(4,4,CV_32FC3,cv::Scalar(0,0,1));
    known.detail=cv::Mat(4,4,CV_8U,cv::Scalar(255));known.gate=cv::Mat(768,768,CV_8U,cv::Scalar(255));
    std::vector<fusion::SamplingPlate> observed;
    for(size_t i=0;i<source.size();++i)observed.push_back({i ? &known : nullptr,cv::Matx33d(1,0,phases[i].x,0,1,phases[i].y,0,0,1),{768,768},&source[i]});
    const auto protectedPlan=fusion::qualifyRegions(observed,{8,8,752,752});
    cv::Mat protectedOutput;cv::resize(source[0](protectedPlan.crop),protectedOutput,{1504,1504},0,0,cv::INTER_CUBIC);
    const auto saved=protectedOutput.clone();cv::Mat preserve(protectedPlan.crop.size(),CV_8U,cv::Scalar(255));
    const auto protectedFit=fusion::refineRegions(observed,protectedPlan,protectedOutput,preserve);
    requireRegion(protectedFit.pixels==0 && cv::norm(saved,protectedOutput,cv::NORM_INF)==0,"Refinement cannot overwrite protected HDR/clipped evidence");
    std::cout<<"PASS: regional phases, invalid background, denoise/collinear/identical/mismatch and independent chart\n";
}
