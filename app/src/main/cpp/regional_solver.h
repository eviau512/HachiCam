#pragma once
#include <opencv2/core.hpp>
#include <algorithm>
#include <vector>

namespace fusion {
struct alignas(16) RegionalObservation {
    int index[16];
    float weight[16];
    cv::Vec3f color;
};
static_assert(sizeof(RegionalObservation)==144,"Stable GPU observation layout");
struct RegionalSolve {
    cv::Mat image;
    double before=0,after=0;
    bool accepted=false;
};
inline double regionalCost(const cv::Mat& estimate,const std::vector<RegionalObservation>& observations,std::vector<cv::Vec3f>* residuals=nullptr) {
    const auto* p=estimate.ptr<cv::Vec3f>();double error=0;
    if(residuals)residuals->resize(observations.size());
    for(size_t i=0;i<observations.size();++i) {
        const auto& o=observations[i];
        cv::Vec3f value(0,0,0);
        for(int k=0;k<16;++k)value+=p[o.index[k]]*o.weight[k];
        for(int c=0;c<3;++c) {
            const double d=std::clamp(double(o.color[c]-value[c]),-14.,14.);
            if(residuals)(*residuals)[i][c]=float(d);
            error+=d*d;
        }
    }
    return error/std::max(size_t(1),observations.size()*3);
}
inline RegionalSolve solveRegionalCpu(const cv::Mat& initial,const std::vector<RegionalObservation>& observations) {
    std::vector<cv::Vec3f> residuals,nextResiduals;
    RegionalSolve r;r.image=initial;r.before=r.after=regionalCost(initial,observations,&residuals);
    if(r.before<=1)return r;
    cv::Mat norm=cv::Mat::zeros(r.image.size(),CV_32F);auto* denom=norm.ptr<float>();
    for(const auto& o:observations)for(int k=0;k<16;++k)denom[o.index[k]]+=o.weight[k];
    for(int iteration=0;iteration<8;++iteration) {
        cv::Mat gradient=cv::Mat::zeros(r.image.size(),CV_32FC3);
        const auto* values=r.image.ptr<cv::Vec3f>();auto* grad=gradient.ptr<cv::Vec3f>();
        for(size_t i=0;i<observations.size();++i) {
            const auto& o=observations[i];
            for(int k=0;k<16;++k)grad[o.index[k]]+=residuals[i]*o.weight[k];
        }
        cv::Mat next=r.image.clone();auto* corrected=next.ptr<cv::Vec3f>();const auto* start=initial.ptr<cv::Vec3f>();
        for(size_t i=0;i<next.total();++i)if(denom[i]>1e-5f)
            for(int c=0;c<3;++c)corrected[i][c]=std::clamp(values[i][c]+.8f*grad[i][c]/denom[i],std::max(0.f,start[i][c]-24),std::min(255.f,start[i][c]+24));
        const double error=regionalCost(next,observations,&nextResiduals);if(error>=r.after)break;
        r.image=std::move(next);r.after=error;residuals.swap(nextResiduals);
    }
    r.accepted=r.after<=r.before*.95;return r;
}
}
