#include "burst_fusion.h"
#include "curve_dewarper.h"
#include "edge_detector.h"
#include "perspective_corrector.h"
#include "regional_reconstruction.h"
#include "vulkan_compute_engine.h"
#include <android/bitmap.h>
#include <android/log.h>
#include <cstring>
#include <jni.h>
#include <opencv2/opencv.hpp>

#define TAG "HachiCam-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static jlongArray encodeFusionResult(JNIEnv *env, cv::Mat &fused, const BurstFusionEngine::Report &report) {
    jlongArray result = env->NewLongArray(42);
    if (!result)
        return nullptr;
    jlong values[42] = {reinterpret_cast<jlong>(new cv::Mat(std::move(fused))),
                        report.anchors,
                        report.highlights,
                        report.scale,
                        report.reference,
                        report.crop.x,
                        report.crop.y,
                        report.crop.width,
                        report.crop.height,
                        report.sourceSize.width,
                        report.sourceSize.height};
    static_assert(sizeof(double) == sizeof(jlong));
    for (int i = 0; i < 9; ++i)
        std::memcpy(&values[11 + i], &report.firstToReference.val[i], sizeof(double));
    values[20] = report.geometryKnown ? 1 : 0;
    std::memcpy(&values[21], &report.multiFrameFraction, sizeof(double));
    std::memcpy(&values[22], &report.meanContributors, sizeof(double));
    std::memcpy(&values[23], &report.recoveredFraction, sizeof(double));
    std::memcpy(&values[24], &report.clippedFraction, sizeof(double));
    values[25] = 1;
    values[26] = report.srRequested;
    values[27] = report.srDenied;
    values[28] = report.regionTiles;
    values[29] = report.qualifiedTiles;
    std::memcpy(&values[30], &report.meanSupport, sizeof(double));
    values[31] = report.centrePhases;
    std::memcpy(&values[32], &report.regionFraction, sizeof(double));
    std::memcpy(&values[33], &report.reconstructedFraction, sizeof(double));
    values[34] = fusion::RegionalPlan::tileSize;
    values[36] = fusion::RegionalPlan::minimumTiles;
    const double minArea = fusion::RegionalPlan::minimumArea,
                 minSupport = fusion::RegionalPlan::minimumSupport, phaseDet = .04;
    std::memcpy(&values[35], &minArea, sizeof(double));
    std::memcpy(&values[37], &minSupport, sizeof(double));
    std::memcpy(&values[38], &phaseDet, sizeof(double));
    values[39] = report.fittedTiles;
    std::memcpy(&values[40], &report.initialFitError, sizeof(double));
    std::memcpy(&values[41], &report.finalFitError, sizeof(double));
    env->SetLongArrayRegion(result, 0, 42, values);
    return result;
}

#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
void nativeFailure(JNIEnv *env, const char *kind, const char *message) {
    if (env->ExceptionCheck())
        return;
    auto type = env->FindClass(kind);
    if (type) {
        env->ThrowNew(type, message);
        env->DeleteLocalRef(type);
    }
}
template <class T, class F> T jniGuard(JNIEnv *env, F action) {
    try {
        if (env->ExceptionCheck())
            return T{};
        return action();
    } catch (const std::invalid_argument &e) {
        nativeFailure(env, "java/lang/IllegalArgumentException", e.what());
    } catch (const std::exception &e) {
        nativeFailure(env, "java/lang/IllegalStateException", e.what());
    } catch (...) {
        nativeFailure(env, "java/lang/IllegalStateException", "Native image operation failed");
    }
    return T{};
}
std::vector<cv::Point2f> readPoints(JNIEnv *env, jfloatArray array, int exact = 0) {
    if (!array)
        throw std::invalid_argument("Missing geometry");
    const int n = env->GetArrayLength(array);
    if (n < 4 || n > 8192 || n % 2 || (exact && n != exact * 2))
        throw std::invalid_argument("Invalid geometry length");
    std::vector<float> values(n);
    env->GetFloatArrayRegion(array, 0, n, values.data());
    if (env->ExceptionCheck())
        throw std::runtime_error("Cannot read geometry");
    std::vector<cv::Point2f> result;
    result.reserve(n / 2);
    for (int i = 0; i < n; i += 2) {
        if (!std::isfinite(values[i]) || !std::isfinite(values[i + 1]))
            throw std::invalid_argument("Nonfinite geometry");
        result.emplace_back(values[i], values[i + 1]);
    }
    return result;
}
void validImage(const cv::Mat &image) {
    if (image.empty() || image.depth() != CV_8U ||
        (image.channels() != 1 && image.channels() != 3 && image.channels() != 4) || image.total() > 56000000)
        throw std::invalid_argument("Invalid image shape or type");
}
void validGray(const cv::Mat &image) {
    validImage(image);
    if (image.channels() != 1)
        throw std::invalid_argument("Expected gray or edge image");
}
void finiteValues(std::initializer_list<float> values) {
    for (float value : values)
        if (!std::isfinite(value))
            throw std::invalid_argument("Nonfinite image parameter");
}
void validBurstMetadata(const std::vector<int> &tiers, const std::vector<double> &exposures) {
    for (size_t i = 0; i < tiers.size(); ++i)
        if (tiers[i] < 0 || tiers[i] > 2 || !std::isfinite(exposures[i]) || exposures[i] < 0)
            throw std::invalid_argument("Invalid exposure metadata");
}
struct BitmapLock {
    JNIEnv *env;
    jobject bitmap;
    void *pixels = nullptr;
    bool locked = false;
    BitmapLock(JNIEnv *e, jobject b) : env(e), bitmap(b) {
        if (AndroidBitmap_lockPixels(env, bitmap, &pixels) < 0)
            throw std::runtime_error("Cannot read bitmap pixels");
        locked = true;
        if (!pixels) {
            unlock();
            throw std::runtime_error("Missing bitmap pixels");
        }
    }
    void unlock() {
        if (locked) {
            AndroidBitmap_unlockPixels(env, bitmap);
            locked = false;
        }
    }
    ~BitmapLock() { unlock(); }
};
std::string readString(JNIEnv *env, jstring value) {
    const char *raw = env->GetStringUTFChars(value, nullptr);
    if (!raw)
        throw std::runtime_error("Cannot read source path");
    try {
        std::string copy(raw);
        env->ReleaseStringUTFChars(value, raw);
        return copy;
    } catch (...) {
        env->ReleaseStringUTFChars(value, raw);
        throw;
    }
}
} // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    LOGI("HachiCam Native Engine loaded: CPU fusion; experimental Vulkan pipeline inactive");
    return JNI_VERSION_1_6;
}

JNIEXPORT jfloatArray JNICALL Java_com_scanner_app_engine_NativeEdgeDetector_nativeDetectDocument(
    JNIEnv *env, jobject /* this */, jlong matAddr, jboolean curvedMode, jfloat touchX, jfloat touchY) {
    return jniGuard<jfloatArray>(env, [&]() -> jfloatArray {
        cv::Mat *grayFrame = reinterpret_cast<cv::Mat *>(matAddr);
        if (!grayFrame)
            return env->NewFloatArray(0);

        validImage(*grayFrame);
        finiteValues({touchX, touchY});
        EdgeDetector detector;
        DetectionResult result = detector.detectDocument(*grayFrame, curvedMode, touchX, touchY);

        std::vector<float> outData;
        outData.push_back(result.found ? 1.0f : 0.0f);
        outData.push_back(result.isCurved ? 1.0f : 0.0f);

        if (!result.isCurved) {
            outData.push_back(static_cast<float>(result.corners.size()));
            for (const auto &pt : result.corners) {
                outData.push_back(pt.x);
                outData.push_back(pt.y);
            }
        } else {
            outData.push_back(static_cast<float>(result.boundaryPoints.size()));
            for (const auto &pt : result.boundaryPoints) {
                outData.push_back(pt.x);
                outData.push_back(pt.y);
            }
        }

        // Tail metadata follows the variable-length point list, including misses.
        outData.insert(outData.end(), {result.partial ? 1.f : 0.f, float(result.method),
                                       float(result.candidates), result.sideSupport});
        jfloatArray retArray = env->NewFloatArray(outData.size());
        if (!retArray)
            return nullptr;
        env->SetFloatArrayRegion(retArray, 0, outData.size(), outData.data());
        return retArray;
    });
}

JNIEXPORT jlong JNICALL Java_com_scanner_app_engine_NativePerspective_nativeCorrectPerspective(
    JNIEnv *env, jobject /* this */, jlong srcMatAddr, jfloatArray corners, jfloat targetAspectRatio) {
    return jniGuard<jlong>(env, [&]() -> jlong {
        cv::Mat *src = reinterpret_cast<cv::Mat *>(srcMatAddr);
        if (!src)
            throw std::invalid_argument("Missing source image");
        validImage(*src);

        const auto srcCorners = readPoints(env, corners, 4);

        PerspectiveCorrector corrector;
        cv::Mat warped =
            corrector.correctPerspective(*src, srcCorners, static_cast<float>(targetAspectRatio));

        cv::Mat *result = new cv::Mat(warped);
        return reinterpret_cast<jlong>(result);
    });
}

JNIEXPORT jlong JNICALL Java_com_scanner_app_engine_NativePerspective_nativeProcessDocument(
    JNIEnv *env, jobject /* this */, jlong srcMatAddr, jfloatArray corners, jint filterType,
    jfloat targetAspectRatio) {
    return jniGuard<jlong>(env, [&]() -> jlong {
        cv::Mat *src = reinterpret_cast<cv::Mat *>(srcMatAddr);
        if (!src)
            throw std::invalid_argument("Missing source image");
        validImage(*src);

        const auto srcCorners = readPoints(env, corners, 4);

        if (filterType < 0 || filterType > 2)
            throw std::invalid_argument("Unknown filter");
        PerspectiveCorrector corrector;
        cv::Mat processed = corrector.processDocument(*src, srcCorners, static_cast<FilterType>(filterType),
                                                      static_cast<float>(targetAspectRatio));

        cv::Mat *result = new cv::Mat(processed);
        return reinterpret_cast<jlong>(result);
    });
}

JNIEXPORT jlong JNICALL Java_com_scanner_app_engine_NativePerspective_nativeApplyFilter(JNIEnv *env,
                                                                                        jobject /* this */,
                                                                                        jlong srcMatAddr,
                                                                                        jint filterType) {
    return jniGuard<jlong>(env, [&]() -> jlong {
        cv::Mat *src = reinterpret_cast<cv::Mat *>(srcMatAddr);
        if (!src)
            throw std::invalid_argument("Missing source image");
        validImage(*src);

        if (filterType < 0 || filterType > 2)
            throw std::invalid_argument("Unknown filter");
        PerspectiveCorrector corrector;
        cv::Mat out;
        switch (static_cast<FilterType>(filterType)) {
        case FilterType::MAGIC_COLOR:
            out = corrector.enhanceMagicColor(*src);
            break;
        case FilterType::PAPER:
            out = corrector.enhancePaper(*src);
            break;
        case FilterType::ORIGINAL:
        default:
            out = src->clone();
            break;
        }

        cv::Mat *result = new cv::Mat(out);
        return reinterpret_cast<jlong>(result);
    });
}

JNIEXPORT jlongArray JNICALL Java_com_scanner_app_engine_NativeBurstFusion_nativeFuseBurstFrames(
    JNIEnv *env, jobject /* this */, jlongArray matAddrs, jboolean removeGlare, jboolean isScreenMode,
    jboolean superResolution, jintArray tierArray, jdoubleArray exposureArray) {
    return jniGuard<jlongArray>(env, [&]() -> jlongArray {
        if (!matAddrs)
            return nullptr;

        jsize len = env->GetArrayLength(matAddrs);
        if (len == 0)
            return nullptr;
        if (!tierArray || !exposureArray || env->GetArrayLength(tierArray) != len ||
            env->GetArrayLength(exposureArray) != len) {
            env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"),
                          "Frame metadata length mismatch");
            return nullptr;
        }

        if (len > 7)
            throw std::invalid_argument("Too many burst frames");
        std::vector<jlong> addrs(len);
        env->GetLongArrayRegion(matAddrs, 0, len, addrs.data());
        if (env->ExceptionCheck())
            return nullptr;
        std::vector<cv::Mat> frames;
        frames.reserve(len);

        for (int i = 0; i < len; ++i) {
            cv::Mat *pMat = reinterpret_cast<cv::Mat *>(addrs[i]);
            if (pMat)
                validImage(*pMat);
            frames.push_back(pMat ? *pMat : cv::Mat());
        }

        std::vector<int> tiers(len);
        std::vector<double> exposures(len);
        env->GetIntArrayRegion(tierArray, 0, len, tiers.data());
        env->GetDoubleArrayRegion(exposureArray, 0, len, exposures.data());
        if (env->ExceptionCheck())
            return nullptr;
        validBurstMetadata(tiers, exposures);
        try {
            BurstFusionEngine engine;
            BurstFusionEngine::Report report;
            cv::Mat fused = engine.fuseBurstFrames(frames, removeGlare, isScreenMode, superResolution, tiers,
                                                   exposures, &report);
            return encodeFusionResult(env, fused, report);
        } catch (const std::exception &error) {
            env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), error.what());
            return nullptr;
        }
    });
}

JNIEXPORT jlongArray JNICALL Java_com_scanner_app_engine_NativeBurstFusion_nativeFuseBurstFiles(
    JNIEnv *env, jobject, jobjectArray pathArray, jintArray tierArray, jdoubleArray exposureArray) {
    return jniGuard<jlongArray>(env, [&]() -> jlongArray {
        if (!pathArray || !tierArray || !exposureArray)
            return nullptr;
        const jsize count = env->GetArrayLength(pathArray);
        if (count < 1 || count > 7 || env->GetArrayLength(tierArray) != count ||
            env->GetArrayLength(exposureArray) != count) {
            env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"),
                          "Stream metadata length mismatch");
            return nullptr;
        }
        std::vector<std::string> paths;
        paths.reserve(count);
        for (int i = 0; i < count; ++i) {
            auto path = static_cast<jstring>(env->GetObjectArrayElement(pathArray, i));
            if (!path) {
                env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Missing source file");
                return nullptr;
            }
            paths.emplace_back(readString(env, path));
            env->DeleteLocalRef(path);
        }
        std::vector<int> tiers(count);
        std::vector<double> exposures(count);
        env->GetIntArrayRegion(tierArray, 0, count, tiers.data());
        env->GetDoubleArrayRegion(exposureArray, 0, count, exposures.data());
        if (env->ExceptionCheck())
            return nullptr;
        validBurstMetadata(tiers, exposures);
        try {
            BurstFusionEngine engine;
            BurstFusionEngine::Report report;
            auto fused = engine.fuseBurstStream(
                count,
                [&](size_t i) {
                    auto image = cv::imread(paths[i], cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
                    if (!image.empty())
                        validImage(image);
                    return image;
                },
                tiers, exposures, &report);
            return encodeFusionResult(env, fused, report);
        } catch (const std::exception &error) {
            env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), error.what());
            return nullptr;
        }
    });
}

JNIEXPORT jlong JNICALL Java_com_scanner_app_engine_NativeCurveDewarper_nativeDewarpCurved(
    JNIEnv *env, jobject /* this */, jlong srcMatAddr, jfloatArray topPts, jfloatArray bottomPts,
    jfloatArray leftPts, jfloatArray rightPts) {
    return jniGuard<jlong>(env, [&]() -> jlong {
        cv::Mat *src = reinterpret_cast<cv::Mat *>(srcMatAddr);
        if (!src)
            throw std::invalid_argument("Missing source image");
        validImage(*src);

        auto getPoints = [&](jfloatArray arr) { return readPoints(env, arr); };

        std::vector<cv::Point2f> top = getPoints(topPts);
        std::vector<cv::Point2f> bottom = getPoints(bottomPts);
        std::vector<cv::Point2f> left = getPoints(leftPts);
        std::vector<cv::Point2f> right = getPoints(rightPts);

        CurveDewarper dewarper;
        cv::Mat dewarped = dewarper.dewarpCurved(*src, top, bottom, left, right);

        cv::Mat *result = new cv::Mat(dewarped);
        return reinterpret_cast<jlong>(result);
    });
}

JNIEXPORT jfloatArray JNICALL Java_com_scanner_app_engine_NativeEdgeDetector_nativeFindSnapPoint(
    JNIEnv *env, jobject /* this */, jlong edgeMatAddr, jfloat touchX, jfloat touchY, jfloat radius) {
    return jniGuard<jfloatArray>(env, [&]() -> jfloatArray {
        cv::Mat *edges = reinterpret_cast<cv::Mat *>(edgeMatAddr);
        if (!edges)
            return env->NewFloatArray(0);

        validGray(*edges);
        finiteValues({touchX, touchY, radius});
        if (radius < 0 || radius > std::max(edges->cols, edges->rows))
            throw std::invalid_argument("Invalid snap radius");

        EdgeDetector detector;
        std::vector<cv::Point> pts =
            detector.findMagneticSnapPoint(*edges, cv::Point2f(touchX, touchY), radius);

        std::vector<float> outData;
        if (!pts.empty()) {
            outData.push_back(1.0f); // found
            outData.push_back(static_cast<float>(pts[0].x));
            outData.push_back(static_cast<float>(pts[0].y));
        } else {
            outData.push_back(0.0f); // not found
            outData.push_back(0.0f);
            outData.push_back(0.0f);
        }

        jfloatArray retArray = env->NewFloatArray(outData.size());
        if (!retArray)
            return nullptr;
        env->SetFloatArrayRegion(retArray, 0, outData.size(), outData.data());
        return retArray;
    });
}

JNIEXPORT jfloat JNICALL Java_com_scanner_app_engine_NativeEdgeDetector_nativeFindLineOffset(
    JNIEnv *env, jobject /* this */, jlong edgeMatAddr, jfloat p1x, jfloat p1y, jfloat p2x, jfloat p2y,
    jfloat maxOffset) {
    return jniGuard<jfloat>(env, [&]() -> jfloat {
        cv::Mat *edgeMat = reinterpret_cast<cv::Mat *>(edgeMatAddr);
        if (!edgeMat || edgeMat->empty()) {
            return std::numeric_limits<float>::quiet_NaN();
        }

        validGray(*edgeMat);
        finiteValues({p1x, p1y, p2x, p2y, maxOffset});
        if (maxOffset < 0 || maxOffset > std::max(edgeMat->cols, edgeMat->rows))
            throw std::invalid_argument("Invalid line search limit");

        EdgeDetector detector;
        return detector.findMagneticLineOffset(*edgeMat, cv::Point2f(p1x, p1y), cv::Point2f(p2x, p2y),
                                               maxOffset);
    });
}

JNIEXPORT jfloatArray JNICALL Java_com_scanner_app_engine_NativeEdgeDetector_nativeFindContourAtPoint(
    JNIEnv *env, jobject /* this */, jlong grayMatAddr, jfloat touchX, jfloat touchY) {
    return jniGuard<jfloatArray>(env, [&]() -> jfloatArray {
        cv::Mat *grayMat = reinterpret_cast<cv::Mat *>(grayMatAddr);
        if (!grayMat || grayMat->empty()) {
            std::vector<float> emptyData = {0.0f};
            jfloatArray retArray = env->NewFloatArray(1);
            if (!retArray)
                return nullptr;
            env->SetFloatArrayRegion(retArray, 0, 1, emptyData.data());
            return retArray;
        }

        validGray(*grayMat);
        finiteValues({touchX, touchY});
        EdgeDetector detector;
        std::vector<cv::Point2f> corners = detector.findContourAtPoint(*grayMat, touchX, touchY);

        if (corners.size() == 4) {
            std::vector<float> outData(9);
            outData[0] = 1.0f; // found
            for (int i = 0; i < 4; ++i) {
                outData[1 + 2 * i] = corners[i].x;
                outData[2 + 2 * i] = corners[i].y;
            }
            jfloatArray retArray = env->NewFloatArray(9);
            if (!retArray)
                return nullptr;
            env->SetFloatArrayRegion(retArray, 0, 9, outData.data());
            return retArray;
        } else {
            std::vector<float> emptyData = {0.0f};
            jfloatArray retArray = env->NewFloatArray(1);
            if (!retArray)
                return nullptr;
            env->SetFloatArrayRegion(retArray, 0, 1, emptyData.data());
            return retArray;
        }
    });
}

JNIEXPORT jobjectArray JNICALL Java_com_scanner_app_engine_NativeEdgeDetector_nativeDetectStructuralLines(
    JNIEnv *env, jobject /* this */, jobject bitmap, jfloat origWidth, jfloat origHeight) {
    return jniGuard<jobjectArray>(env, [&]() -> jobjectArray {
        jclass floatArrayClass = env->FindClass("[F");
        if (!floatArrayClass)
            return nullptr;
        if (!bitmap) {
            jobjectArray result = env->NewObjectArray(2, floatArrayClass, nullptr);
            if (!result)
                return nullptr;
            jfloatArray emptyH = env->NewFloatArray(0);
            if (!emptyH)
                return nullptr;
            jfloatArray emptyV = env->NewFloatArray(0);
            if (!emptyV)
                return nullptr;
            env->SetObjectArrayElement(result, 0, emptyH);
            env->SetObjectArrayElement(result, 1, emptyV);
            return result;
        }

        AndroidBitmapInfo info;
        if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) {
            jobjectArray result = env->NewObjectArray(2, floatArrayClass, nullptr);
            if (!result)
                return nullptr;
            jfloatArray emptyH = env->NewFloatArray(0);
            if (!emptyH)
                return nullptr;
            jfloatArray emptyV = env->NewFloatArray(0);
            if (!emptyV)
                return nullptr;
            env->SetObjectArrayElement(result, 0, emptyH);
            env->SetObjectArrayElement(result, 1, emptyV);
            return result;
        }

        if (!info.width || !info.height || uint64_t(info.width) * info.height > 56000000)
            throw std::invalid_argument("Invalid bitmap dimensions");
        BitmapLock bitmapLock(env, bitmap);
        void *pixels = bitmapLock.pixels;

        cv::Mat gray;
        if (info.format == ANDROID_BITMAP_FORMAT_RGBA_8888) {
            cv::Mat rgba(info.height, info.width, CV_8UC4, pixels, info.stride);
            cv::cvtColor(rgba, gray, cv::COLOR_RGBA2GRAY);
        } else if (info.format == ANDROID_BITMAP_FORMAT_RGB_565) {
            cv::Mat rgb565(info.height, info.width, CV_8UC2, pixels, info.stride);
            cv::cvtColor(rgb565, gray, cv::COLOR_BGR5652GRAY);
        } else if (info.format == ANDROID_BITMAP_FORMAT_A_8) {
            cv::Mat a8(info.height, info.width, CV_8UC1, pixels, info.stride);
            gray = a8.clone();
        }

        bitmapLock.unlock();

        if (gray.empty()) {
            jobjectArray result = env->NewObjectArray(2, floatArrayClass, nullptr);
            if (!result)
                return nullptr;
            jfloatArray emptyH = env->NewFloatArray(0);
            if (!emptyH)
                return nullptr;
            jfloatArray emptyV = env->NewFloatArray(0);
            if (!emptyV)
                return nullptr;
            env->SetObjectArrayElement(result, 0, emptyH);
            env->SetObjectArrayElement(result, 1, emptyV);
            return result;
        }

        float effectiveW = (origWidth > 0.0f) ? origWidth : static_cast<float>(info.width);
        float effectiveH = (origHeight > 0.0f) ? origHeight : static_cast<float>(info.height);
        finiteValues({origWidth, origHeight});
        if (origWidth < 0 || origHeight < 0 || double(effectiveW) * effectiveH > 56000000)
            throw std::invalid_argument("Invalid structural source size");

        EdgeDetector detector;
        EdgeDetector::StructuralLinesResult linesResult =
            detector.detectStructuralLines(gray, effectiveW, effectiveH);

        jobjectArray result = env->NewObjectArray(2, floatArrayClass, nullptr);
        if (!result)
            return nullptr;

        jfloatArray hArray = env->NewFloatArray(linesResult.horizontalLines.size());
        if (!hArray)
            return nullptr;
        if (!linesResult.horizontalLines.empty()) {
            env->SetFloatArrayRegion(hArray, 0, linesResult.horizontalLines.size(),
                                     linesResult.horizontalLines.data());
        }
        env->SetObjectArrayElement(result, 0, hArray);

        jfloatArray vArray = env->NewFloatArray(linesResult.verticalLines.size());
        if (!vArray)
            return nullptr;
        if (!linesResult.verticalLines.empty()) {
            env->SetFloatArrayRegion(vArray, 0, linesResult.verticalLines.size(),
                                     linesResult.verticalLines.data());
        }
        env->SetObjectArrayElement(result, 1, vArray);

        return result;
    });
}

} // extern "C"
