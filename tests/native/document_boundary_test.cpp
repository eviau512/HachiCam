// Independently drawn fixtures; no competitor images or implementations.
#include "edge_detector.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
cv::Mat desk() {
    cv::Mat image(640,480,CV_8U,cv::Scalar(145));
    for(int y=0;y<640;y+=13)cv::line(image,{0,y},{479,y+27},cv::Scalar(151),1);
    return image;
}
void sheet(cv::Mat& image,const std::vector<cv::Point>& points,uchar value) {
    cv::fillConvexPoly(image,points,cv::Scalar(value),cv::LINE_AA);
}
void text(cv::Mat& image) {
    for(int y=260;y<440;y+=38)cv::putText(image,"18:45 Hachi 8B",{170,y},cv::FONT_HERSHEY_SIMPLEX,.4,cv::Scalar(105),1);
}
void compare(const DetectionResult& r,const std::vector<cv::Point2f>& expected,float tolerance) {
    require(r.found && r.corners.size()==4,"Expected a document proposal");
    const auto ordered=EdgeDetector::sortCorners(expected);
    for(int i=0;i<4;++i)require(cv::norm(r.corners[i]-ordered[i])<tolerance,"Supported boundary must remain near physical sides");
}
}

void runBoundaryTests() {
    EdgeDetector detector;
    cv::Mat uniform(640,480,CV_8U,cv::Scalar(180));
    require(!detector.detectDocument(uniform).found,"Uniform field must not fabricate a page");
    require(!detector.detectDocument(desk()).found,"Open desk texture must not fabricate a page");
    cv::Mat contradictory(640,480,CV_8U,cv::Scalar(100));
    cv::rectangle(contradictory,{400,0,80,640},cv::Scalar(240),cv::FILLED);
    cv::rectangle(contradictory,{80,100,320,440},cv::Scalar(200),cv::FILLED);
    require(!detector.detectDocument(contradictory).found,"Opposing side polarity must use the uncertain/manual outcome");
    const std::vector<cv::Point> complete{{130,120},{420,140},{400,550},{45,515}};
    std::vector<cv::Point2f> completeFloat(complete.begin(),complete.end());
    for(uchar value:{uchar(235),uchar(45)}) {
        auto image=desk();sheet(image,complete,value);
        const auto r=detector.detectDocument(image);
        compare(r,completeFloat,5);
        require(!r.partial,"Fully visible blank/dark paper must permit a complete proposal");
    }
    auto receipt=desk();cv::rectangle(receipt,{220,80,36,420},cv::Scalar(238),cv::FILLED);
    const auto longPage=detector.detectDocument(receipt);
    require(longPage.found&&!longPage.partial&&cv::contourArea(longPage.corners)>14000,"Receipt near 12:1 remains eligible");
    auto broken=desk();sheet(broken,complete,235);text(broken);
    // Occluding strips interrupt contour connectivity, leaving observable side fragments.
    cv::rectangle(broken,{250,114,8,40},cv::Scalar(145),cv::FILLED);
    cv::rectangle(broken,{65,340,50,8},cv::Scalar(145),cv::FILLED);
    auto repaired=detector.detectDocument(broken);
    compare(repaired,completeFloat,8);
    require(!repaired.partial,"Broken but visible sides can form a complete proposal");
    auto hiddenCorner=desk();sheet(hiddenCorner,complete,235);text(hiddenCorner);
    cv::rectangle(hiddenCorner,{400,120,38,45},cv::Scalar(100),cv::FILLED);
    const auto occluded=detector.detectDocument(hiddenCorner);
    compare(occluded,completeFloat,8);
    require(occluded.partial,"An occluded corner inside the frame also needs manual confirmation");

    const std::vector<cv::Point> partial{{158,147},{570,135},{416,544},{31,494}};
    std::vector<cv::Point2f> partialFloat(partial.begin(),partial.end());
    auto stack=desk();
    sheet(stack,{{120,130},{590,190},{370,700},{10,460}},220);
    sheet(stack,partial,240);text(stack);
    // Clip covers part of the top side; lower sheet remains visible below it.
    cv::rectangle(stack,{380,125,20,32},cv::Scalar(90),cv::FILLED);
    const auto proposal=detector.detectDocument(stack);
    compare(proposal,partialFloat,12);
    require(proposal.partial&&proposal.method==2,"Stacked/out-of-frame page must be a partial line suggestion");
    require(proposal.sideSupport>=.4f,"Partial sides still need observed support");
    for(int angle:{cv::ROTATE_90_CLOCKWISE,cv::ROTATE_180,cv::ROTATE_90_COUNTERCLOCKWISE}) {
        cv::Mat rotated;cv::rotate(stack,rotated,angle);
        const auto r=detector.detectDocument(rotated);
        require(r.found&&r.partial,"Partial status survives all preview rotations");
    }

    // Private user evidence stays in ignored .tools. Optional reproduction uses
    // exactly the production C++, not a separate Python detector.
    for(int i=1;i<=2;++i) {
        std::ifstream file(".tools/detection-evidence/preview"+std::to_string(i)+".pgm",std::ios::binary);
        if(!file)continue;
        std::string magic;int w,h,maximum;file>>magic>>w>>h>>maximum;file.get();
        require(magic=="P5"&&maximum==255&&w>0&&h>0,"Private screenshot fixture header");
        cv::Mat image(h,w,CV_8U);file.read(reinterpret_cast<char*>(image.data),w*h);
        require(bool(file),"Private screenshot fixture must be complete");
        const auto start=std::chrono::steady_clock::now();
        DetectionResult r;
        for(int n=0;n<10;++n)r=detector.detectDocument(image);
        const auto duration=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/10;
        require(r.found&&r.partial&&r.method==2,"Both user previews need a partial/manual outcome, never desk/full crop");
        std::cout<<"screenshot="<<i<<" partial="<<r.partial<<" minSide="<<r.sideSupport<<" candidates="<<r.candidates<<" meanMs="<<duration<<" quad="<<r.corners<<'\n';
        // A preview may have limited-range luma, resampling and subpixel motion.
        // These are tuning perturbations, not additional independent captures.
        for(int variant=0;variant<4;++variant) {
            cv::Mat moved,luma;
            cv::Mat transform=(cv::Mat_<double>(2,3)<<1,0,(variant%2 ? 2. : -2.),0,1,(variant<2 ? 2. : -2.));
            cv::warpAffine(image,moved,transform,image.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
            moved.convertTo(luma,CV_8U,variant%2 ? .86 : 1.06,variant%2 ? 16 : -8);
            const auto changed=detector.detectDocument(luma);
            require(changed.found&&changed.partial,"Small preview changes must retain the partial/manual outcome");
            for(int corner=0;corner<4;++corner)
                require(cv::norm(changed.corners[corner]-r.corners[corner])<.03*std::hypot(w,h),"Nearby evidence must retain target/corner continuity");
        }
    }
    std::cout<<"PASS: complete, dark, blank, long, broken, stacked/partial and negative boundary fixtures\n";
}
