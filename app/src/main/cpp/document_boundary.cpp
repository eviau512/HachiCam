#include "edge_detector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace {
float cross(cv::Point2f a, cv::Point2f b) { return a.x*b.y-a.y*b.x; }
struct Candidate {
    std::vector<cv::Point2f> quad;
    double score = -1;
    float support = 0;
    bool partial = false;
    int method = 0;
};

// Verify observed sides, not just a rectangular hull. Border pixels cannot
// provide the unavailable outside-of-page sample.
Candidate evaluate(const cv::Mat& gray, const cv::Mat& distance,
        const std::vector<cv::Point2f>& points, int method) {
    Candidate result;
    if (points.size()!=4 || !cv::isContourConvex(points)) return result;
    const auto quad = EdgeDetector::sortCorners(points);
    const float w = gray.cols-1.f, h = gray.rows-1.f;
    const double area = std::abs(cv::contourArea(quad));
    const double fraction = area/(gray.cols*gray.rows);
    if (fraction < (method==2 ? .08 : .005) || fraction > .95) return result;
    int outside = 0, missingCorners = 0;
    cv::Point2f center;
    for (auto p:quad) {
        if (!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<-.25f*w||p.x>1.25f*w||p.y<-.25f*h||p.y>1.25f*h) return result;
        if (p.x<1||p.x>w-1||p.y<1||p.y>h-1) { ++outside; ++missingCorners; }
        else if (distance.at<float>(cvRound(p.y),cvRound(p.x))>6.f) ++missingCorners;
        center += p*.25f;
    }
    // At least three actual corners must be inside the preview.
    if (outside>1 || missingCorners>1 || (method==1 && outside>0)) return result;
    const auto rect = cv::minAreaRect(quad);
    if (std::min(rect.size.width,rect.size.height)<=0 ||
            std::max(rect.size.width,rect.size.height)/std::min(rect.size.width,rect.size.height)>12) return result;
    std::array<float,4> supports{}, contrasts{};
    for (int side=0;side<4;++side) {
        const auto a=quad[side], b=quad[(side+1)%4], d=b-a, next=quad[(side+2)%4]-b;
        const float length=cv::norm(d), nextLength=cv::norm(next);
        if (length<8 || nextLength<8 || std::abs(d.dot(next)/(length*nextLength))>.85f) return result;
        cv::Point2f normal(-d.y/length,d.x/length);
        if (normal.dot(center-(a+b)*.5f)<0) normal=-normal;
        int samples=0, supported=0, contrastCount=0;
        double contrast=0;
        for (int n=0;n<65;++n) {
            const auto p=a+d*(.03f+.94f*n/64.f);
            if (p.x<3||p.x>w-3||p.y<3||p.y>h-3) continue;
            ++samples;
            if (distance.at<float>(cvRound(p.y),cvRound(p.x))<3.5f) ++supported;
            for (float offset:{4.f,7.f}) {
                const auto pin=p+normal*offset, pout=p-normal*offset;
                if (pin.x<0||pin.x>w||pin.y<0||pin.y>h||pout.x<0||pout.x>w||pout.y<0||pout.y>h) continue;
                contrast += int(gray.at<uchar>(cvRound(pin.y),cvRound(pin.x)))-int(gray.at<uchar>(cvRound(pout.y),cvRound(pout.x)));
                ++contrastCount;
            }
        }
        if (samples<10 || contrastCount<10) return result;
        supports[side]=float(supported)/samples;
        contrasts[side]=float(contrast/contrastCount);
    }
    const float weakest=*std::min_element(supports.begin(),supports.end());
    const float mean=std::accumulate(supports.begin(),supports.end(),0.f)/4;
    if (weakest<.40f || mean<.65f) return result;
    const int positive=std::count_if(contrasts.begin(),contrasts.end(),[](float c){return c>4;});
    const int negative=std::count_if(contrasts.begin(),contrasts.end(),[](float c){return c< -4;});
    if (!(positive>=3 && *std::min_element(contrasts.begin(),contrasts.end())>=-3) &&
        !(negative>=3 && *std::max_element(contrasts.begin(),contrasts.end())<=3)) return result;
    const float centerPreference=std::clamp(1.f-.35f*float(cv::norm(center-cv::Point2f(w*.5f,h*.5f)))/(std::hypot(w,h)*.5f),.35f,1.f);
    result.quad=quad;
    result.score=(std::sqrt(fraction)+.18)*mean*(.65+.35*weakest)*centerPreference;
    result.support=weakest;
    result.partial=missingCorners!=0;
    result.method=method;
    return result;
}

struct Line { cv::Point2f a,b,unit; float length; };
bool intersection(const Line& a,const Line& b,cv::Point2f& p) {
    const float determinant=cross(a.unit,b.unit);
    if (std::abs(determinant)<.15f) return false;
    p=a.a+a.unit*(cross(b.a-a.a,b.unit)/determinant);
    return std::isfinite(p.x)&&std::isfinite(p.y);
}
}

DetectionResult EdgeDetector::detectDocument(const cv::Mat& input, bool /*curvedMode*/, float touchX, float touchY) {
    DetectionResult result{};
    if (input.empty()) return result;
    cv::Mat gray;
    if (input.channels()==3) cv::cvtColor(input,gray,cv::COLOR_BGR2GRAY);
    else if (input.channels()==4) cv::cvtColor(input,gray,cv::COLOR_BGRA2GRAY);
    else gray=input;
    cv::Mat small;
    const double scale=std::min(1.,640./std::max(gray.cols,gray.rows));
    if (scale<1) cv::resize(gray,small,{},scale,scale,cv::INTER_AREA); else small=gray;
    if (small.cols<16 || small.rows<16) return result;
    cv::Mat blurred, evidenceEdges, distance;
    cv::GaussianBlur(small,blurred,{3,3},.7);
    cv::Canny(blurred,evidenceEdges,20,60);
    cv::distanceTransform(255-evidenceEdges,distance,cv::DIST_L2,3);
    cv::Mat contrast, contourBlur, edges;
    cv::createCLAHE(2.,{8,8})->apply(small,contrast);
    cv::GaussianBlur(contrast,contourBlur,{5,5},1.);
    cv::Canny(contourBlur,edges,25,70);
    cv::morphologyEx(edges,edges,cv::MORPH_CLOSE,cv::getStructuringElement(cv::MORPH_RECT,{3,3}));
    cv::Mat gx,gy,gradient;
    cv::Sobel(small,gx,CV_16S,1,0,3); cv::Sobel(small,gy,CV_16S,0,1,3);
    cv::convertScaleAbs(gx,gx);cv::convertScaleAbs(gy,gy);cv::addWeighted(gx,.5,gy,.5,0,gradient);
    Candidate best;
    auto consider=[&](const std::vector<cv::Point2f>& quad,int method) {
        auto candidate=evaluate(small,distance,quad,method);
        if (candidate.score<=0) return;
        ++result.candidates;
        cv::Point2f center;
        for(auto p:candidate.quad)center+=p*.25f;
        std::vector<cv::Point> inset;
        for(auto p:candidate.quad){auto q=center+(p-center)*.76f;inset.emplace_back(cvRound(q.x),cvRound(q.y));}
        cv::Mat mask(small.size(),CV_8U,cv::Scalar(0));cv::fillConvexPoly(mask,inset,cv::Scalar(255));
        candidate.score*=1.+.12*std::clamp((cv::mean(gradient,mask)[0]-3.5)/16.,0.,1.);
        if(touchX>=0&&touchY>=0&&cv::pointPolygonTest(candidate.quad,{touchX*(small.cols-1),touchY*(small.rows-1)},false)>=0)candidate.score*=3;
        if(candidate.score>best.score)best=std::move(candidate);
    };
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges,contours,cv::RETR_LIST,cv::CHAIN_APPROX_SIMPLE);
    std::sort(contours.begin(),contours.end(),[](const auto& a,const auto& b){return cv::contourArea(a)>cv::contourArea(b);});
    int inspected=0;
    for (const auto& contour:contours) {
        if(cv::contourArea(contour)<.005*small.total() || ++inspected>80)break;
        for(bool useHull:{false,true}) {
            std::vector<cv::Point> polygon=contour,approx;
            if(useHull)cv::convexHull(contour,polygon);
            const double perimeter=cv::arcLength(polygon,true);
            for(double epsilon:{.02,.025,.03,.04,.05}) {
                cv::approxPolyDP(polygon,approx,epsilon*perimeter,true);
                if(approx.size()!=4)continue;
                std::vector<cv::Point2f> quad(approx.begin(),approx.end());consider(quad,1);break;
            }
        }
    }
    // Independent straight-side assembly handles disconnected page edges.
    std::vector<cv::Vec4f> segments;
    cv::createLineSegmentDetector(cv::LSD_REFINE_STD)->detect(blurred,segments);
    std::sort(segments.begin(),segments.end(),[](auto a,auto b){return std::hypot(a[2]-a[0],a[3]-a[1])>std::hypot(b[2]-b[0],b[3]-b[1]);});
    std::vector<Line> lines;
    for(auto s:segments) {
        Line l{{s[0],s[1]},{s[2],s[3]},{},float(std::hypot(s[2]-s[0],s[3]-s[1]))};
        if(l.length<45)break;
        l.unit=(l.b-l.a)/l.length;
        bool duplicate=false;
        for(const auto& b:lines)if(std::abs(cross(l.unit,b.unit))<.10f && std::abs(cross(l.unit,(l.a+l.b-b.a-b.b)*.5f))<5){duplicate=true;break;}
        if(!duplicate)lines.push_back(l);
        if(lines.size()>=20)break;
    }
    std::vector<std::pair<int,int>> opposite;
    for(int i=0;i<int(lines.size());++i)for(int j=i+1;j<int(lines.size());++j)
        if(std::abs(cross(lines[i].unit,lines[j].unit))<.57f && std::abs(cross(lines[i].unit,(lines[j].a+lines[j].b-lines[i].a-lines[i].b)*.5f))>45)
            opposite.emplace_back(i,j);
    for(size_t a=0;a<opposite.size();++a)for(size_t b=a+1;b<opposite.size();++b) {
        const auto [i,j]=opposite[a];const auto [m,n]=opposite[b];
        if(i==m||i==n||j==m||j==n)continue;
        std::vector<cv::Point2f> q(4);
        if(intersection(lines[i],lines[m],q[0])&&intersection(lines[i],lines[n],q[1])&&
           intersection(lines[j],lines[n],q[2])&&intersection(lines[j],lines[m],q[3]))consider(q,2);
    }
    if(best.score<=0)return result;
    result.found=true;result.partial=best.partial;result.method=best.method;result.sideSupport=best.support;
    const float sx=(gray.cols-1.f)/(small.cols-1),sy=(gray.rows-1.f)/(small.rows-1);
    for(auto p:best.quad)result.corners.emplace_back(p.x*sx,p.y*sy);
    return result;
}
