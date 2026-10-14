#include "burst_fusion.h"
#include "local_registration.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
cv::Mat noisy(const cv::Mat& clean,double sigma,int seed) {
    cv::Mat values,noise(clean.size(),CV_32FC3),out;
    clean.convertTo(values,CV_32FC3);cv::RNG rng(seed);rng.fill(noise,cv::RNG::NORMAL,0,sigma);
    values+=noise;values.convertTo(out,CV_8UC3);return out;
}
double mse(const cv::Mat& a,const cv::Mat& b,cv::Rect roi) {
    return cv::norm(a(roi),b(roi),cv::NORM_L2SQR)/(3*roi.area());
}
cv::Mat scene() {
    cv::Mat image(768,1024,CV_8UC3,cv::Scalar(170,175,180));
    cv::rectangle(image,{65,160,570,440},cv::Scalar(115,115,115),cv::FILLED);
    cv::rectangle(image,{670,20,320,570},cv::Scalar(40,40,40),cv::FILLED);
    for(int y=220;y<480;y+=40)cv::putText(image,"Hachi 18:45 1.08 ABC",{95,y},cv::FONT_HERSHEY_SIMPLEX,.65,cv::Scalar(205,205,205),1,cv::LINE_AA);
    cv::rectangle(image,{120,520,400,2},cv::Scalar(108,108,108),cv::FILLED);
    cv::putText(image,"COLOR",{710,470},cv::FONT_HERSHEY_SIMPLEX,.8,cv::Scalar(60,90,210),2,cv::LINE_AA);
    return image;
}
void evaluate(const cv::Mat& output,const cv::Mat& clean,const std::vector<cv::Mat>& frames,const BurstFusionEngine::Report& report,const char* mode) {
    require(report.anchors==4,"Independent noisy burst must retain all four useful base frames");
    require(report.scale==1,"Noise must not invent subpixel reconstruction eligibility");
    require(report.multiFrameFraction>.60 && report.meanContributors>2.5,"Actual native-size denoise coverage must be useful");
    const auto truth=clean(report.crop);
    const auto single=frames[report.reference](report.crop);
    const cv::Rect flat(710-report.crop.x,65-report.crop.y,230,200);
    const cv::Rect text(100-report.crop.x,185-report.crop.y,510,310);
    const double flatBefore=mse(single,truth,flat),flatAfter=mse(output,truth,flat);
    const double textBefore=mse(single,truth,text),textAfter=mse(output,truth,text);
    std::cout<<mode<<" flatMSE="<<flatBefore<<" -> "<<flatAfter<<" textMSE="<<textBefore<<" -> "<<textAfter
        <<" multiFraction="<<report.multiFrameFraction<<" meanContributors="<<report.meanContributors<<'\n';
    require(flatAfter<flatBefore*.55,"Independent-noise flat regions must improve over one reference");
    require(textAfter<textBefore*.80,"Text error must improve without sacrificing supported strokes");
    const auto ink=cv::mean(output(cv::Rect(140-report.crop.x,520-report.crop.y,350,2)));
    const auto paper=cv::mean(output(cv::Rect(140-report.crop.x,514-report.crop.y,350,2)));
    require(paper[0]-ink[0]>4,"Faint continuous mark must survive temporal denoising");
}
}

void runNoiseTests() {
    const auto clean=scene();std::vector<cv::Mat> frames;
    for(int i=0;i<4;++i)frames.push_back(noisy(clean,7,1701+i));
    cv::Mat gray;cv::cvtColor(frames[0],gray,cv::COLOR_BGR2GRAY);
    const double sigma=fusion::estimateNoiseSigma(gray(cv::Rect(710,65,230,200)));
    require(sigma>3.5&&sigma<6,"Noise estimate should approximate known independent RGB-to-gray grain");
    require(fusion::referenceQuality(noisy(clean,3,9))>fusion::referenceQuality(noisy(clean,16,10)),"Grain energy must not reward a noisier reference");
    BurstFusionEngine engine;BurstFusionEngine::Report report;
    auto output=engine.fuseBurstFrames(frames,false,false,true,{0,0,0,0},{100,100,100,100},&report);
    evaluate(output,clean,frames,report,"normal-noisy");
    output=engine.fuseBurstStream(frames.size(),[&](size_t i){return frames.at(i).clone();},{0,0,0,0},{100,100,100,100},&report);
    evaluate(output,clean,frames,report,"stream-noisy");
    std::vector<cv::Mat> moved,movedTruth;
    for(const auto delta:std::vector<cv::Point2f>{{0,0},{.4f,.15f},{.1f,.55f},{-.35f,.2f}}) {
        cv::Mat shifted;const cv::Mat h=(cv::Mat_<double>(2,3)<<1,0,delta.x,0,1,delta.y);
        cv::warpAffine(clean,shifted,h,clean.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
        movedTruth.push_back(shifted);moved.push_back(noisy(shifted,7,2100+int(moved.size())));
    }
    output=engine.fuseBurstFrames(moved,false,false,false,{0,0,0,0},{100,100,100,100},&report);
    evaluate(output,movedTruth[report.reference],moved,report,"normal-shifted-noisy");
    output=engine.fuseBurstStream(moved.size(),[&](size_t i){return moved[i].clone();},{0,0,0,0},{100,100,100,100},&report);
    evaluate(output,movedTruth[report.reference],moved,report,"stream-shifted-noisy");
    auto identical=engine.fuseBurstStream(4,[&](size_t){return frames[0].clone();},{0,0,0,0},{100,100,100,100},&report);
    require(report.scale==1&&cv::norm(identical,frames[0](report.crop),cv::NORM_INF)<=1,"Identical grain is not a denoising or SR gain");
    std::vector<cv::Mat> flat;
    for(int i=0;i<4;++i)flat.push_back(noisy(cv::Mat(512,512,CV_8UC3,cv::Scalar(100,100,100)),9,20+i));
    engine.fuseBurstFrames(flat,false,false,true,{0,0,0,0},{100,100,100,100},&report);
    require(report.scale==1,"Independent flat noise cannot establish subpixel phases");
    // Streamed source border and blur behavior share the standard safety policy.
    cv::Mat blurred;cv::GaussianBlur(clean,blurred,cv::Size(0,0),1.8);
    std::vector<cv::Mat> mixed{blurred,clean,clean};
    engine.fuseBurstStream(mixed.size(),[&](size_t i){return mixed[i].clone();},{0,0,0},{100,100,100},&report);
    require(report.reference==1&&report.anchors>=2,"Streaming still ranks sharp evidence above a blurred first arrival");
    {
        cv::Mat refGray,blurGray;
        const auto blurryNoisy=noisy(blurred,7,99);
        cv::cvtColor(frames[0],refGray,cv::COLOR_BGR2GRAY);cv::cvtColor(blurryNoisy,blurGray,cv::COLOR_BGR2GRAY);
        const auto local=fusion::registerLocal(refGray,blurGray);
        cv::Mat ink;cv::inRange(clean,cv::Scalar(190,190,190),cv::Scalar(230,230,230),ink);
        const double supported=double(cv::countNonZero(local.gate & ink))/cv::countNonZero(ink);
        std::cout<<"noisy-blur inkSupport="<<supported<<'\n';
        require(supported<.10,"Noise-aware denoising must still reject blurred white text");
    }
    {
        cv::Mat sparse(1536,2048,CV_8UC3,cv::Scalar(100,100,100));
        for(int y=40;y<1500;y+=30)for(int x=20;x<2000;x+=500)
            cv::putText(sparse,"Hachi 18:45 1.08 ABC",{x,y},cv::FONT_HERSHEY_SIMPLEX,.7,cv::Scalar(220,220,220),1,cv::LINE_AA);
        cv::Mat changed;cv::GaussianBlur(sparse,changed,cv::Size(0,0),1.8);changed+=cv::Scalar(30,30,30);
        sparse(cv::Rect(640,384,768,768)).copyTo(changed(cv::Rect(640,384,768,768)));
        engine.fuseBurstFrames({sparse,changed},false,false,false,{0,0},{100,100},&report);
        std::cout<<"sparse support: anchors="<<report.anchors<<" multiFraction="<<report.multiFrameFraction<<'\n';
        require(report.anchors==2&&report.multiFrameFraction>0&&report.multiFrameFraction<.20,"Sparse verified observations below 20 percent must still contribute locally");
    }
    {
        cv::Mat large,shifted;
        cv::resize(clean,large,{4912,3684},0,0,cv::INTER_CUBIC);
        const cv::Mat h=(cv::Mat_<double>(2,3)<<1,0,1.3,0,1,-.7);
        cv::warpAffine(large,shifted,h,large.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
        auto result=engine.fuseBurstStream(2,[&](size_t i){return (i ? large : shifted).clone();},{0,0},{100,100},&report);
        require(report.sourceSize==large.size()&&report.scale==1&&result.size()==report.crop.size(),"Experimental source above 16 MP must remain native sized");
        require(report.anchors==2&&report.multiFrameFraction>.50,"Native large-frame streaming must actually combine observations");
        require(report.reference==1&&report.geometryKnown,"Large streaming must retain changed-reference mapping");
        const auto truth=large(report.crop);
        const double error=cv::norm(result,truth,cv::NORM_L1)/(3*result.total());
        require(error<3,"Analysis-to-native homography conjugation must preserve source geometry");
        if(report.reference==1) {
            require(std::abs(report.firstToReference(0,2)+1.3)<1 && std::abs(report.firstToReference(1,2)-.7)<1,"First-source mapping remains in native pixel units");
        }
        std::cout<<"native-stream source="<<large.cols<<'x'<<large.rows<<" meanAbsError="<<error<<" multiFraction="<<report.multiFrameFraction<<'\n';
    }
    std::cout<<"PASS: noisy flat/text, faint marks, reference noise, streaming sums and false-SR protection\n";
}
