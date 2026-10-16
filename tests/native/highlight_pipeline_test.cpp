#include "burst_fusion.h"
#include "fusion_math.h"
#include "local_registration.h"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
cv::Mat chart() {
    cv::Mat image(1024,1280,CV_8UC3,cv::Scalar(85,100,110));
    for(int y=50;y<1000;y+=55)cv::putText(image,"abcd 1234 IMAGE",{30,y},cv::FONT_HERSHEY_SIMPLEX,.6,{40,45,50},1);
    // A small bright screen has texture visible at both exposures around its
    // clipped strokes. Linear radiance 1.15..1.8 clips in the base only.
    const cv::Rect screen(400,280,460,360);
    image(screen).setTo(cv::Scalar(150,150,150));
    for(int y=300;y<620;y+=35)cv::putText(image,"HDR 126",{420,y},cv::FONT_HERSHEY_SIMPLEX,.65,{225,225,225},2);
    cv::rectangle(image,{500,350,80,70},cv::Scalar(100,100,200),cv::FILLED);
    return image;
}
cv::Mat baseFrom(const cv::Mat& shortImage) {
    cv::Mat output=shortImage.clone();
    for(int y=0;y<output.rows;++y)for(int x=0;x<output.cols;++x)for(int c=0;c<3;++c)
        output.at<cv::Vec3b>(y,x)[c]=cv::saturate_cast<uchar>(fusion::encoded(std::min(1.f,fusion::linear(shortImage.at<cv::Vec3b>(y,x)[c])*4)));
    return output;
}
}
void runHighlightTests() {
    {
        cv::Mat shorter(1024,1280,CV_8UC3,cv::Scalar::all(60));
        for(int y=35;y<1000;y+=35)for(int x=20;x<1200;x+=200)
            cv::putText(shorter,"HDR 126",{x,y},cv::FONT_HERSHEY_SIMPLEX,.55,cv::Scalar::all(90),1);
        const cv::Point center(650,480);
        cv::circle(shorter,center,22,cv::Scalar::all(180),cv::FILLED);
        cv::circle(shorter,center,8,cv::Scalar::all(40),cv::FILLED);
        cv::Mat reference=baseFrom(shorter);
        cv::circle(reference,center,26,cv::Scalar::all(255),cv::FILLED); // clipped ISP bloom hides the hole
        BurstFusionEngine engine;
        for(bool stream:{false,true}) {
            BurstFusionEngine::Report report;
            const std::vector<cv::Mat> frames{reference,shorter};
            const auto result=stream ? engine.fuseBurstStream(2,[&](size_t i){return frames[i].clone();},{0,1},{4,1},&report) :
                engine.fuseBurstFrames(frames,false,true,false,{0,1},{4,1},&report);
            const auto hole=result.at<cv::Vec3b>(center.y-report.crop.y,center.x-report.crop.x);
            const auto ring=result.at<cv::Vec3b>(center.y-report.crop.y,center.x+16-report.crop.x);
            std::cout<<"HDR ring stream="<<stream<<" hole="<<int(hole[0])<<" rim="<<int(ring[0])<<" recovered="<<report.recoveredFraction<<'\n';
            check(hole[0]<160 && ring[0]>hole[0]+40,"Supported short exposure must restore the dark lamp-ring hole hidden by clipping");
            check(report.scale==1,"Lamp recovery must not grant SR phase credit");
        }
        const auto support=fusion::highlightNeighborhood(shorter,4);
        check(support.at<uchar>(center)>0,"Enclosed observed hole must receive bounded support");
        cv::Mat isolated(200,200,CV_8UC3,cv::Scalar::all(0));
        isolated(cv::Rect(100,50,60,100)).setTo(cv::Scalar::all(180));
        const auto bounded=fusion::highlightNeighborhood(isolated,4);
        check(bounded.at<uchar>(100,95)==0,"A dark region outside a bright object must not receive one-sided replacement support");
    }
    const cv::Mat shortImage=chart(),base=baseFrom(shortImage);
    BurstFusionEngine engine;
    for(bool stream:{false,true}) {
        BurstFusionEngine::Report report;
        const std::vector<cv::Mat> frames{base,base,shortImage};
        const cv::Mat out=stream ? engine.fuseBurstStream(3,[&](size_t i){return frames[i].clone();},{0,0,1},{4,4,1},&report) :
            engine.fuseBurstFrames(frames,false,true,false,{0,0,1},{4,4,1},&report);
        std::cout<<"HDR pipeline stream="<<stream<<" admitted="<<report.highlights<<" recovered="<<report.recoveredFraction<<" clipped="<<report.clippedFraction<<'\n';
        check(report.anchors==2,"HDR must retain base multi-frame noise reduction");
        const auto perf=report.performance;
        check(std::isfinite(perf.totalMs) && perf.totalMs>0 && perf.globalRegistrationMs>=0 && perf.localRegistrationMs>=0 &&
            perf.globalRegistrationMs+perf.localRegistrationMs<=perf.totalMs+.001,"Per-call native intervals must be finite and nonoverlapping");
        check(perf.gpuSupportMaps>=0 && perf.gpuSupportMaps<=6 && perf.gpuFitProposals>=0 &&
            perf.gpuNoiseGrids>=0 && perf.gpuNoiseGrids<=7 && perf.gpuMergeTiles>=0 && perf.gpuMergeTiles<=4096 &&
            perf.cpuMergePixels>=0 && perf.cpuMergePixels<=16000000 && perf.gpuMergeMs>=0 &&
            perf.gpuMergeMs<=perf.totalMs-perf.globalRegistrationMs-perf.localRegistrationMs+.001 &&
            perf.gpuSelectorPatches>=0 && perf.gpuSelectorPatches<=24576 && perf.cpuSelectorPatches>=0 &&
            perf.cpuSelectorPatches<=perf.gpuSelectorPatches && perf.selectorMs>=0 && perf.selectorMs<=perf.localRegistrationMs+.001 &&
            report.backendDecisions.size()<=16384,"Per-result backend counters must be bounded");
        check(report.recoveredFraction>.002,"Production pipeline must actually recover clipped screen text");
        check(report.scale==1,"Short exposure must not grant SR phase credit");
        long long measuredPost=0;
        for(int y=0;y<out.rows;++y)for(int x=0;x<out.cols;++x){const auto p=out.at<cv::Vec3b>(y,x);if(std::max({p[0],p[1],p[2]})>=250)++measuredPost;}
        check(std::abs(report.postClippedFraction-double(measuredPost)/out.total())<1e-12,"Post HDR metric must match actual native byte clipping");
        const auto colored=out.at<cv::Vec3b>(385-report.crop.y,540-report.crop.x);
        check(colored[2]<250 && colored[2]>colored[0]+20,"Single-channel clipping must recover observed color");
        cv::Mat sg,og;cv::cvtColor(shortImage,sg,cv::COLOR_BGR2GRAY);cv::cvtColor(out,og,cv::COLOR_BGR2GRAY);
        cv::Mat region=cv::Mat::zeros(base.size(),CV_8U);region(cv::Rect(410,290,440,340)).setTo(255);
        const cv::Mat ink=(sg>210)&region,background=(sg==150)&region;
        check(cv::mean(og,ink(report.crop))[0]>cv::mean(og,background(report.crop))[0]+4,"Recovered text must regain contrast lost to base clipping");
        const auto p=out.at<cv::Vec3b>(80-report.crop.y,120-report.crop.x);
        check(cv::norm(p,base.at<cv::Vec3b>(80,120))<=1,"Recovery must preserve ordinary midtones");
        if(stream)engine.fuseBurstStream(2,[&](size_t i){return frames[i==0 ? 0 : 2].clone();},{0,1},{4,4},&report);
        else engine.fuseBurstFrames({base,shortImage},false,true,false,{0,1},{4,4},&report);
        check(report.highlights==0&&report.recoveredFraction==0,"Ignored manual exposure cannot produce HDR");
        cv::Mat occluded=shortImage.clone();occluded(cv::Rect(400,280,460,360)).setTo(cv::Scalar::all(20));
        const std::vector<cv::Mat> hidden{base,occluded};
        if(stream)engine.fuseBurstStream(2,[&](size_t i){return hidden[i].clone();},{0,1},{4,1},&report);
        else engine.fuseBurstFrames(hidden,false,true,false,{0,1},{4,1},&report);
        check(report.recoveredFraction==0,"Occluded bright screen must not borrow unrelated short pixels");
        cv::Mat moved;const cv::Mat shift=(cv::Mat_<double>(2,3)<<1,0,2.3,0,1,-1.7);
        cv::warpAffine(shortImage,moved,shift,shortImage.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
        const std::vector<cv::Mat> motion{base,moved};
        if(stream)engine.fuseBurstStream(2,[&](size_t i){return motion[i].clone();},{0,1},{4,1},&report);
        else engine.fuseBurstFrames(motion,false,true,false,{0,1},{4,1},&report);
        std::cout<<"HDR shifted stream="<<stream<<" recovered="<<report.recoveredFraction<<'\n';
        check(report.recoveredFraction>.002,"Handheld translation must retain supported highlight recovery");
        const cv::Mat clipped(base.size(),base.type(),cv::Scalar::all(255));
        if(stream)engine.fuseBurstStream(2,[&](size_t){return clipped.clone();},{0,1},{4,1},&report);
        else engine.fuseBurstFrames({clipped,clipped},false,true,false,{0,1},{4,1},&report);
        check(report.recoveredFraction==0,"All-clipped evidence cannot invent detail");
        check(report.postClippedFraction==1,"All-clipped no-recovery output must retain post clipping");
    }
    {
        cv::Mat clean(1536,2048,CV_8UC3,cv::Scalar::all(100));
        for(int y=40;y<1500;y+=37)for(int x=25;x<1800;x+=430)
            cv::putText(clean,"Hachi 18:45 1.08 ABC",{x,y},cv::FONT_HERSHEY_SIMPLEX,.7,cv::Scalar::all(190),1,cv::LINE_AA);
        const cv::Rect bright(950,700,160,120),core(864,608,320,320);
        clean(bright).setTo(cv::Scalar::all(150));
        for(int y=730;y<810;y+=35)cv::putText(clean,"126",{966,y},cv::FONT_HERSHEY_SIMPLEX,.65,cv::Scalar::all(225),2);
        cv::Mat candidate;cv::GaussianBlur(clean,candidate,cv::Size(0,0),3.5);
        clean(core).copyTo(candidate(core));
        const cv::Mat ref=baseFrom(clean);
        cv::Mat rg,sg;cv::cvtColor(ref,rg,cv::COLOR_BGR2GRAY);cv::cvtColor(fusion::normalizeExposure(candidate,4),sg,cv::COLOR_BGR2GRAY);
        const auto known=fusion::registerLocal(rg,sg,true);
        std::cout<<"HDR sparse localSupport="<<known.supportedFraction<<'\n';
        check(known.supportedFraction>.001 && known.supportedFraction<.05,"Fixture must expose useful highlight support below the old 5 percent cutoff");
        BurstFusionEngine::Report report;
        const auto out=engine.fuseBurstFrames({ref,candidate},false,true,false,{0,1},{4,1},&report);
        std::cout<<"HDR sparse bright area="<<double(bright.area())/ref.total()<<" recovered="<<report.recoveredFraction<<" admitted="<<report.highlights<<'\n';
        check(report.recoveredFraction>.001,"Small supported highlight must survive whole-frame admission");
        check(report.recoveredFraction<.05,"Small highlight fixture must recover less than 5 percent of the image");
        const cv::Mat raw=ref(report.crop);
        cv::Mat changed;cv::absdiff(out,raw,changed);cv::cvtColor(changed,changed,cv::COLOR_BGR2GRAY);
        check(cv::countNonZero(changed(cv::Rect(50-report.crop.x,50-report.crop.y,300,300)))==0,"Unsupported blurred area must retain reference pixels");
        const std::vector<cv::Mat> frames{ref,candidate};
        const auto streamed=engine.fuseBurstStream(2,[&](size_t i){return frames[i].clone();},{0,1},{4,1},&report);
        std::cout<<"HDR sparse stream recovered="<<report.recoveredFraction<<'\n';
        check(report.recoveredFraction>.001 && report.recoveredFraction<.05,"Streaming pipeline must retain sparse highlight support");
        cv::absdiff(streamed,ref(report.crop),changed);cv::cvtColor(changed,changed,cv::COLOR_BGR2GRAY);
        check(cv::countNonZero(changed(cv::Rect(50-report.crop.x,50-report.crop.y,300,300)))==0,"Streaming recovery must preserve unsupported areas");
    }
}
