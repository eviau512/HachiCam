cmake_minimum_required(VERSION 3.22.1)
project("doc_scanner_engine")

set(CMAKE_CXX_STANDARD 17)

# OpenCV is provided via the org.opencv:opencv AAR (Maven) using Prefab.
# With buildFeatures.prefab = true in build.gradle.kts, AGP exports the
# AAR's native headers+libs as a CMake config package named "opencv".
find_package(OpenCV REQUIRED CONFIG)

add_library(
        doc_scanner_engine
        SHARED
        edge_detector.cpp
        document_boundary.cpp
        perspective_corrector.cpp
        curve_dewarper.cpp
        burst_fusion.cpp
        fusion_pipeline.cpp
        stream_fusion.cpp
        local_registration.cpp
        gpu_local_support.cpp
        regional_reconstruction.cpp
        regional_refinement.cpp
        vulkan_compute_engine.cpp
        jni_bridge.cpp
)

find_library(vulkan-lib vulkan)
find_library(dl-lib dl)

target_link_libraries(
        doc_scanner_engine
        log
        jnigraphics
        OpenCV::opencv_java4
        ${vulkan-lib}
        ${dl-lib}
)

option(HACHICAM_GPU_EXPERIMENTAL "Enable validated experimental support/ECC compute backend" OFF)
if(HACHICAM_GPU_EXPERIMENTAL)
    target_compile_definitions(doc_scanner_engine PRIVATE HACHICAM_VULKAN_SUPPORT=1)
endif()

target_compile_options(doc_scanner_engine PRIVATE -O3 -fopenmp)
target_link_options(doc_scanner_engine PRIVATE -fopenmp -static-openmp)
