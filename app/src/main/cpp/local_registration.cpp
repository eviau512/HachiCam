#include "local_registration.h"
#include "fusion_math.h"
#include "local_support.h"
#include "gpu_compute.h"
#include <chrono>
#include <android/log.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <cstdio>

namespace fusion {
cv::Mat highlightNeighborhood(const cv::Mat& image,float gain) {
    CV_Assert(image.type()==CV_8UC3);
    cv::Mat seeds(image.size(),CV_8U,cv::Scalar(0));
    for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x) {
        const auto p=image.at<cv::Vec3b>(y,x);
        const int peak=std::max({p[0],p[1],p[2]});
        if(peak>=3 && peak<246 && linear(float(peak))*gain>=.60f)seeds.at<uchar>(y,x)=255;
    }
    cv::Mat neighborhood;
    cv::morphologyEx(seeds,neighborhood,cv::MORPH_CLOSE,cv::getStructuringElement(cv::MORPH_ELLIPSE,{25,25}),{-1,-1},1,cv::BORDER_CONSTANT,cv::Scalar(0));
    return neighborhood;
}
cv::Mat unclippedReference(const cv::Mat& image) {
    CV_Assert(image.type()==CV_8UC3);
    cv::Mat valid(image.size(),CV_8U);
    for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x) {
        const auto p=image.at<cv::Vec3b>(y,x);
        valid.at<uchar>(y,x)=std::max({p[0],p[1],p[2]})<247 ? 255 : 0;
    }
    return valid;
}
cv::Mat normalizeExposure(const cv::Mat& image, double gain) {
    CV_Assert(image.depth()==CV_8U);
    cv::Mat lut(1,256,CV_8U), output;
    for(int i=0;i<256;++i)lut.at<uchar>(i)=cv::saturate_cast<uchar>(encoded(std::min(1.,linear(float(i))*gain)));
    cv::LUT(image,lut,output);return output;
}
// Shared unclipped JPEG response is only an approximation of ISP tonemapping.
float responseGain(const cv::Mat& base,const cv::Mat& aligned,double fallback) {
    std::vector<float> ratios;
    const int step=std::max(1,std::max(base.rows,base.cols)/100);
    for(int y=0;y<base.rows;y+=step)for(int x=0;x<base.cols;x+=step) {
        const auto b=base.at<cv::Vec3b>(y,x),s=aligned.at<cv::Vec3b>(y,x);
        const int bp=std::max({b[0],b[1],b[2]}),sp=std::max({s[0],s[1],s[2]});
        if(bp<40||bp>220||sp<12||sp>220)continue;
        const float bl=.114f*linear(b[0])+.587f*linear(b[1])+.299f*linear(b[2]);
        const float sl=.114f*linear(s[0])+.587f*linear(s[1])+.299f*linear(s[2]);
        if(sl>1e-5f)ratios.push_back(bl/sl);
    }
    if(ratios.size()<64)return std::clamp(float(fallback),1.f,256.f);
    auto mid=ratios.begin()+ratios.size()/2;std::nth_element(ratios.begin(),mid,ratios.end());
    return std::clamp(*mid,1.f,256.f);
}
namespace {
cv::Mat highPass(const cv::Mat& gray) {
    cv::Mat f, low, band;
    gray.convertTo(f, CV_32F);
    // Compare a subpixel-stable band. Raw high-pass energy collapses under
    // legitimate interpolation and would reject the very phases SR needs.
    cv::GaussianBlur(f, band, cv::Size(0, 0), .8);
    cv::GaussianBlur(f, low, cv::Size(0, 0), 2.0);
    return band - low;
}
double correlation(const cv::Mat& a, const cv::Mat& b, const cv::Mat& mask, double noiseA, double noiseB, double& ratio) {
    if (cv::countNonZero(mask) < 256) { ratio = 0; return -1; }
    cv::Scalar ma, sa, mb, sb;
    cv::meanStdDev(a, ma, sa, mask); cv::meanStdDev(b, mb, sb, mask);
    const double va = std::max(0., sa[0]*sa[0]-noiseA), vb = std::max(0., sb[0]*sb[0]-noiseB);
    ratio = std::sqrt(vb / std::max(va,.01));
    return (cv::mean(a.mul(b), mask)[0] - ma[0] * mb[0]) / std::max(std::sqrt(va*vb), .01);
}
float graySample(const cv::Mat& image, double x, double y) {
    if (x < 0 || y < 0 || x > image.cols - 1 || y > image.rows - 1) return -1000;
    const int ix = int(x), iy = int(y);
    const int nx = std::min(ix + 1, image.cols - 1), ny = std::min(iy + 1, image.rows - 1);
    const float fx = x - ix, fy = y - iy;
    return (1 - fy) * ((1 - fx) * image.at<uchar>(iy, ix) + fx * image.at<uchar>(iy, nx)) +
        fy * ((1 - fx) * image.at<uchar>(ny, ix) + fx * image.at<uchar>(ny, nx));
}
}

double estimateNoiseSigma(const cv::Mat& gray) {
    CV_Assert(gray.type()==CV_8UC1);
    double estimate=0;
    for(int separation:{1,2,4}) {
        std::vector<float> differences;
        differences.reserve((gray.cols/4+1)*(gray.rows/4+1));
        for(int y=0;y+separation<gray.rows;y+=4) for(int x=0;x+separation<gray.cols;x+=4) {
            const int a=gray.at<uchar>(y,x),b=gray.at<uchar>(y,x+separation);
            const int c=gray.at<uchar>(y+separation,x),d=gray.at<uchar>(y+separation,x+separation);
            if(std::min({a,b,c,d})<4 || std::max({a,b,c,d})>251)continue;
            differences.push_back(std::abs((a-b-c+d)*.5f));
        }
        if(differences.size()<16)continue;
        auto middle=differences.begin()+differences.size()/2;
        std::nth_element(differences.begin(),middle,differences.end());
        estimate=std::max(estimate,double(*middle)/.67448975);
    }
    return std::clamp(estimate,0.,24.);
}

double referenceQuality(const cv::Mat& image) {
    cv::Mat gray, smooth, lap;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, smooth, cv::Size(0, 0), .7);
    cv::Laplacian(smooth, lap, CV_32F, 3);
    std::vector<double> tiles;
    for (int y = 0; y < gray.rows; y += 256) for (int x = 0; x < gray.cols; x += 256) {
        const cv::Rect r(x, y, std::min(256, gray.cols - x), std::min(256, gray.rows - y));
        cv::Scalar mean, sd;
        cv::meanStdDev(lap(r), mean, sd);
        const double sigma=estimateNoiseSigma(gray(r));
        // Noise must not win sharpness ranking merely through Laplacian energy.
        static const double noiseGain=[] {
            cv::Mat impulse=cv::Mat::zeros(25,25,CV_32F),s,l;
            impulse.at<float>(12,12)=1;
            cv::GaussianBlur(impulse,s,cv::Size(0,0),.7);cv::Laplacian(s,l,CV_32F,3);
            return cv::sum(l.mul(l))[0];
        }();
        tiles.push_back(std::max(0.,sd[0]*sd[0]-noiseGain*sigma*sigma)/(1.+.02*sigma*sigma));
    }
    std::sort(tiles.begin(), tiles.end());
    const double detail = tiles[std::min(tiles.size() - 1, tiles.size() * 3 / 4)];
    const double clipped = double(cv::countNonZero((gray <= 3) | (gray >= 252))) / gray.total();
    return detail * (1 - .5 * clipped);
}

bool LocalRegistration::hasDetail(double x, double y) const {
    if(detail.empty() || !std::isfinite(x) || !std::isfinite(y)) return false;
    const double gx=std::clamp(x/spacing,0.,double(detail.cols-1));
    const double gy=std::clamp(y/spacing,0.,double(detail.rows-1));
    const int x0=int(gx),y0=int(gy),x1=std::min(x0+1,detail.cols-1),y1=std::min(y0+1,detail.rows-1);
    const float fx=gx-x0,fy=gy-y0;
    return ((1-fx)*(1-fy)<=.001f || detail.at<uchar>(y0,x0)) &&
           (fx*(1-fy)<=.001f || detail.at<uchar>(y0,x1)) &&
           ((1-fx)*fy<=.001f || detail.at<uchar>(y1,x0)) &&
           (fx*fy<=.001f || detail.at<uchar>(y1,x1));
}
bool LocalRegistration::offset(double x, double y, cv::Point2d& delta, float& confidence) const {
    if (field.empty()) { delta = {0, 0}; confidence = 1; return true; }
    if(patchSupport) {
        const int gx=int(std::round(x/spacing)),gy=int(std::round(y/spacing));
        cv::Vec3f selected(0,0,0);double best=1e30;
        cv::Vec3f candidates[9];int candidateCount=0;
        for(int yy=std::max(0,gy-1);yy<=std::min(field.rows-1,gy+1);++yy)
            for(int xx=std::max(0,gx-1);xx<=std::min(field.cols-1,gx+1);++xx) {
                const auto p=field.at<cv::Vec3f>(yy,xx);
                if(p[2]<=0)continue;
                const double dx=x-std::min(xx*spacing,domain.width-1),dy=y-std::min(yy*spacing,domain.height-1);
                if(std::abs(dx)>140||std::abs(dy)>140)continue;
                for(int i=0;i<candidateCount;++i)
                    if(std::hypot(p[0]-candidates[i][0],p[1]-candidates[i][1])>1.5)return false;
                candidates[candidateCount++]=p;
                const double distance=dx*dx+dy*dy;
                if(distance<best){best=distance;selected=p;}
            }
        if(selected[2]<=0)return false;
        delta={selected[0],selected[1]};confidence=selected[2];return true;
    }
    const double gx = std::clamp(x / spacing, 0.0, double(field.cols - 1));
    const double gy = std::clamp(y / spacing, 0.0, double(field.rows - 1));
    const int x0 = int(gx), y0 = int(gy), x1 = std::min(x0 + 1, field.cols - 1), y1 = std::min(y0 + 1, field.rows - 1);
    const float fx = gx - x0, fy = gy - y0;
    const cv::Vec3f p[] = {field.at<cv::Vec3f>(y0, x0), field.at<cv::Vec3f>(y0, x1),
                          field.at<cv::Vec3f>(y1, x0), field.at<cv::Vec3f>(y1, x1)};
    const float weights[] = {(1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy};
    cv::Vec3f sum(0, 0, 0);
    for (int i = 0; i < 4; ++i) {
        if (weights[i] <= .001f) continue;
        if (p[i][2] <= 0) return false;
        for (int j = 0; j < i; ++j) {
            if (weights[j] > .001f && std::hypot(p[i][0]-p[j][0], p[i][1]-p[j][1]) > 1.5f)
                return false; // Do not interpolate a motion discontinuity across text.
        }
        sum += p[i] * weights[i];
    }
    delta = {sum[0], sum[1]}; confidence = sum[2];
    return confidence > 0;
}

ReferencePreparation prepareReference(const cv::Mat& reference) {
    CV_Assert(reference.type()==CV_8UC1 && !reference.empty());
    ReferencePreparation cache;cache.identity=reference.data;cache.domain=reference.size();
    cache.highPass=highPass(reference);
    cv::GaussianBlur(reference,cache.align,cv::Size(0,0),1.2);
    cv::GaussianBlur(reference,cache.smooth,cv::Size(0,0),.6);
    cache.noiseSigma=float(estimateNoiseSigma(reference));
    const int nx=(reference.cols-1+255)/256+1,ny=(reference.rows-1+255)/256+1;
    std::string backend;
    if(gpuNoiseGrid(reference,256,{},cache.noise,backend)) {
        cache.gpuNoiseGrid=backend=="validated GPU noise histogram";
        __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","reference noise grid backend=%s",backend.c_str());
        return cache;
    }
    cache.noise.create(ny,nx,CV_64F);
    #pragma omp parallel for schedule(dynamic)
    for(int gy=0;gy<ny;++gy)for(int gx=0;gx<nx;++gx) {
        const int cx=std::min(gx*256,reference.cols-1),cy=std::min(gy*256,reference.rows-1);
        const cv::Rect roi(std::max(0,cx-160),std::max(0,cy-160),
            std::min(reference.cols,cx+161)-std::max(0,cx-160),std::min(reference.rows,cy+161)-std::max(0,cy-160));
        cache.noise.at<double>(gy,gx)=estimateNoiseSigma(reference(roi));
    }
    return cache;
}

cv::Mat highlightDemand(const cv::Mat& native,const cv::Rect& crop,cv::Size domain) {
    CV_Assert(native.type()==CV_8UC3 && native.size()==crop.size() &&
        (crop & cv::Rect({},domain))==crop);
    cv::Mat mask=cv::Mat::zeros(domain,CV_8U);
    #pragma omp parallel for schedule(static)
    for(int y=0;y<native.rows;++y)for(int x=0;x<native.cols;++x) {
        const auto p=native.at<cv::Vec3b>(y,x);
        if(std::max({p[0],p[1],p[2]})>=247)mask.at<uchar>(y+crop.y,x+crop.x)=255;
    }
    return mask;
}
cv::Mat highlightFitNodes(const cv::Mat& requested,int spacing) {
    CV_Assert(requested.type()==CV_8U && !requested.empty() && spacing>0);
    const int nx=(requested.cols-1+spacing-1)/spacing+1,ny=(requested.rows-1+spacing-1)/spacing+1;
    cv::Mat nodes=cv::Mat::zeros(ny,nx,CV_8U);
    for(int gy=0;gy<ny;++gy)for(int gx=0;gx<nx;++gx) {
        const int cx=std::min(gx*spacing,requested.cols-1),cy=std::min(gy*spacing,requested.rows-1);
        const cv::Rect support(std::max(0,cx-140),std::max(0,cy-140),
            std::min(requested.cols,cx+141)-std::max(0,cx-140),std::min(requested.rows,cy+141)-std::max(0,cy-140));
        if(cv::countNonZero(requested(support)))nodes.at<uchar>(gy,gx)=255;
    }
    return nodes;
}

LocalRegistration registerLocal(const cv::Mat& reference, const cv::Mat& warped, bool highlight, bool singlePatch, const cv::Mat& referenceValidity, const ReferencePreparation* prepared, const cv::Mat& requestedPixels) {
    const auto started=std::chrono::steady_clock::now();
    CV_Assert(reference.type() == CV_8UC1 && reference.size() == warped.size() && warped.type() == CV_8UC1);
    CV_Assert(referenceValidity.empty() || (referenceValidity.type()==CV_8U && referenceValidity.size()==reference.size()));
    LocalRegistration model;
    model.patchSupport=highlight;model.domain=reference.size();
    const cv::Mat& geometryValidity=referenceValidity;
    const int nx = singlePatch ? 1 : (reference.cols - 1 + model.spacing - 1) / model.spacing + 1;
    const int ny = singlePatch ? 1 : (reference.rows - 1 + model.spacing - 1) / model.spacing + 1;
    model.field = cv::Mat::zeros(ny, nx, CV_32FC3);
    model.detail = cv::Mat::zeros(ny,nx,CV_8U);
    CV_Assert(requestedPixels.empty() || (highlight && !singlePatch && requestedPixels.type()==CV_8U && requestedPixels.size()==reference.size()));
    const cv::Mat activeNodes=requestedPixels.empty() ? cv::Mat() : highlightFitNodes(requestedPixels,model.spacing);
    const int activeCount=activeNodes.empty() ? nx*ny : cv::countNonZero(activeNodes);
    if(!activeCount) {model.gate=cv::Mat::zeros(reference.size(),CV_8U);return model;}
    const bool cached=!singlePatch && prepared && prepared->identity==reference.data && prepared->domain==reference.size() && prepared->noise.size()==model.field.size();
    const cv::Mat refHP = cached ? prepared->highPass : highPass(reference), srcHP = highPass(warped);
    cv::Mat refAlign,srcAlign;
    if(cached)refAlign=prepared->align;else cv::GaussianBlur(reference,refAlign,cv::Size(0,0),1.2);
    cv::GaussianBlur(warped,srcAlign,cv::Size(0,0),1.2);
    static const double noiseGain=[] {
        cv::Mat impulse=cv::Mat::zeros(25,25,CV_32F);impulse.at<float>(12,12)=1;
        const auto band=highPass(impulse);return cv::sum(band.mul(band))[0];
    }();
    model.noiseSigma=cached ? prepared->noiseSigma : float(estimateNoiseSigma(reference));
    struct NodePreparation {double refNoise=0,srcNoise=0,refVariance=0,srcVariance=0;};
    cv::Mat sourceNoise;std::string noiseBackend="CPU noise";
    if(!singlePatch && gpuNoiseGrid(warped,model.spacing,activeNodes,sourceNoise,noiseBackend))
        model.gpuNoiseGrid=noiseBackend=="validated GPU noise histogram";
    __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","local source noise backend=%s",noiseBackend.c_str());
    std::vector<NodePreparation> nodes(nx*ny);
    int preparedNodes=0;
    #pragma omp parallel for schedule(dynamic) reduction(+:preparedNodes)
    for(int gy=0;gy<ny;++gy)for(int gx=0;gx<nx;++gx) {
        if(!activeNodes.empty() && !activeNodes.at<uchar>(gy,gx))continue;
        const int cx=singlePatch ? reference.cols/2 : std::min(gx*model.spacing,reference.cols-1);
        const int cy=singlePatch ? reference.rows/2 : std::min(gy*model.spacing,reference.rows-1);
        const cv::Rect roi(std::max(0,cx-160),std::max(0,cy-160),
            std::min(reference.cols,cx+161)-std::max(0,cx-160),std::min(reference.rows,cy+161)-std::max(0,cy-160));
        if(roi.width<32 || roi.height<32)continue;
        auto& node=nodes[gy*nx+gx];
        ++preparedNodes;
        node.refNoise=cached ? prepared->noise.at<double>(gy,gx) : estimateNoiseSigma(reference(roi));
        node.srcNoise=sourceNoise.empty() ? estimateNoiseSigma(warped(roi)) : sourceNoise.at<double>(gy,gx);
        const cv::Rect interior(roi.x+10,roi.y+10,roi.width-20,roi.height-20);
        cv::Scalar mean,a,b;cv::meanStdDev(refHP(interior),mean,a);cv::meanStdDev(srcHP(interior),mean,b);
        node.refVariance=a[0]*a[0];node.srcVariance=b[0]*b[0];
    }
    const auto preparedAt=std::chrono::steady_clock::now();
    std::vector<cv::Vec4f> fittedNodes(nx*ny,cv::Vec4f(0,0,-1,0));
    std::string fitBackend="CPU fitting";
    if(!singlePatch && gpuFittingAvailable(reference.total(),highlight)) {
        const auto selectionStarted=std::chrono::steady_clock::now();
        std::vector<cv::Rect> candidates;std::vector<int> candidateIndices;std::vector<cv::Vec2d> candidateNoise;
        for(int gy=0;gy<ny;++gy)for(int gx=0;gx<nx;++gx) {
            if(!activeNodes.empty() && !activeNodes.at<uchar>(gy,gx))continue;
            const int cx=std::min(gx*model.spacing,reference.cols-1),cy=std::min(gy*model.spacing,reference.rows-1);
            const cv::Rect roi(std::max(0,cx-160),std::max(0,cy-160),
                std::min(reference.cols,cx+161)-std::max(0,cx-160),std::min(reference.rows,cy+161)-std::max(0,cy-160));
            if(roi.width<32 || roi.height<32)continue;
            const auto& node=nodes[gy*nx+gx];
            const double rn=node.refNoise,sn=node.srcNoise;
            const double rs=node.refVariance-noiseGain*rn*rn,ss=node.srcVariance-noiseGain*sn*sn;
            if(node.refVariance<4 || node.srcVariance<4 ||
               rs<std::max(2.25,4*noiseGain*rn*rn) || ss<std::max(2.25,4*noiseGain*sn*sn))continue;
            candidates.push_back(roi);candidateIndices.push_back(gy*nx+gx);
            candidateNoise.push_back({noiseGain*rn*rn,noiseGain*sn*sn});
        }
        std::vector<uchar> eligible(candidates.size(),0);GpuSelectionStats selection;
        std::string selectorBackend="CPU selector";
        const bool selected=!candidates.empty() && gpuStrongSelection(reference,warped,refHP,srcHP,geometryValidity,candidates,candidateNoise,eligible,selection,selectorBackend);
        if(!selected) {
            #pragma omp parallel for schedule(dynamic)
            for(int i=0;i<int(candidates.size());++i)eligible[i]=strongPatchEligible(reference,warped,refHP,srcHP,geometryValidity,candidates[i],candidateNoise[i]);
        }
        std::vector<cv::Rect> patches;std::vector<int> indices;
        for(size_t i=0;i<candidates.size();++i)if(eligible[i]) {
            patches.push_back(candidates[i]);indices.push_back(candidateIndices[i]);
        }
        const double selectorMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-selectionStarted).count();
        const double selectorEstimate=std::max(0.,selectorMs-selection.ms)+selection.estimateMs;
        model.gpuSelectorPatches=selection.patches;model.cpuSelectorPatches=selection.cpuPatches;model.selectorMs=selectorMs;
        std::vector<cv::Vec4f> proposals;
        if(!patches.empty() && gpuPatchFits(refAlign,srcAlign,warped,geometryValidity,patches,highlight,proposals,fitBackend,selectorEstimate))
            for(size_t i=0;i<indices.size();++i)fittedNodes[indices[i]]=proposals[i];
        if(!patches.empty())__android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU strong scheduling candidates=%zu prequalified=%zu selectorMs=%.3f selectorEstimateMs=%.3f selector=%s reason=%s",patches.size(),candidates.size(),selectorMs,selectorEstimate,selectorBackend.c_str(),fitBackend.c_str());
    }
    int gpuFitCount=0;
    for(const auto& fit:fittedNodes)if(fit[3]==1)++gpuFitCount;
    model.gpuFitProposals=gpuFitCount;
    __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","local fit GPU proposals=%d nodes=%d preparedNodes=%d reason=%s",gpuFitCount,nx*ny,preparedNodes,fitBackend.c_str());
    int gpuPhysicalFallbacks=0,cpuRetryChecks=0;
    // Keep ECC and its independent physical validation separate so the latter
    // can warp/reduce a bounded batch without per-node GPU synchronization.
    std::vector<cv::Vec4f> pending(nx*ny,cv::Vec4f(0,0,-1,0));
    std::vector<int> rejection(nx*ny,0);std::vector<double> fitCorrelation(nx*ny,0);
    auto patchAt=[&](int index) {
        const int gx=index%nx,gy=index/nx;
        const int cx=singlePatch ? reference.cols/2 : std::min(gx*model.spacing,reference.cols-1);
        const int cy=singlePatch ? reference.rows/2 : std::min(gy*model.spacing,reference.rows-1);
        return cv::Rect(std::max(0,cx-160),std::max(0,cy-160),
            std::min(reference.cols,cx+161)-std::max(0,cx-160),std::min(reference.rows,cy+161)-std::max(0,cy-160));
    };
    auto fitCpu=[&](const cv::Rect& roi,cv::Vec4f& result,double& correlationValue) {
        try {
            cv::Mat mask=localFitMask(warped,geometryValidity,roi);
            if(cv::countNonZero(mask)<256)return 3;
            cv::Mat t=cv::Mat::eye(2,3,CV_32F);
            const double cc=cv::findTransformECC(refAlign(roi),srcAlign(roi),t,cv::MOTION_TRANSLATION,
                cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.0001),mask,3);
            const float dx=t.at<float>(0,2),dy=t.at<float>(1,2);
            if(!std::isfinite(cc)||cc<.65||!std::isfinite(dx)||!std::isfinite(dy)||std::hypot(dx,dy)>8)return 5;
            result={dx,dy,float(cc),2};correlationValue=cc;return 0;
        } catch(const cv::Exception&){return 4;}
    };
    const auto proposalsStarted=std::chrono::steady_clock::now();
    #pragma omp parallel for schedule(dynamic)
    for(int index=0;index<nx*ny;++index) {
        const int gx=index%nx,gy=index/nx;
        if(!activeNodes.empty() && !activeNodes.at<uchar>(gy,gx))continue;
        const auto roi=patchAt(index);if(roi.width<32||roi.height<32)continue;
        const auto& node=nodes[index];
        const double refSignal=std::max(0.,node.refVariance-noiseGain*node.refNoise*node.refNoise);
        const double srcSignal=std::max(0.,node.srcVariance-noiseGain*node.srcNoise*node.srcNoise);
        const cv::Rect interior(roi.x+10,roi.y+10,roi.width-20,roi.height-20);
        if(refSignal<2.25 && srcSignal<2.25) {
            cv::Mat difference;cv::absdiff(refAlign(interior),srcAlign(interior),difference);
            if(!highlight && cv::mean(difference)[0]<std::max(3.,.28*std::hypot(node.refNoise,node.srcNoise)))
                model.field.at<cv::Vec3f>(gy,gx)={0,0,.85f};
            rejection[index]=1;continue;
        }
        if(highlight) {
            cv::Mat mask=cv::Mat::zeros(roi.size(),CV_8U);
            mask(cv::Rect(10,10,roi.width-20,roi.height-20)).setTo(255);
            mask &= (reference(roi)>8)&(reference(roi)<247);
            if(!geometryValidity.empty())mask &= geometryValidity(roi);
            double ratio=0;
            const double score=correlation(refHP(roi),srcHP(roi),mask,
                noiseGain*node.refNoise*node.refNoise,noiseGain*node.srcNoise*node.srcNoise,ratio);
            if(score>=.95 && ratio>=.68) {
                model.field.at<cv::Vec3f>(gy,gx)={0,0,float(std::min(1.,score))};
                model.detail.at<uchar>(gy,gx)=255;rejection[index]=2;continue;
            }
        }
        if(fittedNodes[index][3]>=1){pending[index]=fittedNodes[index];fitCorrelation[index]=pending[index][2];}
        else rejection[index]=fitCpu(roi,pending[index],fitCorrelation[index]);
    }
    std::vector<PhysicalPatch> checks;std::vector<int> checkedIndices;
    for(int index=0;index<nx*ny;++index)if(pending[index][3]) {
        const auto& node=nodes[index];const auto p=pending[index];
        checks.push_back({patchAt(index),{p[0],p[1]},
            {noiseGain*node.refNoise*node.refNoise,noiseGain*node.srcNoise*node.srcNoise}});
        checkedIndices.push_back(index);
    }
    const auto physicalStarted=std::chrono::steady_clock::now();
    const double proposalMs=std::chrono::duration<double,std::milli>(physicalStarted-proposalsStarted).count();
    std::vector<cv::Vec2d> metrics;GpuPhysicalStats physicalStats;std::string physicalReason="no pending fits";
    const bool accelerated=!singlePatch && !checks.empty() &&
        gpuPhysicalChecks(reference,refHP,srcHP,geometryValidity,checks,highlight,metrics,physicalStats,physicalReason);
    if(!accelerated) {
        metrics.resize(checks.size());
        #pragma omp parallel for schedule(dynamic)
        for(int i=0;i<int(checks.size());++i)metrics[i]=cpuPhysicalCheck(reference,refHP,srcHP,geometryValidity,checks[i]);
        physicalStats.cpuPatches=int(checks.size());
    }
    #pragma omp parallel for schedule(dynamic) reduction(+:gpuPhysicalFallbacks,cpuRetryChecks)
    for(int i=0;i<int(checks.size());++i) {
        const int index=checkedIndices[i],gx=index%nx,gy=index/nx;auto proposal=pending[index];
        auto m=metrics[i];const auto& node=nodes[index];
        auto admitted=[&] {
            const bool strict=m[0]>=(highlight ? .80 : .90);
            const bool denoiseOnly=!highlight && std::max(node.refNoise,node.srcNoise)>1 && fitCorrelation[index]>=.95 && m[0]>=.80;
            return (strict||denoiseOnly) && m[1]>=.68;
        };
        if(!admitted() && proposal[3]==1) {
            ++gpuPhysicalFallbacks;const int failed=fitCpu(checks[i].roi,proposal,fitCorrelation[index]);
            if(failed){rejection[index]=failed;continue;}
            auto retry=checks[i];retry.shift={proposal[0],proposal[1]};
            m=cpuPhysicalCheck(reference,refHP,srcHP,geometryValidity,retry);++cpuRetryChecks;
        }
        if(!admitted()){rejection[index]=m[1]<.68 ? 7 : 6;continue;}
        model.field.at<cv::Vec3f>(gy,gx)={proposal[0],proposal[1],float(std::clamp(m[0],0.,1.))};
        const double refSignal=std::max(0.,node.refVariance-noiseGain*node.refNoise*node.refNoise);
        const double srcSignal=std::max(0.,node.srcVariance-noiseGain*node.srcNoise*node.srcNoise);
        if(m[0]>=(highlight ? .80 : .90) && refSignal>=std::max(2.25,4*noiseGain*node.refNoise*node.refNoise) &&
            srcSignal>=std::max(2.25,4*noiseGain*node.srcNoise*node.srcNoise))model.detail.at<uchar>(gy,gx)=255;
        rejection[index]=8;
    }
    physicalStats.cpuPatches+=cpuRetryChecks;
    physicalStats.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-physicalStarted).count();
    int counts[9]{};for(int code:rejection)++counts[code];
    char evidence[512];std::snprintf(evidence,sizeof(evidence),
        "local-validation-v1 highlight=%d gpuChecks=%d cpuChecks=%d proposalMs=%.3f verifyPhaseMs=%.3f lowSignal=%d identity=%d mask=%d ecc=%d transform=%d correlation=%d detail=%d admitted=%d reason=%s",
        int(highlight),physicalStats.patches,physicalStats.cpuPatches,proposalMs,physicalStats.ms,counts[1],counts[2],counts[3],counts[4],counts[5],counts[6],counts[7],counts[8],physicalReason.c_str());
    recordFusionDiagnostic(evidence);
    __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf",
        "local physical v1 highlight=%d gpuChecks=%d cpuChecks=%d proposalMs=%.3f verifyPhaseMs=%.3f reason=%s; nodes lowSignal=%d identity=%d mask=%d ecc=%d transform=%d correlation=%d detail=%d admitted=%d",
        int(highlight),physicalStats.patches,physicalStats.cpuPatches,proposalMs,physicalStats.ms,physicalReason.c_str(),
        counts[1],counts[2],counts[3],counts[4],counts[5],counts[6],counts[7],counts[8]);
    if(gpuPhysicalFallbacks)__android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf","GPU physical-check CPU completions=%d",gpuPhysicalFallbacks);
    int detailNodes = 0,denoiseOnlyNodes=0;
    for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        const auto p = model.field.at<cv::Vec3f>(y, x);
        if (model.detail.at<uchar>(y,x)) ++detailNodes;
        else if(p[2]>0)++denoiseOnlyNodes;
        if (p[2] > 0) model.maxOffset = std::max(model.maxOffset, std::hypot(p[0], p[1]));
    }
    model.detailFraction = float(detailNodes) / (nx * ny);
    model.denoiseOnlyFraction=float(denoiseOnlyNodes)/(nx*ny);
    if(singlePatch) { model.supportedFraction = model.field.at<cv::Vec3f>(0,0)[2]>0 ? 1.f : 0.f; return model; }
    const auto fitted=std::chrono::steady_clock::now();
    if(detailNodes==0 && denoiseOnlyNodes==0) {
        model.gate=cv::Mat::zeros(reference.size(),CV_8U);
        __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf",
            "local support backend=CPU zero-map size=%dx%d prepareMs=%.3f fitVerifyMs=%.3f reason=no qualified local nodes",
            reference.cols,reference.rows,
            std::chrono::duration<double,std::milli>(preparedAt-started).count(),
            std::chrono::duration<double,std::milli>(fitted-preparedAt).count());
        return model;
    }
    cv::Mat refSmooth, srcSmooth;
    if(cached)refSmooth=prepared->smooth;else cv::GaussianBlur(reference, refSmooth, cv::Size(0, 0), .6);
    cv::GaussianBlur(warped, srcSmooth, cv::Size(0, 0), .6);
    std::string backendReason=highlight ? "HDR patch support remains CPU" : "";
    const bool gateReady=!singlePatch && gpuLocalSupport(refSmooth,srcSmooth,model,model.gate,backendReason,geometryValidity);
    model.gpuSupportMap=gateReady && backendReason=="validated packed-byte support kernel";
    if(!gateReady) {
        model.gate.create(reference.size(),CV_8U);
        #pragma omp parallel for schedule(static)
        for (int y=0;y<reference.rows;++y) {
            auto* row=model.gate.ptr<uchar>(y);
            for(int x=0;x<reference.cols;++x)
                row[x]=supportPixel(refSmooth,srcSmooth,model,x,y,highlight,referenceValidity);
        }
    }
    const auto finished=std::chrono::steady_clock::now();
    auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
    __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf",
        "local support backend=%s size=%dx%d prepareMs=%.3f fitVerifyMs=%.3f gateMs=%.3f reason=%s",
        gateReady && backendReason=="validated packed-byte support kernel" ? "Vulkan" : "CPU",reference.cols,reference.rows,ms(started,preparedAt),
        ms(preparedAt,fitted),ms(fitted,finished),backendReason.c_str());
    model.supportedFraction = float(cv::countNonZero(model.gate)) / model.gate.total();
    if(!requestedPixels.empty()) {
        const int supportedDemand=cv::countNonZero(model.gate & requestedPixels);
        const bool exhaustive=supportedDemand>0 && supportedDemand<1024;
        __android_log_print(ANDROID_LOG_INFO,"HachiCam-Perf",
            "HDR demand nodes=%d/%d supportedDemand=%d exhaustiveFallback=%d",activeCount,nx*ny,supportedDemand,int(exhaustive));
        if(exhaustive)return registerLocal(reference,warped,highlight,singlePatch,referenceValidity,prepared);
        if(!supportedDemand){model.gate.setTo(0);model.supportedFraction=0;}
    }
    return model;
}
}
