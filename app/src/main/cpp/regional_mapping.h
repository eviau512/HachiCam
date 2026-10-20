#pragma once
#include "regional_reconstruction.h"
#include <limits>
namespace fusion {
struct MappedObservation {cv::Point2d local[4];bool valid=false;};
inline cv::Point2d regionalProject(const cv::Matx33d& h,double x,double y) {
    const auto p=h*cv::Vec3d(x,y,1);
    if(!std::isfinite(p[2])||std::abs(p[2])<1e-9)return {NAN,NAN};
    return {p[0]/p[2],p[1]/p[2]};
}
inline bool regionalReferencePoint(const SamplingPlate& plate,const cv::Matx33d& forward,double x,double y,cv::Point2d& r) {
    const auto initial=regionalProject(forward,x,y);
    if(!std::isfinite(initial.x)||!std::isfinite(initial.y))return false;
    r=initial;if(!plate.local||plate.local->field.empty())return true;
    for(int i=0;i<3;++i){cv::Point2d delta;float confidence;if(!plate.local->offset(r.x,r.y,delta,confidence))return false;r=initial-delta;if(!std::isfinite(r.x)||!std::isfinite(r.y))return false;}
    cv::Point2d p;float confidence;
    if(!detailMapping(plate,r.x,r.y,p,confidence)||cv::norm(p-cv::Point2d(x,y))>.15)return false;
    return plate.local->gate.at<uchar>(std::clamp(int(std::round(r.y)),0,plate.size.height-1),std::clamp(int(std::round(r.x)),0,plate.size.width-1))!=0;
}
inline MappedObservation mapRegionalObservationCpu(const SamplingPlate& plate,const cv::Matx33d& forward,const cv::Rect& patch,int x,int y,bool sourceChecked=false) {
    MappedObservation out;
    if(!sourceChecked){const auto pixel=plate.image->at<cv::Vec3b>(y,x);if(std::max({pixel[0],pixel[1],pixel[2]})>=247)return out;}
    int k=0;
    for(double dy:{-.25,.25})for(double dx:{-.25,.25}) {
        cv::Point2d r;if(!regionalReferencePoint(plate,forward,x+dx,y+dy,r))return out;
        out.local[k]={r.x-patch.x,r.y-patch.y};
        const double px=2*out.local[k].x+.5,py=2*out.local[k].y+.5;
        if(px<0||py<0||px>=patch.width*2-1||py>=patch.height*2-1)return out;
        ++k;
    }
    out.valid=true;return out;
}
inline std::vector<MappedObservation> mapRegionalCpu(const SamplingPlate& plate,const cv::Rect& source,const cv::Rect& patch) {
    std::vector<MappedObservation> out(source.area());const auto forward=plate.inverse.inv();
    for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x)out[y*source.width+x]=mapRegionalObservationCpu(plate,forward,patch,source.x+x,source.y+y);
    return out;
}
}
