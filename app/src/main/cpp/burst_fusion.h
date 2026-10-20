#ifndef BURST_FUSION_H
#define BURST_FUSION_H

#include <opencv2/opencv.hpp>
#include <opencv2/photo.hpp>
#include <vector>
#include <functional>
#include <chrono>

class BurstFusionEngine {
public:
    struct Performance {
        double totalMs=0,globalRegistrationMs=0,localRegistrationMs=0;
        int gpuSupportMaps=0,gpuFitProposals=0;
        int gpuNoiseGrids=0,gpuMergeTiles=0;
        long long cpuMergePixels=0;
        double gpuMergeMs=0;
        int gpuSelectorPatches=0,cpuSelectorPatches=0;
        double selectorMs=0;
        int gpuGlobalCalls=0,gpuGlobalIterations=0;
        double gpuGlobalMs=0;
    };
    struct Report {
        int anchors = 1, highlights = 0, scale = 1, reference = 0;
        cv::Rect crop;
        cv::Size sourceSize;
        cv::Matx33d firstToReference = cv::Matx33d::eye();
        bool geometryKnown = true;
        double multiFrameFraction = 0, meanContributors = 1;
        double recoveredFraction = 0, clippedFraction = 0, postClippedFraction = 0;
        bool srRequested=false,centrePhases=false;
        int srDenied=32,regionTiles=0,qualifiedTiles=0;
        double meanSupport=0,regionFraction=0,reconstructedFraction=0;
        int fittedTiles=0;
        double initialFitError=0,finalFitError=0;
        Performance performance;
        std::string backendDecisions;
    };
    // Experimental CPU JPEG fusion. Select a base reference, validate global
    // and sparse local registration, and sample accepted originals directly.
    // 2x output requires independent phases and local detail support; dimensions
    // alone do not establish recovered resolving power. Report includes the
    // common source rectangle and first-frame -> chosen-reference mapping.
    cv::Mat fuseBurstFrames(const std::vector<cv::Mat>& burstFrames, bool removeGlare = false, bool isScreenMode = false, bool superResolution = false,
                           const std::vector<int>& tiers = {}, const std::vector<double>& exposureProducts = {}, Report* report = nullptr);
    // Native-size, sequential decode for the experimental high-resolution profile.
    // loader returns one owned decoded frame; at most reference + one candidate
    // are retained. Local validation uses bounded source-resolution patches.
    cv::Mat fuseBurstStream(size_t count, const std::function<cv::Mat(size_t)>& loader,
        const std::vector<int>& tiers, const std::vector<double>& exposures, Report* report = nullptr);

    // Global registration candidate; quality is checked again locally.
    bool alignFrameHomography(const cv::Mat& src, const cv::Mat& ref, cv::Mat& outWarped, cv::Mat& outH);
    bool alignBaseFrame(const cv::Mat& src, const cv::Mat& ref, cv::Mat& outWarped, cv::Mat& outH);

    // 增强相关系数配准 (Enhanced Correlation Coefficient, ECC)
    // Global registration candidate; quality is checked again locally.
    bool alignFrameECC(const cv::Mat& src, const cv::Mat& ref, cv::Mat& outWarped, cv::Mat& outH);

    // 基于 Greg Ward 经典中值阈值位图的多曝光对齐 (AlignMTB)
    // Global registration candidate; quality is checked again locally.
    bool alignFrameMTB(const cv::Mat& src, const cv::Mat& ref, cv::Mat& outWarped, cv::Mat& outH);

    // 梯度域相位相关配准 (Gradient-Domain Phase Correlation)
    // Global registration candidate; quality is checked again locally.
    bool alignGradientPhaseCorrelation(const cv::Mat& src, const cv::Mat& ref, cv::Mat& outWarped, cv::Mat& outH);

    // 高光多候选 ROI 归一化互相关模板精配准 (Multi-ROI NCC Template Match)
    // Global registration candidate; quality is checked again locally.
    bool alignHighlightTemplate(const cv::Mat& srcShort, const cv::Mat& refBase, cv::Mat& outWarped, cv::Mat& outH);

    // 暗光高动态场景极高光结构配准 (Highlight Structure & Multi-Algorithm Alignment)
    // Global registration candidate; quality is checked again locally.
    bool alignHighlightFrame(const cv::Mat& srcShort, const cv::Mat& refBase, cv::Mat& outWarped, cv::Mat& outH, double exposureGain = 1);

};

#endif // BURST_FUSION_H
