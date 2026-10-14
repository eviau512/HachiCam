#pragma once
#include <opencv2/core.hpp>

namespace fusion {
struct ReferencePreparation {
    const unsigned char* identity=nullptr;
    cv::Size domain;
    cv::Mat highPass, align, smooth, noise;
    float noiseSigma=0;
};
ReferencePreparation prepareReference(const cv::Mat& reference);
struct LocalRegistration {
    int gpuFitProposals=0;
    bool gpuSupportMap=false;
    cv::Mat field; // CV_32FC3: reference -> globally warped source dx,dy,confidence
    cv::Mat gate;  // CV_8U, source-resolution local photometric support
    cv::Mat detail; // CV_8U nodes qualifying structure, separate from denoising.
    int spacing = 256;
    float supportedFraction = 0;
    float detailFraction = 0;
    float maxOffset = 0;
    float denoiseOnlyFraction = 0;
    float noiseSigma = 0;
    bool patchSupport = false; // Highlights use measured patch interiors only.
    cv::Size domain;
    bool offset(double x, double y, cv::Point2d& delta, float& confidence) const;
    bool hasDetail(double x, double y) const;
};
LocalRegistration registerLocal(const cv::Mat& referenceGray, const cv::Mat& warpedGray, bool highlight = false, bool singlePatch = false, const cv::Mat& referenceValidity = cv::Mat(), const ReferencePreparation* prepared = nullptr);
cv::Mat unclippedReference(const cv::Mat& image);
cv::Mat highlightNeighborhood(const cv::Mat& image, float gain);
double referenceQuality(const cv::Mat& image);
double estimateNoiseSigma(const cv::Mat& gray);
cv::Mat normalizeExposure(const cv::Mat& image, double gain);
float responseGain(const cv::Mat& base, const cv::Mat& aligned, double fallback);
}
