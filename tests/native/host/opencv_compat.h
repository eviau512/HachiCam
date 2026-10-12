#pragma once
#include <opencv2/core/version.hpp>
#if CV_VERSION_MAJOR >= 5
// OpenCV 5 split these declarations out of imgproc/calib3d. Host testing only;
// Android continues to build and ship OpenCV 4.10 through its Maven AAR.
#include <opencv2/geometry.hpp>
#endif
