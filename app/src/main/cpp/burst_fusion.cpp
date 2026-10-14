#include "burst_fusion.h"
#include "local_registration.h"
#include <opencv2/photo.hpp>
#include <opencv2/video.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <android/log.h>

#define LOG_TAG "HachiCam-Fusion"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ============================================================================
// [SPEC_02 §2.4 / v23] 对齐校验辅助函数
// 1. maxDisplacement: 位移场 (中心+四角) 最大位移，取代对矩阵平移分量 (绕原点旋转时失真) 的硬上限
// 2. alignmentScores / verifyAlignment: 半分辨率高通 ZNCC 结构校验，取代固定像素物理熔断
// ============================================================================
static double maxDisplacement(const cv::Mat& H, int rows, int cols) {
    if (H.empty() || H.rows != 3 || H.cols != 3) return 1e9;
    cv::Mat Hd;
    H.convertTo(Hd, CV_64F);
    if (!cv::checkRange(Hd)) return 1e9;
    const cv::Point2d pts[5] = {
        {cols * 0.5, rows * 0.5}, {0.0, 0.0}, {static_cast<double>(cols), 0.0},
        {0.0, static_cast<double>(rows)}, {static_cast<double>(cols), static_cast<double>(rows)}
    };
    double m = 0.0;
    for (const auto& p : pts) {
        double x = Hd.at<double>(0, 0) * p.x + Hd.at<double>(0, 1) * p.y + Hd.at<double>(0, 2);
        double y = Hd.at<double>(1, 0) * p.x + Hd.at<double>(1, 1) * p.y + Hd.at<double>(1, 2);
        double w = Hd.at<double>(2, 0) * p.x + Hd.at<double>(2, 1) * p.y + Hd.at<double>(2, 2);
        if (std::abs(w) < 1e-9) return 1e9;
        m = std::max(m, std::hypot(x / w - p.x, y / w - p.y));
    }
    return m;
}

static bool displacementWithinLimit(const cv::Mat& H, int rows, int cols, const char* tag) {
    double limit = 0.08 * static_cast<double>(std::max(rows, cols));
    double d = maxDisplacement(H, rows, cols);
    if (d > limit) {
        LOGW("%s: displacement field max=%.1fpx > limit %.1fpx -> rejected", tag, d, limit);
        return false;
    }
    return true;
}

static bool alignmentScores(const cv::Mat& src, const cv::Mat& ref, const cv::Mat& H,
                            double& zH, double& zI, double& lumaRatio) {
    try {
        int maxDim = std::max(ref.cols, ref.rows);
        double s = (maxDim > 2048) ? 2048.0 / maxDim : 1.0;
        cv::Mat gs, gr;
        if (src.channels() == 3) cv::cvtColor(src, gs, cv::COLOR_BGR2GRAY); else gs = src;
        if (ref.channels() == 3) cv::cvtColor(ref, gr, cv::COLOR_BGR2GRAY); else gr = ref;
        if (s < 1.0) {
            cv::resize(gs, gs, cv::Size(), s, s, cv::INTER_AREA);
            cv::resize(gr, gr, cv::Size(), s, s, cv::INTER_AREA);
        }
        cv::Mat Hd;
        H.convertTo(Hd, CV_64F);
        cv::Mat S = (cv::Mat_<double>(3, 3) << s, 0, 0, 0, s, 0, 0, 0, 1);
        cv::Mat Si = (cv::Mat_<double>(3, 3) << 1.0 / s, 0, 0, 0, 1.0 / s, 0, 0, 0, 1);
        cv::Mat Hs = S * Hd * Si;

        auto highPass = [](const cv::Mat& g) {
            cv::Mat f, a, b;
            g.convertTo(f, CV_32F);
            cv::GaussianBlur(f, a, cv::Size(0, 0), 1.2);
            cv::GaussianBlur(f, b, cv::Size(0, 0), 5.0);
            return cv::Mat(a - b);
        };
        cv::Mat hpRef = highPass(gr);
        cv::Mat hpSrc = highPass(gs);

        cv::Mat hpWarp, gsWarp, ones(gs.size(), CV_8U, cv::Scalar(255)), vmask;
        cv::warpPerspective(hpSrc, hpWarp, Hs, gr.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
        cv::warpPerspective(gs, gsWarp, Hs, gr.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
        cv::warpPerspective(ones, vmask, Hs, gr.size(), cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(0));

        // [SPEC_02 §2.5.3 v24] 全图估算曝光亮度比 (lumaRatio)，绝不可受局部掩模裁剪影响，
        // 彻底杜绝超短曝光帧 (Tier 3) 因掩模有效像素<1000 导致 mean 归零并误判为 sameExposure=1！
        double mRefAll = cv::mean(gr)[0];
        double mSrcAll = cv::mean(gs)[0];
        lumaRatio = (mRefAll > 0.01) ? (mSrcAll / mRefAll) : 1.0;

        auto zncc = [](const cv::Mat& a, const cv::Mat& b, const cv::Mat& mask) {
            if (cv::countNonZero(mask) < 200) return -1.0;
            cv::Scalar ma, sa, mb, sb;
            cv::meanStdDev(a, ma, sa, mask);
            cv::meanStdDev(b, mb, sb, mask);
            double mab = cv::mean(a.mul(b), mask)[0];
            return (mab - ma[0] * mb[0]) / (sa[0] * sb[0] + 1e-6);
        };

        cv::Mat refOk = (gr >= 4) & (gr <= 250);
        cv::Mat warpOk = (gsWarp >= 4) & (gsWarp <= 250) & (vmask > 0) & refOk;
        cv::Mat idOk = (gs >= 4) & (gs <= 250) & refOk;
        zH = zncc(hpWarp, hpRef, warpOk);
        zI = zncc(hpSrc, hpRef, idOk);
        if (cv::countNonZero(warpOk) >= 200) {
            double mRef = cv::mean(gr, warpOk)[0];
            double mSrc = cv::mean(gsWarp, warpOk)[0];
            if (mRef > 1.0) lumaRatio = mSrc / mRef;
        }
        return true;
    } catch (const std::exception& e) {
        LOGW("alignmentScores exception: %s", e.what());
        return false;
    }
}

// 同曝光对：高通 ZNCC 必须显著为正且不劣于“不对齐”；跨曝光对：仅记录，交由对齐器自身置信度裁决
static bool verifyAlignment(const cv::Mat& src, const cv::Mat& ref, const cv::Mat& H, const char* tag) {
    double zH = 0.0, zI = 0.0, ratio = 1.0;
    if (!alignmentScores(src, ref, H, zH, zI, ratio)) return false;
    bool sameExposure = (ratio >= 0.7 && ratio <= 1.4);
    // 绝对下限极低 (纯噪声互相关≈0)；位移 > 2px 的变换必须严格优于“不对齐”才可接受
    double moved = maxDisplacement(H, ref.rows, ref.cols);
    bool ok = !sameExposure || (zH >= 0.03 && zH >= zI - 0.01 && (moved <= 2.0 || zH >= zI + 0.02));
    LOGI("verify[%s]: hp-zncc=%.3f identity=%.3f moved=%.1fpx lumaRatio=%.2f sameExposure=%d -> %s",
         tag, zH, zI, moved, ratio, sameExposure ? 1 : 0, ok ? "ACCEPT" : "REJECT");
    return ok;
}

static bool projectiveECC(const cv::Mat& src,const cv::Mat& ref,const cv::Mat& initial,cv::Mat& warped,cv::Mat& h) {
    try {
        cv::Mat gs,gr;cv::cvtColor(src,gs,cv::COLOR_BGR2GRAY);cv::cvtColor(ref,gr,cv::COLOR_BGR2GRAY);
        const double ratio=std::min(1.,1280./std::max(ref.cols,ref.rows));
        if(ratio<1){cv::resize(gs,gs,{},ratio,ratio,cv::INTER_AREA);cv::resize(gr,gr,{},ratio,ratio,cv::INTER_AREA);}
        const double sx=double(gr.cols)/ref.cols,sy=double(gr.rows)/ref.rows;
        const cv::Mat s=(cv::Mat_<double>(3,3)<<sx,0,.5*sx-.5,0,sy,.5*sy-.5,0,0,1);
        cv::Mat inverse=initial.empty() ? cv::Mat::eye(3,3,CV_64F) : initial.inv();
        cv::Mat w=s*inverse*s.inv();w.convertTo(w,CV_32F);
        const double cc=cv::findTransformECC(gr,gs,w,cv::MOTION_HOMOGRAPHY,
            cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,40,1e-5));
        if(!std::isfinite(cc)||cc<.60)return false;
        w.convertTo(w,CV_64F);h=s.inv()*w.inv()*s;
        if(!cv::checkRange(h)||std::abs(h.at<double>(2,2))<1e-9)return false;
        h/=h.at<double>(2,2);
        if(std::abs(cv::determinant(h))<1e-9||!displacementWithinLimit(h,ref.rows,ref.cols,"ProjectiveECC")||
            !verifyAlignment(src,ref,h,"ProjectiveECC"))return false;
        cv::warpPerspective(src,warped,h,ref.size(),cv::INTER_LINEAR,cv::BORDER_CONSTANT);
        return true;
    } catch(const cv::Exception&) {return false;}
}

bool BurstFusionEngine::alignBaseFrame(const cv::Mat& src,const cv::Mat& ref,cv::Mat& outWarped,cv::Mat& outH) {
    double best=-2;const char* selected="none";
    outWarped.release();outH.release();
    for(int method=0;method<4;++method) {
        cv::Mat warped,h;
        const bool accepted=method==0 ? alignFrameECC(src,ref,warped,h) :
            method==1 ? projectiveECC(src,ref,outH,warped,h) :
            method==2 ? alignFrameHomography(src,ref,warped,h) : alignGradientPhaseCorrelation(src,ref,warped,h);
        if(!accepted)continue;
        double score,identity,ratio;
        if(!alignmentScores(src,ref,h,score,identity,ratio)||!std::isfinite(score))continue;
        const char* name=method==0 ? "ECC" : method==1 ? "ProjectiveECC" : method==2 ? "ORB-Homography" : "Phase";
        LOGI("Base candidate %s score=%.4f",name,score);
        if(score>best+.02) {best=score;selected=name;outH=h;outWarped=warped;}
        if(best>=.985)break;
    }
    LOGI("Base selected=%s score=%.4f",selected,best);
    return best>-2;
}

// ============================================================================
// 第一级：全局单应性粗配准 (ORB + RANSAC Homography)
// ============================================================================
bool BurstFusionEngine::alignFrameHomography(const cv::Mat& src, const cv::Mat& ref, cv::Mat& outWarped, cv::Mat& outH) {
    if (src.empty() || ref.empty() || src.size() != ref.size()) {
        return false;
    }

    int maxDim = std::max(ref.cols, ref.rows);
    float scale = (maxDim > 960) ? 960.0f / maxDim : 1.0f;

    cv::Mat graySrc, grayRef;
    if (src.channels() == 3) cv::cvtColor(src, graySrc, cv::COLOR_BGR2GRAY);
    else graySrc = src;

    if (ref.channels() == 3) cv::cvtColor(ref, grayRef, cv::COLOR_BGR2GRAY);
    else grayRef = ref;

    cv::Mat smallSrc, smallRef;
    if (scale < 1.0f) {
        cv::resize(graySrc, smallSrc, cv::Size(), scale, scale, cv::INTER_AREA);
        cv::resize(grayRef, smallRef, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        smallSrc = graySrc;
        smallRef = grayRef;
    }

    // CLAHE 均衡化局部对比度，使暗部细节和过曝边缘具备清晰梯度
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(3.0, cv::Size(8, 8));
    cv::Mat claheSrc, claheRef;
    clahe->apply(smallSrc, claheSrc);
    clahe->apply(smallRef, claheRef);

    // 降低 fastThreshold 至 8，确保暗光场景提取丰富角点
    cv::Ptr<cv::ORB> orb = cv::ORB::create(1500, 1.2f, 4, 31, 0, 2, cv::ORB::HARRIS_SCORE, 31, 8);
    std::vector<cv::KeyPoint> kpSrc, kpRef;
    cv::Mat descSrc, descRef;
    orb->detectAndCompute(claheSrc, cv::noArray(), kpSrc, descSrc);
    orb->detectAndCompute(claheRef, cv::noArray(), kpRef, descRef);

    if (descSrc.empty() || descRef.empty() || kpSrc.size() < 15 || kpRef.size() < 15) {
        return false;
    }

    cv::BFMatcher matcher(cv::NORM_HAMMING, true);
    std::vector<cv::DMatch> matches;
    matcher.match(descSrc, descRef, matches);

    if (matches.size() < 12) {
        return false;
    }

    std::sort(matches.begin(), matches.end(), [](const cv::DMatch& a, const cv::DMatch& b) {
        return a.distance < b.distance;
    });

    int goodCount = std::min(static_cast<int>(matches.size()), 180);
    std::vector<cv::Point2f> ptsSrc, ptsRef;
    ptsSrc.reserve(goodCount);
    ptsRef.reserve(goodCount);

    float invScale = (scale < 1.0f) ? (1.0f / scale) : 1.0f;
    for (int i = 0; i < goodCount; ++i) {
        ptsSrc.push_back(kpSrc[matches[i].queryIdx].pt * invScale);
        ptsRef.push_back(kpRef[matches[i].trainIdx].pt * invScale);
    }

    cv::Mat inlierMask;
    cv::Mat H = cv::findHomography(ptsSrc, ptsRef, cv::RANSAC, 3.0, inlierMask);
    if (H.empty()) {
        return false;
    }

    int inlierCount = cv::countNonZero(inlierMask);
    float inlierRatio = (goodCount > 0) ? (static_cast<float>(inlierCount) / static_cast<float>(goodCount)) : 0.0f;
    if (inlierCount < 15 || inlierRatio < 0.12f) {
        return false;
    }

    double det = H.at<double>(0,0) * H.at<double>(1,1) - H.at<double>(0,1) * H.at<double>(1,0);
    if (std::abs(det - 1.0) > 0.30) {
        return false;
    }

    // [SPEC_02 §2.4.2 v23] 位移场合理性 (中心+四角) + 半分辨率高通 ZNCC 结构校验，取代固定 tx/ty 像素熔断
    if (!displacementWithinLimit(H, ref.rows, ref.cols, "alignFrameHomography")) {
        return false;
    }
    if (!verifyAlignment(src, ref, H, "ORB-Homography")) {
        return false;
    }

    outH = H;
    cv::warpPerspective(src, outWarped, H, ref.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    return true;
}

// ============================================================================
// 第二级：增强相关系数配准 (Enhanced Correlation Coefficient, ECC)
// 专为跨曝光级差、屏幕摩尔纹与低对比度场景设计：
// 1. 在低分辨率金字塔 (宽约 480) 运行，耗时仅 ~15ms
// 2. 具有完全的光度不变性 (Photometric Invariance)，完全免疫 EV-2.5 曝光衰减
// 3. 欧几里得刚体模型 (平移+旋转) 求解亚像素位移，彻底解决暗光屏幕重影
// ============================================================================
bool BurstFusionEngine::alignFrameECC(const cv::Mat& src, const cv::Mat& ref, cv::Mat& outWarped, cv::Mat& outH) {
    if (src.empty() || ref.empty() || src.size() != ref.size()) {
        return false;
    }

    try {
        int maxDim = std::max(ref.cols, ref.rows);
        float scale = (maxDim > 480) ? 480.0f / maxDim : 1.0f;

        cv::Mat graySrc, grayRef;
        if (src.channels() == 3) cv::cvtColor(src, graySrc, cv::COLOR_BGR2GRAY);
        else graySrc = src;
        if (ref.channels() == 3) cv::cvtColor(ref, grayRef, cv::COLOR_BGR2GRAY);
        else grayRef = ref;

        cv::Mat smallSrc, smallRef;
        if (scale < 1.0f) {
            cv::resize(graySrc, smallSrc, cv::Size(), scale, scale, cv::INTER_AREA);
            cv::resize(grayRef, smallRef, cv::Size(), scale, scale, cv::INTER_AREA);
        } else {
            smallSrc = graySrc;
            smallRef = grayRef;
        }

        cv::Mat warpMatrix = cv::Mat::eye(2, 3, CV_32F);
        cv::TermCriteria criteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 30, 0.005);
        double cc = cv::findTransformECC(smallRef, smallSrc, warpMatrix, cv::MOTION_EUCLIDEAN, criteria);

        // Coarse 480px alignment alone is insufficient for source-pixel phase
        // reconstruction. Refine the same transform on a larger luma pyramid.
        if (maxDim > 480 && cc >= 0.60) {
            const float refinedScale = std::min(1.0f, 1920.0f / maxDim);
            warpMatrix.at<float>(0, 2) *= refinedScale / scale;
            warpMatrix.at<float>(1, 2) *= refinedScale / scale;
            cv::resize(graySrc, smallSrc, cv::Size(), refinedScale, refinedScale, cv::INTER_AREA);
            cv::resize(grayRef, smallRef, cv::Size(), refinedScale, refinedScale, cv::INTER_AREA);
            cc = cv::findTransformECC(smallRef, smallSrc, warpMatrix, cv::MOTION_EUCLIDEAN,
                cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 40, 1e-5));
            scale = refinedScale;
        }

        float invScale = (scale < 1.0f) ? (1.0f / scale) : 1.0f;
        double a00 = warpMatrix.at<float>(0, 0);
        double a01 = warpMatrix.at<float>(0, 1);
        double a10 = warpMatrix.at<float>(1, 0);
        double a11 = warpMatrix.at<float>(1, 1);
        double tx  = warpMatrix.at<float>(0, 2) * invScale;
        double ty  = warpMatrix.at<float>(1, 2) * invScale;

        double s = std::sqrt(a00 * a00 + a01 * a01);
        double theta = std::atan2(a10, a00);
        double transDist = std::hypot(tx, ty);

        LOGI("alignFrameECC: cc=%.4f, rot=%.3f deg, s=%.4f, tx=%.2f, ty=%.2f, dist=%.2f",
             cc, theta * 180.0 / CV_PI, s, tx, ty, transDist);

        // [SPEC_02 §2.4.2 v23] 位移场 (中心+四角) 合理性 + 旋转 ≤ 5° + cc ≥ 0.60；
        // 注意: findTransformECC 返回的 W 满足 src(W·x) ≈ ref(x)，即 ref→src，必须取逆作为 src→ref 的 outH！
        if (cc >= 0.60 && std::abs(theta) < 0.087) {
            cv::Mat H_ref2src = (cv::Mat_<double>(3, 3) <<
                a00, a01, tx,
                a10, a11, ty,
                0.0, 0.0, 1.0);
            cv::Mat H_src2ref = H_ref2src.inv();
            if (displacementWithinLimit(H_src2ref, ref.rows, ref.cols, "alignFrameECC") &&
                verifyAlignment(src, ref, H_src2ref, "ECC")) {
                outH = H_src2ref;
                cv::warpPerspective(src, outWarped, outH, ref.size(),
                                    cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
                return true;
            }
        }
        return false;
    } catch (const std::exception& e) {
        LOGW("alignFrameECC exception: %s", e.what());
        return false;
    }
}

// ============================================================================
// 基于 Greg Ward 经典中值阈值位图的多曝光对齐 (AlignMTB)
// 专为跨大曝光级差的 HDR 连拍设计：
// 1. 中值阈值二值化将图像分割为亮部与暗部拓扑几何，完全免疫由于曝光变化导致的灰度剧变
// 2. 金字塔位图 XOR 与 popcount 极速搜索最佳刚体位移，单次仅耗时 5~10ms
// 3. 彻底杜绝特征点角点匮乏、大面积过曝或纯黑导致的漂移错位与环形光斑重影
// ============================================================================
bool BurstFusionEngine::alignFrameMTB(const cv::Mat& src, const cv::Mat& ref, cv::Mat& outWarped, cv::Mat& outH) {
    if (src.empty() || ref.empty() || src.size() != ref.size()) {
        return false;
    }

    try {
        cv::Mat graySrc, grayRef;
        if (src.channels() == 3) cv::cvtColor(src, graySrc, cv::COLOR_BGR2GRAY);
        else graySrc = src;
        if (ref.channels() == 3) cv::cvtColor(ref, grayRef, cv::COLOR_BGR2GRAY);
        else grayRef = ref;

        // max_bits=6 (支持最大 +/- 63 像素手抖偏移), exclude_range=4, cut=false
        cv::Ptr<cv::AlignMTB> aligner = cv::createAlignMTB(6, 4, false);
        cv::Point shift = aligner->calculateShift(grayRef, graySrc);
        LOGI("alignFrameMTB: computed robust exposure shift dx=%d, dy=%d", shift.x, shift.y);

        outH = (cv::Mat_<double>(3, 3) <<
            1.0, 0.0, static_cast<double>(shift.x),
            0.0, 1.0, static_cast<double>(shift.y),
            0.0, 0.0, 1.0);
        // [SPEC_02 §2.4.2 v23] 位移场合理性 + 结构校验 (取代固定 8px 上限)
        if (!displacementWithinLimit(outH, ref.rows, ref.cols, "alignFrameMTB") ||
            !verifyAlignment(src, ref, outH, "MTB")) {
            return false;
        }
        cv::warpPerspective(src, outWarped, outH, ref.size(),
                            cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        return true;
    } catch (const std::exception& e) {
        LOGW("alignFrameMTB exception: %s", e.what());
        return false;
    }
}

// ============================================================================
// 对数梯度域相位相关配准 (Gradient-Domain Phase Correlation)
// 专为跨曝光级差的 HDR 刚体配准设计：
// 1. Log(1 + |grad|) 将对数域乘性曝光系数转化为加性直流分量
// 2. 交叉功率谱相位白化过滤所有亮度差异，全局边缘相干对齐
// 3. 彻底杜绝特征点角点匮乏或大面积过曝导致的失配
// ============================================================================
bool BurstFusionEngine::alignGradientPhaseCorrelation(const cv::Mat& src, const cv::Mat& ref, cv::Mat& outWarped, cv::Mat& outH) {
    if (src.empty() || ref.empty() || src.size() != ref.size()) {
        return false;
    }

    int maxDim = std::max(ref.cols, ref.rows);
    float scale = (maxDim > 960) ? 960.0f / maxDim : 1.0f;

    cv::Mat graySrc, grayRef;
    if (src.channels() == 3) cv::cvtColor(src, graySrc, cv::COLOR_BGR2GRAY);
    else graySrc = src;

    if (ref.channels() == 3) cv::cvtColor(ref, grayRef, cv::COLOR_BGR2GRAY);
    else grayRef = ref;

    cv::Mat smallSrc, smallRef;
    if (scale < 1.0f) {
        cv::resize(graySrc, smallSrc, cv::Size(), scale, scale, cv::INTER_AREA);
        cv::resize(grayRef, smallRef, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        smallSrc = graySrc;
        smallRef = grayRef;
    }

    cv::Mat gxSrc, gySrc, magSrc;
    cv::Sobel(smallSrc, gxSrc, CV_32F, 1, 0, 3);
    cv::Sobel(smallSrc, gySrc, CV_32F, 0, 1, 3);
    cv::magnitude(gxSrc, gySrc, magSrc);

    cv::Mat gxRef, gyRef, magRef;
    cv::Sobel(smallRef, gxRef, CV_32F, 1, 0, 3);
    cv::Sobel(smallRef, gyRef, CV_32F, 0, 1, 3);
    cv::magnitude(gxRef, gyRef, magRef);

    magSrc += 1.0f;
    cv::log(magSrc, magSrc);
    magRef += 1.0f;
    cv::log(magRef, magRef);

    cv::Mat hanningWin;
    cv::createHanningWindow(hanningWin, smallRef.size(), CV_32F);

    double response = 0.0;
    cv::Point2d shift = cv::phaseCorrelate(magRef, magSrc, hanningWin, &response);

    float invScale = (scale < 1.0f) ? (1.0f / scale) : 1.0f;
    double dx = -shift.x * invScale;
    double dy = -shift.y * invScale;

    LOGI("alignGradientPhaseCorrelation: response=%.4f, dx=%.2f, dy=%.2f", response, dx, dy);

    // [SPEC_02 §2.4.2 v23] 响应度 >= 0.20 + 位移场合理性 + 结构校验 (取代固定 6px 上限)
    if (response >= 0.20) {
        cv::Mat Hpc = (cv::Mat_<double>(3, 3) <<
            1.0, 0.0, dx,
            0.0, 1.0, dy,
            0.0, 0.0, 1.0);
        if (displacementWithinLimit(Hpc, ref.rows, ref.cols, "alignGradientPhaseCorrelation") &&
            verifyAlignment(src, ref, Hpc, "PhaseCorr")) {
            outH = Hpc;
            cv::warpPerspective(src, outWarped, outH, ref.size(),
                                cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
            return true;
        }
    }

    return false;
}

// ============================================================================
// 高光多候选 ROI 归一化互相关模板精配准 (Multi-ROI NCC Template Match)
// 专门针对夜景灯芯、环形灯管、发光字：
// 1. 归一化互相关 TM_CCOEFF_NORMED 对仿射光度变化完全不变
// 2. 抛物线亚像素插值达到 0.05 像素精度
// 3. 彻底杜绝桌面漫反射拉偏质心导致的环形重影
// ============================================================================
bool BurstFusionEngine::alignHighlightTemplate(const cv::Mat& srcShort, const cv::Mat& refBase, cv::Mat& outWarped, cv::Mat& outH) {
    if (srcShort.empty() || refBase.empty() || srcShort.size() != refBase.size()) {
        return false;
    }

    cv::Mat grayShort, grayBase;
    if (srcShort.channels() == 3) cv::cvtColor(srcShort, grayShort, cv::COLOR_BGR2GRAY);
    else grayShort = srcShort;

    if (refBase.channels() == 3) cv::cvtColor(refBase, grayBase, cv::COLOR_BGR2GRAY);
    else grayBase = refBase;

    int rows = grayShort.rows;
    int cols = grayShort.cols;

    int maxDim = std::max(cols, rows);
    float scale = (maxDim > 1920) ? 1920.0f / maxDim : 1.0f;

    cv::Mat sShort, sBase;
    if (scale < 1.0f) {
        cv::resize(grayShort, sShort, cv::Size(), scale, scale, cv::INTER_AREA);
        cv::resize(grayBase, sBase, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        sShort = grayShort;
        sBase = grayBase;
    }

    int sRows = sShort.rows;
    int sCols = sShort.cols;

    cv::Mat blurShort;
    cv::GaussianBlur(sShort, blurShort, cv::Size(7, 7), 2.0);

    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(blurShort, &minVal, &maxVal, &minLoc, &maxLoc);

    if (maxVal < 15.0) {
        return false;
    }

    int R = std::min(70, std::min(sCols, sRows) / 10);
    int M = 45;

    // 1. 提取所有显著发光高光峰值候选 (Connected Components + Local Maxima)
    double threshVal = std::max(18.0, maxVal * 0.35);
    cv::Mat threshMask;
    cv::threshold(blurShort, threshMask, threshVal, 255.0, cv::THRESH_BINARY);
    threshMask.convertTo(threshMask, CV_8UC1);

    cv::Mat labels, stats, centroids;
    int nLabels = cv::connectedComponentsWithStats(threshMask, labels, stats, centroids);

    struct PeakCandidate {
        cv::Point pt;
        double maxVal;
        int area;
    };
    std::vector<PeakCandidate> candidates;

    for (int i = 1; i < nLabels; ++i) {
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < 15) continue;

        cv::Mat compMask = (labels == i);
        double cMin, cMax;
        cv::Point cMinLoc, cMaxLoc;
        cv::minMaxLoc(blurShort, &cMin, &cMax, &cMinLoc, &cMaxLoc, compMask);

        int px = cMaxLoc.x;
        int py = cMaxLoc.y;
        if (px - R - M >= 0 && px + R + M < sCols && py - R - M >= 0 && py + R + M < sRows) {
            candidates.push_back({cMaxLoc, cMax, area});
        }
    }

    // 按峰值亮度与面积降序排序
    std::sort(candidates.begin(), candidates.end(), [](const PeakCandidate& a, const PeakCandidate& b) {
        if (std::abs(a.maxVal - b.maxVal) > 5.0) return a.maxVal > b.maxVal;
        return a.area > b.area;
    });

    // 空间非极大值抑制 (NMS): 过滤相互重叠的临近峰值
    std::vector<PeakCandidate> selectedPeaks;
    for (const auto& p : candidates) {
        bool tooClose = false;
        for (const auto& sp : selectedPeaks) {
            double dist = std::hypot(p.pt.x - sp.pt.x, p.pt.y - sp.pt.y);
            if (dist < 2.0 * R) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) {
            selectedPeaks.push_back(p);
            if (selectedPeaks.size() >= 8) break; // 最多匹配 8 个显著发光光源
        }
    }

    // 若连通域未筛选出有效峰值，则使用全局最大值兜底
    if (selectedPeaks.empty()) {
        int px = maxLoc.x;
        int py = maxLoc.y;
        if (px - R - M >= 0 && px + R + M < sCols && py - R - M >= 0 && py + R + M < sRows) {
            selectedPeaks.push_back({maxLoc, maxVal, 100});
        } else {
            return false;
        }
    }

    // 2. 对每个独立发光光源执行 NCC 归一化互相关匹配与亚像素插值
    std::vector<cv::Point2f> srcPts;
    std::vector<cv::Point2f> dstPts;
    std::vector<double> matchScores;
    std::vector<cv::Point2d> displacements;

    for (const auto& peak : selectedPeaks) {
        int px = peak.pt.x;
        int py = peak.pt.y;

        cv::Rect templRect(px - R, py - R, 2 * R, 2 * R);
        cv::Rect searchRect(px - R - M, py - R - M, 2 * R + 2 * M, 2 * R + 2 * M);

        cv::Mat templ = sShort(templRect);
        cv::Mat searchRoi = sBase(searchRect);

        cv::Mat matchResult;
        cv::matchTemplate(searchRoi, templ, matchResult, cv::TM_CCOEFF_NORMED);

        double minV, maxV;
        cv::Point minL, maxL;
        cv::minMaxLoc(matchResult, &minV, &maxV, &minL, &maxL);

        if (maxV >= 0.35) {
            double intDx = static_cast<double>(maxL.x - M);
            double intDy = static_cast<double>(maxL.y - M);

            int mx = maxL.x;
            int my = maxL.y;
            double subDx = intDx;
            double subDy = intDy;

            if (mx > 0 && mx < matchResult.cols - 1) {
                double denom = matchResult.at<float>(my, mx - 1) - 2.0 * matchResult.at<float>(my, mx) + matchResult.at<float>(my, mx + 1);
                if (std::abs(denom) > 1e-5) {
                    subDx += (matchResult.at<float>(my, mx - 1) - matchResult.at<float>(my, mx + 1)) / (2.0 * denom);
                }
            }
            if (my > 0 && my < matchResult.rows - 1) {
                double denom = matchResult.at<float>(my - 1, mx) - 2.0 * matchResult.at<float>(my, mx) + matchResult.at<float>(my + 1, mx);
                if (std::abs(denom) > 1e-5) {
                    subDy += (matchResult.at<float>(my - 1, mx) - matchResult.at<float>(my + 1, mx)) / (2.0 * denom);
                }
            }

            srcPts.emplace_back(static_cast<float>(px), static_cast<float>(py));
            dstPts.emplace_back(static_cast<float>(px + subDx), static_cast<float>(py + subDy));
            matchScores.push_back(maxV);
            displacements.emplace_back(subDx, subDy);

            LOGI("alignHighlightTemplate: matched light peak at (%d,%d), maxVal=%.1f, score=%.4f, dx=%.2f, dy=%.2f",
                 px, py, peak.maxVal, maxV, subDx, subDy);
        }
    }

    if (srcPts.empty()) {
        return false;
    }

    float invScale = (scale < 1.0f) ? (1.0f / scale) : 1.0f;
    bool fitSuccess = false;

    // 3. 变换矩阵估计：多光源时估计 4 自由度刚体变换 (平移+旋转)，单光源时使用纯平移
    if (srcPts.size() >= 2) {
        cv::Mat inliers;
        cv::Mat M = cv::estimateAffinePartial2D(srcPts, dstPts, inliers, cv::RANSAC, 3.0);
        if (!M.empty() && M.rows == 2 && M.cols == 3) {
            double a00 = M.at<double>(0, 0);
            double a01 = M.at<double>(0, 1);
            double a10 = M.at<double>(1, 0);
            double a11 = M.at<double>(1, 1);
            double tx = M.at<double>(0, 2);
            double ty = M.at<double>(1, 2);

            double s = std::sqrt(a00 * a00 + a01 * a01);
            double theta = std::atan2(a10, a00);
            double transDist = std::hypot(tx, ty);

            double fullTx = tx * invScale;
            double fullTy = ty * invScale;
            // [SPEC_02 §2.4.2 v23] 物理自检: 旋转 ≤ 5°, 缩放 0.96~1.04, 位移场 (中心+四角) 在限幅内
            cv::Mat H_try = (cv::Mat_<double>(3, 3) <<
                a00, a01, fullTx,
                a10, a11, fullTy,
                0.0, 0.0, 1.0);
            if (s >= 0.96 && s <= 1.04 && std::abs(theta) < 0.087 &&
                displacementWithinLimit(H_try, refBase.rows, refBase.cols, "alignHighlightTemplate")) {
                outH = H_try;
                fitSuccess = true;
                LOGI("alignHighlightTemplate: fitted Multi-Peak Rigid Affine: s=%.4f, rot=%.3f deg, tx=%.2f, ty=%.2f",
                     s, theta * 180.0 / CV_PI, fullTx, fullTy);
            } else {
                LOGW("alignHighlightTemplate: rejected affine fit: s=%.4f, rot=%.3f deg, tx=%.2f, ty=%.2f",
                     s, theta * 180.0 / CV_PI, fullTx, fullTy);
            }
        }
    }

    if (!fitSuccess) {
        // 单光源或刚体拟合退化: 选择最高置信度的光源位移作为全局平移
        size_t bestIdx = 0;
        double bestScore = -1.0;
        for (size_t i = 0; i < matchScores.size(); ++i) {
            if (matchScores[i] > bestScore) {
                bestScore = matchScores[i];
                bestIdx = i;
            }
        }
        double fullDx = displacements[bestIdx].x * invScale;
        double fullDy = displacements[bestIdx].y * invScale;
        LOGI("alignHighlightTemplate: single/best-peak translation: dx=%.2f, dy=%.2f, score=%.4f",
             fullDx, fullDy, bestScore);

        if (bestScore >= 0.65 && std::hypot(fullDx, fullDy) <= 0.08 * std::max(refBase.rows, refBase.cols)) {
            outH = (cv::Mat_<double>(3, 3) <<
                1.0, 0.0, fullDx,
                0.0, 1.0, fullDy,
                0.0, 0.0, 1.0);
            fitSuccess = true;
        } else {
            LOGW("alignHighlightTemplate: rejected single-peak shift dx=%.2f, dy=%.2f, score=%.4f",
                 fullDx, fullDy, bestScore);
        }
    }

    if (fitSuccess) {
        cv::warpPerspective(srcShort, outWarped, outH, refBase.size(),
                            cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        return true;
    }

    return false;
}

// ============================================================================
// 暗光夜景与屏幕高动态多级自适应配准 (Highlight & Screen Multi-Strategy Alignment)
// 1. CLAHE ORB 单应性配准 (场景环境结构丰富时)
// 2. ECC 增强相关系数欧几里得配准 (屏幕摩尔纹/跨曝光阶差/纯净文字)
// 3. NCC Template 归一化互相关多ROI拟合 (点光源/环形灯管/发光字)
// 4. AlignMTB 中值阈值位图匹配 (全局大曝光反差)
// 5. 对数梯度相位相关 (低对比度平滑结构)
// 6. 坏板熔断保护 (返回 false，防止未对齐曝光板进入 MergeMertens 造成双重重影)
// ============================================================================
bool BurstFusionEngine::alignHighlightFrame(const cv::Mat& srcShort, const cv::Mat& refBase, cv::Mat& outWarped, cv::Mat& outH, double exposureGain) {
    if (srcShort.empty() || refBase.empty() || srcShort.size() != refBase.size()) {
        return false;
    }

    // Fit matching brightness/saturation first; keep the original short pixels.
    if (exposureGain > 1.25 && std::isfinite(exposureGain)) {
        cv::Mat normalized=fusion::normalizeExposure(srcShort,std::min(256.,exposureGain)), ignored;
        if (alignFrameECC(normalized,refBase,ignored,outH)) {
            cv::warpPerspective(srcShort,outWarped,outH,refBase.size(),cv::INTER_LINEAR,cv::BORDER_CONSTANT);
            LOGI("alignHighlightFrame: normalized ECC accepted gain=%.3f",exposureGain);
            return true;
        }
    }
    // 1. 优先尝试 CLAHE + ORB 单应性配准
    if (alignFrameHomography(srcShort, refBase, outWarped, outH)) {
        LOGI("alignHighlightFrame: successfully aligned via CLAHE ORB homography");
        return true;
    }

    // 2. 第二级：ECC 增强相关系数配准 (专克屏幕摩尔纹与跨曝光阶差)
    if (alignFrameECC(srcShort, refBase, outWarped, outH)) {
        LOGI("alignHighlightFrame: successfully aligned via ECC Euclidean");
        return true;
    }

    // 3. 第三级：基于发光核心归一化互相关模板精配准 (NCC Template Matching)
    if (alignHighlightTemplate(srcShort, refBase, outWarped, outH)) {
        LOGI("alignHighlightFrame: successfully aligned via Highlight NCC Template");
        return true;
    }

    // 4. 第四级：AlignMTB 中值阈值位图匹配
    if (alignFrameMTB(srcShort, refBase, outWarped, outH)) {
        LOGI("alignHighlightFrame: successfully aligned via AlignMTB");
        return true;
    }

    // 5. 第五级：对数梯度域全局相位相关配准 (Log-Gradient Phase Correlation)
    if (alignGradientPhaseCorrelation(srcShort, refBase, outWarped, outH)) {
        LOGI("alignHighlightFrame: successfully aligned via Gradient Phase Correlation");
        return true;
    }

    // 6. 终极保护：配准失败，返回 false，标记为未配准坏板
    LOGW("alignHighlightFrame: all alignment strategies failed -> plate marked as unaligned (Anti-Ghosting Guard)");
    outH = cv::Mat::eye(3, 3, CV_64F);
    outWarped = srcShort.clone();
    return false;
}
