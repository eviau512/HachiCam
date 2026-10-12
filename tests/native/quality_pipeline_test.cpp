#include "local_registration.h"
#include "burst_fusion.h"
#include "perspective_corrector.h"
#include "edge_detector.h"
#include <iostream>
#include <stdexcept>

static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void runBoundaryTests();
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
int runTests() {
    std::cout << std::unitbuf;
    cv::setNumThreads(2);
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
    runBoundaryTests();
    std::cout << "PASS: local alignment, parallax, occlusion, blur, reference ranking, fusion, crop, paper and document boundary\n";
    return 0;
}
int main() { try { return runTests(); } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << "\n"; return 1; } }
