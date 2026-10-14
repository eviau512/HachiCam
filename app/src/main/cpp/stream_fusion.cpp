#include "burst_fusion.h"
#include "local_registration.h"
#include "fusion_math.h"
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#define STREAM_LOG(...) __android_log_print(ANDROID_LOG_INFO,"HachiCam-Fusion",__VA_ARGS__)
namespace {
struct StreamPlate {
    size_t index;
    cv::Matx33d inverse;
    fusion::LocalRegistration local;
    float gain=1;
};
cv::Point2d project(const cv::Matx33d& h,double x,double y) {
    const auto p=h*cv::Vec3d(x,y,1);
    if(!std::isfinite(p[2])||std::abs(p[2])<1e-9)return {-1,-1};
    return {p[0]/p[2],p[1]/p[2]};
}
cv::Mat analysis(const cv::Mat& image) {
    cv::Mat small;
    const double s=std::min(1.,1600./std::max(image.cols,image.rows));
    if(s<1)cv::resize(image,small,{},s,s,cv::INTER_AREA);else small=image.clone();
    return small;
}
cv::Matx33d fullTransform(const cv::Mat& transform,cv::Size full,cv::Size reduced) {
    cv::Mat h;transform.convertTo(h,CV_64F);
    cv::Matx33d m;
    for(int y=0;y<3;++y)for(int x=0;x<3;++x)m(y,x)=h.at<double>(y,x);
    const double sx=double(reduced.width)/full.width,sy=double(reduced.height)/full.height;
    const cv::Matx33d scale(sx,0,.5*sx-.5,0,sy,.5*sy-.5,0,0,1);
    return scale.inv()*m*scale;
}
bool cropFor(const StreamPlate& plate,cv::Size size,cv::Rect& crop) {
    auto candidate=crop;
    auto supported=[&] {
        for(double y:{double(candidate.y),candidate.y+(candidate.height-1)*.5,double(candidate.br().y-1)})
            for(double x:{double(candidate.x),candidate.x+(candidate.width-1)*.5,double(candidate.br().x-1)})
                for(double dy:{-double(plate.local.maxOffset),double(plate.local.maxOffset)})
                    for(double dx:{-double(plate.local.maxOffset),double(plate.local.maxOffset)}) {
                        const auto p=project(plate.inverse,x+dx,y+dy);
                        if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<2||p.y<2||p.x>size.width-3||p.y>size.height-3)return false;
                    }
        return true;
    };
    while(!supported()) {
        candidate={candidate.x+1,candidate.y+1,candidate.width-2,candidate.height-2};
        if(candidate.width<32||candidate.height<32||candidate.x>size.width*.05||candidate.y>size.height*.05||
           size.width-candidate.br().x>size.width*.05||size.height-candidate.br().y>size.height*.05)return false;
    }
    crop=candidate;return true;
}
fusion::LocalRegistration localPatches(const cv::Mat& reference,const cv::Mat& source,const cv::Matx33d& h,bool highlight,float gain=1) {
    fusion::LocalRegistration model;
    model.patchSupport=highlight;model.domain=reference.size();
    const int nx=(reference.cols-1+model.spacing-1)/model.spacing+1;
    const int ny=(reference.rows-1+model.spacing-1)/model.spacing+1;
    model.field=cv::Mat::zeros(ny,nx,CV_32FC3);model.detail=cv::Mat::zeros(ny,nx,CV_8U);
    // Only small source-resolution patches become floats. No full-frame float
    // band, native warped plate or per-input full-resolution mask is retained.
    #pragma omp parallel for schedule(dynamic)
    for(int gy=0;gy<ny;++gy)for(int gx=0;gx<nx;++gx) {
        const int cx=std::min(gx*model.spacing,reference.cols-1),cy=std::min(gy*model.spacing,reference.rows-1);
        const cv::Rect roi(std::max(0,cx-160),std::max(0,cy-160),
            std::min(reference.cols,cx+161)-std::max(0,cx-160),std::min(reference.rows,cy+161)-std::max(0,cy-160));
        if(roi.width<32||roi.height<32)continue;
        const cv::Matx33d shift(1,0,-roi.x,0,1,-roi.y,0,0,1);
        cv::Mat sourcePatch,rg,sg;
        cv::warpPerspective(source,sourcePatch,cv::Mat(shift*h),roi.size(),cv::INTER_LINEAR,cv::BORDER_CONSTANT);
        cv::cvtColor(reference(roi),rg,cv::COLOR_BGR2GRAY);
        cv::cvtColor(highlight ? fusion::normalizeExposure(sourcePatch,gain) : sourcePatch,sg,cv::COLOR_BGR2GRAY);
        const auto patch=fusion::registerLocal(rg,sg,highlight,true,highlight ? fusion::unclippedReference(reference(roi)) : cv::Mat());
        model.field.at<cv::Vec3f>(gy,gx)=patch.field.at<cv::Vec3f>(0,0);
        model.detail.at<uchar>(gy,gx)=patch.detail.at<uchar>(0,0);
    }
    int nodes=0,details=0;
    for(int y=0;y<ny;++y)for(int x=0;x<nx;++x) {
        const auto p=model.field.at<cv::Vec3f>(y,x);
        if(p[2]>0){++nodes;model.maxOffset=std::max(model.maxOffset,std::hypot(p[0],p[1]));}
        if(model.detail.at<uchar>(y,x))++details;
    }
    model.supportedFraction=float(nodes)/(nx*ny);model.detailFraction=float(details)/(nx*ny);
    model.denoiseOnlyFraction=float(nodes-details)/(nx*ny);
    // A bounded sample of native gray patches estimates the pixel gate; the
    // analysis image's reduced noise would understate native grain.
    std::vector<double> sigmas;
    for(int y=0;y<reference.rows;y+=1024)for(int x=0;x<reference.cols;x+=1024) {
        cv::Mat gray;cv::cvtColor(reference(cv::Rect(x,y,std::min(160,reference.cols-x),std::min(160,reference.rows-y))),gray,cv::COLOR_BGR2GRAY);
        sigmas.push_back(fusion::estimateNoiseSigma(gray));
    }
    std::sort(sigmas.begin(),sigmas.end());model.noiseSigma=float(sigmas[sigmas.size()/2]);
    return model;
}
bool sample(const cv::Mat& image,cv::Point2d p,fusion::Color& c) {
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.y<0||p.x>image.cols-1||p.y>image.rows-1)return false;
    const int x=int(p.x),y=int(p.y),xx=std::min(x+1,image.cols-1),yy=std::min(y+1,image.rows-1);
    const float fx=p.x-x,fy=p.y-y;
    for(int k=0;k<3;++k)c[k]=(1-fy)*((1-fx)*image.at<cv::Vec3b>(y,x)[k]+fx*image.at<cv::Vec3b>(y,xx)[k])+
        fy*((1-fx)*image.at<cv::Vec3b>(yy,x)[k]+fx*image.at<cv::Vec3b>(yy,xx)[k]);
    return true;
}
}

cv::Mat BurstFusionEngine::fuseBurstStream(size_t count,const std::function<cv::Mat(size_t)>& loader,
        const std::vector<int>& tiers,const std::vector<double>& exposures,Report* report) {
    const auto nativeStarted=std::chrono::steady_clock::now();Performance performance;
    if(!count||count>7||tiers.size()!=count||exposures.size()!=count||tiers[0]!=0)
        throw std::invalid_argument("Invalid streamed burst metadata");
    if(std::count(tiers.begin(),tiers.end(),0)>4)throw std::invalid_argument("At most four base frames");
    size_t reference=0;double best=-1;cv::Size size;
    for(size_t i=0;i<count;++i)if(tiers[i]==0) {
        auto frame=loader(i);
        if(frame.empty()||frame.type()!=CV_8UC3)continue;
        if(!size.area())size=frame.size();
        if(frame.size()!=size)continue;
        const auto small=analysis(frame);
        double score=fusion::referenceQuality(small);
        if(exposures[i]>0&&exposures[0]>0)score/=1+std::max(0.,std::abs(std::log(exposures[i]/exposures[0]))-.15);
        STREAM_LOG("Stream reference candidate %zu quality=%.4f",i,score);
        if(score>best*1.000001){best=score;reference=i;}
    }
    if(best<0||size.area()<=0||int64_t(size.width)*size.height>56000000)throw std::runtime_error("No valid native-size reference");
    auto ref=loader(reference);const auto reduced=analysis(ref);
    std::vector<StreamPlate> bases,highlights;
    cv::Rect crop(0,0,size.width,size.height);
    Report achieved;achieved.reference=int(reference);achieved.sourceSize=size;achieved.scale=1;
    achieved.geometryKnown=reference==0;
    for(size_t i=0;i<count;++i) {
        if(i==reference)continue;
        auto source=loader(i);
        if(source.empty()||source.type()!=ref.type()||source.size()!=size)continue;
        const auto srcSmall=analysis(source);cv::Mat warped,h;
        const double gain=exposures[i]>0 ? exposures[reference]/exposures[i] : 0;
        if(tiers[i]!=0 && (gain<=1.25 || !std::isfinite(gain)))continue;
        const auto globalStarted=std::chrono::steady_clock::now();
        const bool aligned=tiers[i]==0 ? alignBaseFrame(srcSmall,reduced,warped,h):
            alignHighlightFrame(srcSmall,reduced,warped,h,gain);
        performance.globalRegistrationMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-globalStarted).count();
        if(!aligned||h.empty()||!cv::checkRange(h)||std::abs(cv::determinant(h))<1e-9)continue;
        const auto fullH=fullTransform(h,size,reduced.size());
        if(i==0){achieved.firstToReference=fullH;achieved.geometryKnown=true;}
        const float jpegGain=tiers[i]!=0 ? fusion::responseGain(reduced,warped,gain) : 1;
        const auto localStarted=std::chrono::steady_clock::now();
        StreamPlate plate{i,fullH.inv(),localPatches(ref,source,fullH,tiers[i]!=0,jpegGain),jpegGain};
        performance.localRegistrationMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-localStarted).count();
        STREAM_LOG("Stream frame=%zu tier=%d nodeSupport=%.3f detail=%.3f denoiseOnly=%.3f sigma=%.2f",i,tiers[i],plate.local.supportedFraction,plate.local.detailFraction,plate.local.denoiseOnlyFraction,plate.local.noiseSigma);
        if(plate.local.supportedFraction<.005f)continue;
        if(tiers[i]==0) {
            auto next=crop;if(!cropFor(plate,size,next))continue;
            crop=next;bases.push_back(std::move(plate));
        } else if(exposures[i]>0&&exposures[reference]/exposures[i]>1.25) {
            if(plate.gain>1.1f)highlights.push_back(std::move(plate));
        }
    }
    cv::Mat sums;ref(crop).convertTo(sums,CV_16UC3);
    cv::Mat counts(crop.size(),CV_8U,cv::Scalar(1));
    int admitted=1;
    for(const auto& plate:bases) {
        auto source=loader(plate.index);if(source.empty()||source.size()!=size||source.type()!=CV_8UC3)continue;
        long long contributed=0;
        #pragma omp parallel for schedule(static) reduction(+:contributed)
        for(int y=0;y<crop.height;++y)for(int x=0;x<crop.width;++x) {
            const int rx=crop.x+x,ry=crop.y+y;
            cv::Point2d d;float confidence;
            if(!plate.local.offset(rx,ry,d,confidence))continue;
            fusion::Color observed;if(!sample(source,project(plate.inverse,rx+d.x,ry+d.y),observed))continue;
            const auto baseline=ref.at<cv::Vec3b>(ry,rx);
            const float threshold=std::clamp(6.35f*plate.local.noiseSigma,14.f,64.f);
            float difference=0;for(int c=0;c<3;++c)difference=std::max(difference,std::abs(observed[c]-baseline[c]));
            if(difference>threshold)continue;
            auto& total=sums.at<cv::Vec3w>(y,x);
            for(int c=0;c<3;++c)total[c]+=cv::saturate_cast<uchar>(observed[c]);
            ++counts.at<uchar>(y,x);++contributed;
        }
        if(contributed>0)++admitted;
        STREAM_LOG("Stream frame=%zu contributingPixels=%lld",plate.index,contributed);
    }
    ref.release();
    cv::Mat output(crop.size(),CV_8UC3);long long totalContributors=0,multiPixels=0;
    #pragma omp parallel for schedule(static) reduction(+:totalContributors,multiPixels)
    for(int y=0;y<crop.height;++y)for(int x=0;x<crop.width;++x) {
        const int n=counts.at<uchar>(y,x);const auto sum=sums.at<cv::Vec3w>(y,x);
        auto& out=output.at<cv::Vec3b>(y,x);for(int c=0;c<3;++c)out[c]=cv::saturate_cast<uchar>(float(sum[c])/n);
        totalContributors+=n;if(n>=2)++multiPixels;
    }
    sums.release();counts.release();
    std::stable_sort(highlights.begin(),highlights.end(),[](const auto& a,const auto& b){return a.gain<b.gain;});
    long long clippedPixels=0;
    for(int y=0;y<output.rows;++y)for(int x=0;x<output.cols;++x) {
        const auto p=output.at<cv::Vec3b>(y,x);if(std::max({p[0],p[1],p[2]})>=250)++clippedPixels;
    }
    cv::Mat recovered(output.size(),CV_8U,cv::Scalar(0));int usedHighlights=0;
    for(const auto& plate:highlights) {
        auto source=loader(plate.index);if(source.empty()||source.size()!=size||source.type()!=CV_8UC3)continue;
        const auto neighborhood=fusion::highlightNeighborhood(source,plate.gain);
        long long used=0;
        #pragma omp parallel for schedule(static) reduction(+:used)
        for(int y=0;y<output.rows;++y)for(int x=0;x<output.cols;++x) {
            if(recovered.at<uchar>(y,x))continue;
            auto& pixel=output.at<cv::Vec3b>(y,x);if(std::max({pixel[0],pixel[1],pixel[2]})<=230)continue;
            const int rx=crop.x+x,ry=crop.y+y;cv::Point2d d;float confidence;
            if(!plate.local.offset(rx,ry,d,confidence))continue;
            const auto sourcePoint=project(plate.inverse,rx+d.x,ry+d.y);
            fusion::Color shortColor;if(!sample(source,sourcePoint,shortColor))continue;
            const float peak=*std::max_element(shortColor.begin(),shortColor.end());
            const fusion::Color baseline{float(pixel[0]),float(pixel[1]),float(pixel[2])};
            const bool observedDark=std::max({pixel[0],pixel[1],pixel[2]})>=247 &&
                neighborhood.at<uchar>(std::clamp(int(std::round(sourcePoint.y)),0,source.rows-1),
                    std::clamp(int(std::round(sourcePoint.x)),0,source.cols-1));
            if(peak>=246||(!observedDark&&(peak<3||fusion::linear(peak)*plate.gain<.60f))||
                !fusion::compatibleHighlight(baseline,shortColor,plate.gain))continue;
            const auto color=fusion::recoverHighlight(baseline,shortColor,plate.gain,observedDark);
            float change=0;for(int c=0;c<3;++c)change=std::max(change,std::abs(color[c]-pixel[c]));
            if(change<.5f)continue;
            for(int c=0;c<3;++c)pixel[c]=cv::saturate_cast<uchar>(color[c]);recovered.at<uchar>(y,x)=255;++used;
        }
        if(used)++usedHighlights;
    }
    achieved.anchors=admitted;achieved.highlights=usedHighlights;achieved.crop=crop;
    achieved.multiFrameFraction=double(multiPixels)/output.total();achieved.meanContributors=double(totalContributors)/output.total();
    achieved.recoveredFraction=double(cv::countNonZero(recovered))/output.total();achieved.clippedFraction=double(clippedPixels)/output.total();
    long long postClipped=0;
    #pragma omp parallel for reduction(+:postClipped)
    for(int y=0;y<output.rows;++y)for(int x=0;x<output.cols;++x){const auto p=output.at<cv::Vec3b>(y,x);if(std::max({p[0],p[1],p[2]})>=250)++postClipped;}
    achieved.postClippedFraction=double(postClipped)/output.total();
    STREAM_LOG("Highlight pixels: recoveredFraction=%.6f clippedFraction=%.6f",achieved.recoveredFraction,achieved.clippedFraction);
    STREAM_LOG("Stream fusion: native=%dx%d anchors=%d highlights=%d scale=1 crop=%d,%d,%d,%d multiFrameFraction=%.4f meanContributors=%.3f",size.width,size.height,admitted,usedHighlights,crop.x,crop.y,crop.width,crop.height,achieved.multiFrameFraction,achieved.meanContributors);
    performance.totalMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-nativeStarted).count();
    achieved.performance=performance;
    if(report)*report=achieved;
    return output;
}
