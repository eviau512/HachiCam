#include "local_registration.h"
#include "burst_fusion.h"
#include "perspective_corrector.h"
#include "edge_detector.h"
void runRegionalTests();
#include <iostream>
#include <stdexcept>
#include <limits>

static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void runBoundaryTests();
void runNoiseTests();
void runHighlightTests();
static cv::Mat textChart() {
    cv::Mat image(1024, 1024, CV_8UC3, cv::Scalar(235, 235, 235));
    for (int y = 32; y < image.rows - 20; y += 37)
        cv::putText(image, "Hachi 8B 1709 .,: " + std::to_string(y * 17), {17 + (y % 23), y},
            cv::FONT_HERSHEY_SIMPLEX, .8, cv::Scalar(35, 35, 35), 1, cv::LINE_AA);
    return image;
}
static cv::Mat shifted(const cv::Mat& image, double dx, double dy) {
    cv::Mat result;
    cv::Mat h = (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy);
    cv::warpAffine(image, result, h, image.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
    return result;
}
static double error(const cv::Mat& a, const cv::Mat& b, const cv::Mat& mask) {
    cv::Mat difference; cv::absdiff(a, b, difference);
    difference.convertTo(difference, CV_32F); difference = difference.mul(difference);
    return cv::mean(difference, mask)[0];
}
static void runHighlightDemandTests() {
    cv::Mat native(41,63,CV_8UC3,cv::Scalar::all(230));
    native.at<cv::Vec3b>(4,5)={231,20,20};
    const auto demand=fusion::highlightDemand(native,{17,23,63,41},{777,533});
    check(cv::countNonZero(demand)==1 && demand.at<uchar>(27,22),"HDR demand uses actual rounded byte channels and crop coordinates");
    cv::Mat requested=cv::Mat::zeros(533,777,CV_8U);
    for(auto p:{cv::Point(0,0),cv::Point(128,128),cv::Point(256,256),cv::Point(776,532)})
        cv::circle(requested,p,9,cv::Scalar(255),cv::FILLED);
    fusion::LocalRegistration full;full.domain=requested.size();full.patchSupport=true;
    const auto selected=fusion::highlightFitNodes(requested,full.spacing);
    full.field.create(selected.size(),CV_32FC3);
    for(int y=0;y<full.field.rows;++y)for(int x=0;x<full.field.cols;++x)
        full.field.at<cv::Vec3f>(y,x)={float(x%3)*.9f,float(y%3)*.8f,.95f};
    auto pruned=full;pruned.field=full.field.clone();
    for(int y=0;y<selected.rows;++y)for(int x=0;x<selected.cols;++x)
        if(!selected.at<uchar>(y,x))pruned.field.at<cv::Vec3f>(y,x)={0,0,0};
    int compared=0;
    for(int y=0;y<requested.rows;++y)for(int x=0;x<requested.cols;++x)if(requested.at<uchar>(y,x)) {
        cv::Point2d a,b;float ca=0,cb=0;
        const bool old=full.offset(x,y,a,ca),now=pruned.offset(x,y,b,cb);
        check(old==now,"Pruning must preserve conflicting neighbor rejection, including clamped edge nodes");
        if(old)check(cv::norm(a-b)<1e-12 && ca==cb,"Pruning must preserve selected demanded offsets");
        ++compared;
    }
    check(compared>300,"Demand footprint regression needs meaningful border and conflict coverage");
    const auto chart=textChart();cv::Mat gray;cv::cvtColor(chart,gray,cv::COLOR_BGR2GRAY);
    const auto exhaustive=fusion::registerLocal(gray,gray,true);
    cv::Mat small=cv::Mat::zeros(gray.size(),CV_8U);small(cv::Rect(250,250,10,10)).setTo(255);
    const auto fallback=fusion::registerLocal(gray,gray,true,false,cv::Mat(),nullptr,small);
    check(cv::norm(exhaustive.field,fallback.field,cv::NORM_INF)==0 &&
        cv::norm(exhaustive.gate,fallback.gate,cv::NORM_INF)==0,"Small supported demand must retain exhaustive plate admission");
    const auto none=fusion::registerLocal(gray,gray,true,false,cv::Mat(),nullptr,cv::Mat::zeros(gray.size(),CV_8U));
    check(cv::countNonZero(none.gate)==0,"Empty highlight demand cannot perform or invent recovery");
    std::cout<<"HDR demand footprint/border/conflict/admission/empty checks PASS\n";
}
int runTests() {
    std::cout << std::unitbuf;
    cv::setNumThreads(2);
    runHighlightDemandTests();
    auto chart = textChart();
    cv::Mat blurred; cv::GaussianBlur(chart, blurred, cv::Size(0, 0), 1.8);
    check(fusion::referenceQuality(chart) > fusion::referenceQuality(blurred) * 2, "Sharp reference must outrank blurred arrival");
    cv::Mat ref; cv::cvtColor(chart, ref, cv::COLOR_BGR2GRAY);
    auto upper = shifted(ref, .65, -.40), lower = shifted(ref, -2.30, .80);
    lower.rowRange(512, 1024).copyTo(upper.rowRange(512, 1024));
    cv::rectangle(upper, cv::Rect(700, 100, 100, 100), cv::Scalar(60), cv::FILLED);
    auto local = fusion::registerLocal(ref, upper);
    cv::Point2d a, b; float confidence;
    check(local.offset(256, 256, a, confidence), "Upper-depth text needs valid local registration");
    check(local.offset(256, 768, b, confidence), "Lower-depth text needs valid local registration");
    std::cout << "depth offsets=" << a << "," << b << " support=" << local.supportedFraction << '\n';
    check(cv::norm(a - cv::Point2d(.65, -.40)) < .20, "Upper local translation accuracy");
    check(cv::norm(b - cv::Point2d(-2.30, .80)) < .20, "Lower local translation accuracy");
    check(cv::countNonZero(local.gate(cv::Rect(720, 120, 50, 50))) == 0, "Occlusion must not mix into text");
    cv::Mat mapX(ref.size(), CV_32F), mapY(ref.size(), CV_32F);
    for (int y = 0; y < ref.rows; ++y) for (int x = 0; x < ref.cols; ++x) {
        cv::Point2d d; float c;
        if (!local.offset(x, y, d, c)) d = {0, 0};
        mapX.at<float>(y,x) = x + d.x; mapY.at<float>(y,x) = y + d.y;
    }
    cv::Mat corrected; cv::remap(upper, corrected, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
    const double before = error(ref, upper, local.gate), after = error(ref, corrected, local.gate);
    std::cout << "local residual MSE=" << before << " -> " << after << '\n';
    check(after < before * .5, "Local registration must reduce supported text error");
    cv::Mat blurGray; cv::cvtColor(blurred, blurGray, cv::COLOR_BGR2GRAY);
    const auto blurModel = fusion::registerLocal(ref, blurGray);
    const cv::Mat inkMask = ref < 200;
    const double blurredInkSupport = double(cv::countNonZero(blurModel.gate & inkMask)) / cv::countNonZero(inkMask);
    std::cout << "blur total support=" << blurModel.supportedFraction << " ink support=" << blurredInkSupport << '\n';
    check(blurredInkSupport < .10, "Blurred text cannot contribute; blank-region noise support is allowed");

    BurstFusionEngine engine;
    {
        cv::Mat projective;const cv::Mat h=(cv::Mat_<double>(3,3)<<1,.003,4,.002,1,-3,.00001,-.000015,1);
        cv::warpPerspective(chart,projective,h,chart.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
        cv::Mat first,firstH,chosen,chosenH;
        check(engine.alignFrameECC(projective,chart,first,firstH),"Fixture needs a plausible first Euclidean candidate");
        check(engine.alignBaseFrame(projective,chart,chosen,chosenH),"Projective text needs a selected registration");
        const cv::Rect interior(64,64,896,896);
        const double before=cv::norm(first(interior),chart(interior),cv::NORM_L2SQR)/interior.area();
        const double after=cv::norm(chosen(interior),chart(interior),cv::NORM_L2SQR)/interior.area();
        std::cout<<"projective candidate residual="<<before<<" -> "<<after<<'\n';
        check(after<before*.55,"Candidate comparison must beat first-success text alignment");
    }
    BurstFusionEngine::Report report;
    const auto result = engine.fuseBurstFrames({blurred, chart, shifted(chart, .65, .10), shifted(chart, .10, .65)},
        false, false, true, {0,0,0,0}, {100,100,100,100}, &report);
    check(report.reference == 1, "Best reference replaces blurred first arrival");
    check(report.anchors >= 3, "Multi-frame reconstruction remains active");
    check(report.crop.x <= chart.cols * .05 && report.crop.y <= chart.rows * .05, "Bounded source-border loss");
    check(result.cols == report.crop.width * report.scale && result.rows == report.crop.height * report.scale, "Report describes actual crop and scale");
    check(report.geometryKnown, "Reference change retains first-frame coordinate mapping");
    const auto same = engine.fuseBurstFrames({chart, chart, chart}, false, false, true, {0,0,0}, {100,100,100}, &report);
    check(report.scale == 1 && report.anchors == 3 && !same.empty(), "Identical frames retain native-size fusion without a false SR gain");
    engine.fuseBurstFrames({chart, shifted(chart, 65, 0)}, false, false, false, {0,0}, {100,100}, &report);
    check(report.anchors == 1 && report.crop == cv::Rect(0,0,1024,1024), "Reject a frame requiring more than 5 percent border loss");

    PerspectiveCorrector filters;
    cv::Mat page(600, 800, CV_8UC3);
    for (int y=0;y<page.rows;++y) for(int x=0;x<page.cols;++x)
        page.at<cv::Vec3b>(y,x) = {uchar(170+60*x/800), uchar(180+60*x/800), uchar(190+60*x/800)};
    cv::rectangle(page, {200,200,200,3}, cv::Scalar(173,183,193), cv::FILLED);
    cv::putText(page, "1.08", {300,300}, cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(30,40,170), 2);
    const cv::Mat original = page.clone(), paper = filters.enhancePaper(page);
    check(cv::norm(original, page, cv::NORM_INF) == 0, "Paper rendering preserves the input master");
    check(paper.type() == CV_8UC3, "Paper is continuous color output");
    cv::Mat colorMask(page.size(), CV_8U, cv::Scalar(0));
    for (int y=0;y<page.rows;++y) for(int x=0;x<page.cols;++x) {
        const auto pixel = page.at<cv::Vec3b>(y,x);
        if (pixel[2] > pixel[0] + 60) colorMask.at<uchar>(y,x) = 255;
    }
    check(cv::countNonZero(colorMask) > 30, "Fixture contains genuine colored ink");
    const auto colorMean = cv::mean(paper, colorMask);
    check(colorMean[2] - colorMean[0] > 60, "Colored ink retains chroma");
    check(paper.at<cv::Vec3b>(206,300)[0] - paper.at<cv::Vec3b>(201,300)[0] >= 4, "Faint stroke remains distinct from paper");
    for (int channels : {1,3,4}) {
        cv::Mat tiny(1, 1, CV_MAKETYPE(CV_8U, channels), cv::Scalar::all(190));
        check(filters.enhancePaper(tiny).size() == tiny.size(), "Tiny and channel-normalized input");
    }
    cv::Mat blank(640,640,CV_8UC3,cv::Scalar(160,160,160));
    cv::rectangle(blank, {80,50,480,540}, cv::Scalar(230,230,230), cv::FILLED);
    auto document = EdgeDetector().detectDocument(blank);
    check(document.found && cv::contourArea(document.corners) > 230000, "Blank outer page remains eligible");
    for (int y=180;y<460;y+=35) cv::line(blank,{180,y},{470,y},cv::Scalar(20,20,20),2);
    for (int x=180;x<480;x+=50) cv::line(blank,{x,180},{x,460},cv::Scalar(20,20,20),2);
    document = EdgeDetector().detectDocument(blank);
    check(document.found && cv::contourArea(document.corners) > 230000, "Inner table cannot replace the plausible page boundary");
    // Production geometry rejects invalid evidence before allocation and leaves source intact.
    cv::Mat geometrySource(2048,2048,CV_8UC3,cv::Scalar(90,100,110));
    std::vector<cv::Point2f> validCorners{{0,0},{2047,0},{2047,2047},{0,2047}};
    auto rejectsGeometry=[&](std::vector<cv::Point2f> points,float ratio) {
        try { filters.correctPerspective(geometrySource,points,ratio); return false; }
        catch(const std::invalid_argument&) { return true; }
    };
    auto crossed=validCorners;std::swap(crossed[1],crossed[2]);
    check(rejectsGeometry(crossed,0),"Crossed crop rejected before warp");
    auto nonfinite=validCorners;nonfinite[1].x=std::numeric_limits<float>::quiet_NaN();
    check(rejectsGeometry(nonfinite,0),"Nonfinite crop rejected before warp");
    check(rejectsGeometry(validCorners,20),"Expanded perspective output obeys pixel budget");
    check(rejectsGeometry(validCorners,std::numeric_limits<float>::infinity()),"Nonfinite aspect rejected");
    check(geometrySource.at<cv::Vec3b>(0,0)==cv::Vec3b(90,100,110),"Invalid edits preserve source pixels");
    runRegionalTests();
    runBoundaryTests();
    runNoiseTests();
    runHighlightTests();
    std::cout << "PASS: local alignment, parallax, occlusion, blur, noise, highlights, reference ranking, fusion, crop, paper and document boundary\n";
    return 0;
}
int main() { try { return runTests(); } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << "\n"; return 1; } }
