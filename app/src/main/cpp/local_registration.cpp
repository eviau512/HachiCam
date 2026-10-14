#include "local_registration.h"
#include "fusion_math.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

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

LocalRegistration registerLocal(const cv::Mat& reference, const cv::Mat& warped, bool highlight, bool singlePatch, const cv::Mat& referenceValidity) {
    CV_Assert(reference.type() == CV_8UC1 && reference.size() == warped.size() && warped.type() == CV_8UC1);
    CV_Assert(referenceValidity.empty() || (referenceValidity.type()==CV_8U && referenceValidity.size()==reference.size()));
    LocalRegistration model;
    model.patchSupport=highlight;model.domain=reference.size();
    const cv::Mat& geometryValidity=referenceValidity;
    const int nx = singlePatch ? 1 : (reference.cols - 1 + model.spacing - 1) / model.spacing + 1;
    const int ny = singlePatch ? 1 : (reference.rows - 1 + model.spacing - 1) / model.spacing + 1;
    model.field = cv::Mat::zeros(ny, nx, CV_32FC3);
    model.detail = cv::Mat::zeros(ny,nx,CV_8U);
    const cv::Mat refHP = highPass(reference), srcHP = highPass(warped);
    cv::Mat refAlign,srcAlign;
    cv::GaussianBlur(reference,refAlign,cv::Size(0,0),1.2);
    cv::GaussianBlur(warped,srcAlign,cv::Size(0,0),1.2);
    const double noiseGain=[] {
        cv::Mat impulse=cv::Mat::zeros(25,25,CV_32F);impulse.at<float>(12,12)=1;
        const auto band=highPass(impulse);return cv::sum(band.mul(band))[0];
    }();
    model.noiseSigma=float(estimateNoiseSigma(reference));
    #pragma omp parallel for schedule(dynamic)
    for (int gy = 0; gy < ny; ++gy) for (int gx = 0; gx < nx; ++gx) {
        const int cx = singlePatch ? reference.cols/2 : std::min(gx * model.spacing, reference.cols - 1);
        const int cy = singlePatch ? reference.rows/2 : std::min(gy * model.spacing, reference.rows - 1);
        const cv::Rect roi(std::max(0, cx - 160), std::max(0, cy - 160),
            std::min(reference.cols, cx + 161) - std::max(0, cx - 160),
            std::min(reference.rows, cy + 161) - std::max(0, cy - 160));
        if (roi.width < 32 || roi.height < 32) continue;
        const cv::Mat ref=refAlign(roi),src=srcAlign(roi);
        const double refNoise=estimateNoiseSigma(reference(roi)),srcNoise=estimateNoiseSigma(warped(roi));
        const cv::Rect interior(roi.x+10,roi.y+10,roi.width-20,roi.height-20);
        cv::Scalar mean,sd,srcSd;
        cv::meanStdDev(refHP(interior),mean,sd);cv::meanStdDev(srcHP(interior),mean,srcSd);
        const double refSignal=std::max(0.,sd[0]*sd[0]-noiseGain*refNoise*refNoise);
        const double srcSignal=std::max(0.,srcSd[0]*srcSd[0]-noiseGain*srcNoise*srcNoise);
        if (refSignal < 2.25 && srcSignal < 2.25) {
            cv::Mat difference;cv::absdiff(refAlign(interior),srcAlign(interior),difference);
            // No translation fit to random grain, and no SR credit. The globally
            // aligned low-frequency observations must still agree locally.
            if (!highlight && cv::mean(difference)[0] < std::max(3.,.28*std::hypot(refNoise,srcNoise)))
                model.field.at<cv::Vec3f>(gy,gx)={0,0,.85f};
            continue;
        }
        if(highlight) {
            cv::Mat mask=cv::Mat::zeros(roi.size(),CV_8U);
            mask(cv::Rect(10,10,roi.width-20,roi.height-20)).setTo(255);
            mask &= (reference(roi)>8)&(reference(roi)<247);
            if(!geometryValidity.empty())mask &= geometryValidity(roi);
            double ratio=0;
            const double score=correlation(refHP(roi),srcHP(roi),mask,
                noiseGain*refNoise*refNoise,noiseGain*srcNoise*srcNoise,ratio);
            if(score>=.95 && ratio>=.68) {
                model.field.at<cv::Vec3f>(gy,gx)={0,0,float(std::min(1.,score))};
                model.detail.at<uchar>(gy,gx)=255;continue;
            }
        }
        try {
            cv::Mat transform = cv::Mat::eye(2, 3, CV_32F);
            cv::Mat mask = (warped(roi) > 8) & (warped(roi) < 247);
            if(!geometryValidity.empty())mask &= geometryValidity(roi);
            if(cv::countNonZero(mask)<256)continue;
            const double cc = cv::findTransformECC(ref, src, transform, cv::MOTION_TRANSLATION,
                cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, .0001), mask, 3);
            const float dx = transform.at<float>(0, 2), dy = transform.at<float>(1, 2);
            if (!std::isfinite(cc) || cc < .65 || std::hypot(dx, dy) > 8) continue;
            cv::Mat correctedHP;
            cv::warpAffine(srcHP(roi), correctedHP, transform, roi.size(), cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REFLECT_101);
            cv::Mat valid(roi.size(), CV_8U, cv::Scalar(255));
            valid.rowRange(0, 10).setTo(0); valid.rowRange(valid.rows-10, valid.rows).setTo(0);
            valid.colRange(0, 10).setTo(0); valid.colRange(valid.cols-10, valid.cols).setTo(0);
            valid &= (reference(roi) > 8) & (reference(roi) < 247);
            if(!geometryValidity.empty())valid &= geometryValidity(roi);
            double detailRatio = 0;
            const double score = correlation(refHP(roi), correctedHP, valid,
                noiseGain*refNoise*refNoise,noiseGain*srcNoise*srcNoise,detailRatio);
            const bool strict = score >= (highlight ? .80 : .90);
            const bool denoiseOnly = !highlight && std::max(refNoise,srcNoise)>1 && cc>=.95 && score>=.80;
            if ((!strict && !denoiseOnly) || detailRatio < .68) continue;
            model.field.at<cv::Vec3f>(gy, gx) = {dx, dy, float(std::clamp(score, 0.0, 1.0))};
            // Subpixel detail requires substantial structure above the grain.
            if(strict && refSignal>=std::max(2.25,4*noiseGain*refNoise*refNoise) &&
               srcSignal>=std::max(2.25,4*noiseGain*srcNoise*srcNoise))model.detail.at<uchar>(gy,gx)=255;
        } catch (const cv::Exception&) { /* Insufficient local evidence. */ }
    }
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
    model.gate = cv::Mat::zeros(reference.size(), CV_8U);
    cv::Mat refSmooth, srcSmooth;
    cv::GaussianBlur(reference, refSmooth, cv::Size(0, 0), .6);
    cv::GaussianBlur(warped, srcSmooth, cv::Size(0, 0), .6);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < reference.rows; ++y) {
        auto* row = model.gate.ptr<uchar>(y);
        for (int x = 0; x < reference.cols; ++x) {
            cv::Point2d d; float confidence;
            if (!model.offset(x, y, d, confidence)) continue;
            const float observed = graySample(srcSmooth, x + d.x, y + d.y);
            // Same-exposure photometric check protects occlusions and fine strokes.
            // Short input has already been projected to reference brightness;
            // residual gating still rejects occlusions and response mismatches.
            const bool clippedTarget=highlight && !referenceValidity.empty() && !referenceValidity.at<uchar>(y,x);
            if (observed >= 0 && (clippedTarget || std::abs(observed - refSmooth.at<uchar>(y, x)) <= (highlight ? 18 : 14))) row[x] = 255;
        }
    }
    model.supportedFraction = float(cv::countNonZero(model.gate)) / model.gate.total();
    return model;
}
}
