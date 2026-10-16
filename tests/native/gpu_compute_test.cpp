#include "gpu_compute.h"
#include "local_support.h"
#include <iostream>
#include <stdexcept>
using namespace fusion;
static void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
void runGpuComputeTests() {
    cv::RNG rng(91353);
    for(auto size:{cv::Size(17,13),cv::Size(513,333),cv::Size(1027,769),cv::Size(4096,3072)}) {
        cv::Mat padded(size.height+1,size.width+7,CV_8U);rng.fill(padded,cv::RNG::UNIFORM,0,256);
        cv::Mat gray=padded(cv::Rect(3,1,size.width,size.height));
        for(int y=0;y<gray.rows;++y)for(int x=0;x<gray.cols;++x) {
            auto& v=gray.at<uchar>(y,x);
            if(x%37>3)v=uchar(110+v%32); // Uncapped sigmas exercise exact medians.
        }
        const cv::Size shape((size.width-1+255)/256+1,(size.height-1+255)/256+1);
        for(int sparse=0;sparse<2;++sparse) {
            cv::Mat active,actual;std::string reason;
            if(sparse){active=cv::Mat(shape,CV_8U,cv::Scalar(255));active.at<uchar>(0,0)=0;}
            require(gpuNoiseGrid(gray,256,active,actual,reason),"GPU noise unavailable: "+reason);
            for(int y=0;y<shape.height;++y)for(int x=0;x<shape.width;++x) {
                const int cx=std::min(x*256,size.width-1),cy=std::min(y*256,size.height-1);
                const cv::Rect roi(std::max(0,cx-160),std::max(0,cy-160),std::min(size.width,cx+161)-std::max(0,cx-160),std::min(size.height,cy+161)-std::max(0,cy-160));
                const double expected=sparse&&!active.at<uchar>(y,x) ? 0 : estimateNoiseSigma(gray(roi));
                require(actual.at<double>(y,x)==expected,"GPU noise median/sample phase mismatch");
            }
        }
        // Two smooth/noisy scales, clipped exclusions and small valid sample sets.
        cv::Mat flat(size,CV_8U,cv::Scalar(127)),actual;std::string reason;
        require(gpuNoiseGrid(flat,256,{},actual,reason)&&cv::countNonZero(actual)==0,"Flat noise changed");
        std::cout<<"GPU exact node histograms "<<size<<" dense/sparse/padded/flat PASS\n";
    }
    for(auto size:{cv::Size(97,65),cv::Size(517,333),cv::Size(1027,769),cv::Size(4096,3072)}) {
        std::vector<cv::Mat> images(4);std::vector<LocalRegistration> models(4);
        std::vector<SamplingPlate> plates;
        for(int i=0;i<4;++i) {
            cv::Mat padded(size.height+1,size.width+5,CV_8UC3);rng.fill(padded,cv::RNG::UNIFORM,0,256);
            images[i]=padded(cv::Rect(2,1,size.width,size.height));
            if(i) {
                models[i].domain=size;models[i].field=cv::Mat((size.height-1+255)/256+1,(size.width-1+255)/256+1,CV_32FC3,cv::Scalar(.375*i,-.125*i,.85+.03*i));
                models[i].gate=cv::Mat(size,CV_8U,cv::Scalar(255));models[i].gate.row(0).setTo(0);
                models[i].field.at<cv::Vec3f>(0,1)={-1.25f,.25f,.7f};
                models[i].field.at<cv::Vec3f>(1,0)={0,0,0};
            }
            cv::Matx33d h(1+.00013*i,.00021*i,.31*i,-.00017*i,1-.00019*i,-.13*i,.00000003*i,-.00000002*i,1);
            plates.push_back({&models[i],h,size,&images[i]});
        }
        cv::Rect crop(3,2,size.width-7,size.height-5);cv::Mat actual;long long n=0,m=0;GpuComputeStats stats;std::string reason;
        require(gpuMergeAnchors(plates,images[0],crop,actual,n,m,stats,reason),"GPU merge unavailable: "+reason);
        long long expectedN=0,expectedM=0,mismatches=0;
        #pragma omp parallel for reduction(+:expectedN,expectedM,mismatches)
        for(int y=0;y<crop.height;++y)for(int x=0;x<crop.width;++x) {
            int count;auto expected=mergePixel(plates,images[0],crop.x+x,crop.y+y,count);
            mismatches+=actual.at<cv::Vec3b>(y,x)!=expected;expectedN+=std::max(1,count);expectedM+=count>=2;
        }
        require(!mismatches,"GPU color mismatch pixels="+std::to_string(mismatches));
        require(n==expectedN&&m==expectedM&&stats.mergeTiles>0,"GPU merge counts mismatch");
        const auto state=gpuTestState();require(state.healthy&&!state.inFlight&&state.allocation<=64ULL*1024*1024,"GPU layout transition exceeded budget");
        std::cout<<"GPU tiled color merge "<<size<<" exact; tiles="<<stats.mergeTiles<<" cpuPixels="<<stats.cpuMergePixels<<" PASS\n";
    }
}
