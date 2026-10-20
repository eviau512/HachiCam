#include "gpu_compute.h"
#include "local_support.h"
#include <iostream>
#include <stdexcept>
using namespace fusion;
static void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
void runGpuComputeTests() {
    cv::RNG rng(91353);
    for(auto size:{cv::Size(1027,769),cv::Size(4096,3072)}) {
        cv::Mat storage(size.height+1,size.width+5,CV_8UC3,cv::Scalar::all(130));
        cv::Mat image=storage(cv::Rect(2,1,size.width,size.height));
        LocalRegistration model;model.domain=size;
        const cv::Size grid((size.width-1+255)/256+1,(size.height-1+255)/256+1);
        model.field=cv::Mat(grid,CV_32FC3,cv::Scalar(.31,-.19,.9));model.detail=cv::Mat(grid,CV_8U,cv::Scalar(255));
        model.gate=cv::Mat(size,CV_8U,cv::Scalar(255));
        SamplingPlate plate{&model,cv::Matx33d::eye(),size,&image};
        cv::Rect patch(size.width/2-40,size.height/2-40,80,80),source(patch.x-10,patch.y-10,100,100);
        auto compare=[&](bool requireUseful){
            std::vector<MappedObservation> actual;bool used=false;int completed=0;std::string reason;
            require(gpuObservationMapping(plate,source,patch,actual,used,completed,reason)&&used,"Actual GPU observation mapping unavailable: "+reason);
            const auto expected=mapRegionalCpu(plate,source,patch);int valid=0;
            require(actual.size()==expected.size(),"GPU mapping count differs");
            for(size_t i=0;i<actual.size();++i){require(actual[i].valid==expected[i].valid,"GPU mapping admitted/rejected an observation differently");if(actual[i].valid){++valid;for(int k=0;k<4;++k)require(cv::norm(actual[i].local[k]-expected[i].local[k])<=.0005,"GPU relative observation mapping lost precision");}}
            if(requireUseful)require(valid>3000&&completed<source.area()/3,"GPU mapping did not perform useful work beyond CPU completion");
            require(gpuTestState().healthy&&gpuTestState().allocation<=64ULL*1024*1024,"GPU mapping poisoned context or exceeded reservation");
        };
        compare(true);
        plate.inverse={1.00005,.0001,.375,-.0001,.99995,-.23,1e-7,-1e-7,1};compare(true);
        model.gate(cv::Rect(patch.x+20,patch.y+10,19,23)).setTo(0);image(cv::Rect(source.x+25,source.y+25,7,7)).setTo(cv::Scalar(247,130,130));compare(true);
        model.detail.at<uchar>(grid.height/2,grid.width/2)=0;compare(false);
        model.field.at<cv::Vec3f>(grid.height/2,grid.width/2)={3.5f,-.19f,.9f};compare(false);
        model.field.setTo(cv::Scalar(.31,-.19,.9));model.detail.setTo(255);model.gate.setTo(255);plate.inverse=cv::Matx33d::eye();patch={0,0,80,80};source={0,0,100,100};compare(true);
        std::cout<<"GPU observation mapping "<<size<<" relative coordinates, projective transform, clipped source, gates, detail and conflict PASS\n";
    }
    {
        cv::Mat truth(64,80,CV_32FC3);rng.fill(truth,cv::RNG::UNIFORM,80,170);
        cv::GaussianBlur(truth,truth,{0,0},.5);
        cv::Mat initial(truth.size(),CV_32FC3,cv::Scalar::all(125));
        std::vector<RegionalObservation> observations;
        for(int y=1;y<truth.rows-2;++y)for(int x=1;x<truth.cols-2;++x)for(float phase:{.1f,.6f}) {
            RegionalObservation o{};int k=0;o.color={0,0,0};
            for(float dy:{0.f,.4f})for(float dx:{0.f,.4f}) {
                const float fx=phase+dx,fy=.2f+dy;
                for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx) {
                    o.index[k]=(y+yy)*truth.cols+x+xx;o.weight[k]=.25f*(xx?fx:1-fx)*(yy?fy:1-fy);
                    o.color+=truth.ptr<cv::Vec3f>()[o.index[k]]*o.weight[k];++k;
                }
            }
            observations.push_back(o);
        }
        const auto cpu=solveRegionalCpu(initial,observations);RegionalSolve actual;bool used=false;std::string reason;
        require(gpuRegionalSolve(initial,observations,actual,used,reason)&&used,"Actual regional GPU solver did not execute: "+reason);
        require(actual.accepted==cpu.accepted&&cv::norm(actual.image,cpu.image,cv::NORM_INF)<=.005,"GPU regional estimate or admission differs");
        require(std::abs(actual.before-cpu.before)<=.0002*cpu.before&&std::abs(actual.after-cpu.after)<=.0002*std::max(1.,cpu.after),"GPU regional costs differ");
        require(cv::norm(initial,cv::Mat(initial.size(),initial.type(),cv::Scalar::all(125)),cv::NORM_INF)==0,"GPU mutated immutable initial estimate");
        for(int y=0;y<initial.rows;++y)for(int x=0;x<initial.cols;++x)for(int c=0;c<3;++c)
            require(std::abs(actual.image.at<cv::Vec3f>(y,x)[c]-125)<=24.00001,"GPU exceeded correction bounds");
        auto invalid=observations;invalid[0].index[0]=int(initial.total());
        require(!gpuRegionalSolve(initial,invalid,actual,used,reason)&&!used,"GPU accepted out-of-range tap");
        invalid=observations;invalid[0].weight[0]=NAN;
        require(!gpuRegionalSolve(initial,invalid,actual,used,reason),"GPU accepted nonfinite tap");
        require(!gpuRegionalSolve(initial,{},actual,used,reason),"GPU accepted empty observations");
        std::vector<RegionalObservation> duplicate(512);
        for(size_t i=0;i<duplicate.size();++i){auto& o=duplicate[i];o.color={125,125,125};for(int k=0;k<16;++k){o.index[k]=int(i%initial.total());o.weight[k]=1.f/16;}}
        require(gpuRegionalSolve(initial,duplicate,actual,used,reason)&&!actual.accepted&&actual.before==0&&cv::norm(actual.image,initial,cv::NORM_INF)==0,"Constant/duplicate-tap negative changed reference");
        std::cout<<"GPU regional solver actual resident iterations, stable CSR, correction bounds and negatives PASS\n";
    }
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
