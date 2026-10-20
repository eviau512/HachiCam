#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace fusion {
struct GpuCostProfile {
    enum State { Unknown, Gpu, CpuCost, NumericalFailure } state=Unknown;
    int attempts=0,skip=0;
    double cold=0,warm=0,cpu=0,estimatedGpu=0,estimatedCpu=0;
    std::string detail;
    static double reusedCost(double cold,double warm) {return warm+std::max(0.,cold-warm)*.25;}
    bool eligible() {
        if(state==NumericalFailure)return false;
        if(state!=CpuCost)return true;
        if(attempts>=3)return false;
        if(skip>0){--skip;return false;}
        return true;
    }
    bool needsProbe() const {return state!=Gpu;}
    void record(bool exact,double coldMs,double warmMs,double cpuMs,
                double gpuEstimate,double cpuEstimate,const std::string& note={}) {
        ++attempts;cold=coldMs;warm=warmMs;cpu=cpuMs;estimatedGpu=gpuEstimate;estimatedCpu=cpuEstimate;detail=note;
        const bool finite=std::isfinite(cold)&&std::isfinite(warm)&&std::isfinite(cpu)&&
            std::isfinite(estimatedGpu)&&std::isfinite(estimatedCpu)&&cold>=0&&warm>=0&&cpu>0&&estimatedGpu>=0&&estimatedCpu>0;
        state=!exact||!finite ? NumericalFailure : estimatedGpu<=estimatedCpu*.90 ? Gpu : CpuCost;
        skip=state==CpuCost ? 8 : 0;
    }
    std::string describe(const char* stage,int key) const {
        char text[384];std::snprintf(text,sizeof(text),
            "%s key=%d state=%s attempts=%d cooldown=%d coldMs=%.3f warmMs=%.3f cpuMs=%.3f estimatedGpuMs=%.3f estimatedCpuMs=%.3f %s",
            stage,key,state==Gpu?"GPU":state==CpuCost?"CPU-cost":state==NumericalFailure?"numeric-block":"unmeasured",
            attempts,skip,cold,warm,cpu,estimatedGpu,estimatedCpu,detail.c_str());return text;
    }
};
}
