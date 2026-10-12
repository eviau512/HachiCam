> [!NOTE]
> The owner of this repo built this entirely via vibe coding, and isn't quite sure what this Agent actually does. ¯\\\_(ツ)\_/¯

> See the [2026-10-12 implementation audit](specs/IMPLEMENTATION_AUDIT_2026-10-12.md) for verified behavior and missing features. Historical fixed 50MP, GPU/OEM parity and vector PDF claims below are not acceptance results. Build the test APK with `scripts/build-test.ps1`.

<div align="center">

<img src="app/src/hachimi/res/mipmap-xxxhdpi/ic_launcher.png" width="128" height="128" style="border-radius: 28px;" alt="HachiCam Logo" />

# HachiCam 🐾

**A vibe-coded, fully local Android document scanner.**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Specs: CC0 1.0](https://img.shields.io/badge/Specs-CC0%201.0-lightgrey.svg)](specs/LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Android%2010%2B-green.svg)](https://developer.android.com)
[![Engine](https://img.shields.io/badge/Engine-OpenCV%204.10%20%2B%20C%2B%2B17-orange.svg)](app/src/main/cpp)
[![UI](https://img.shields.io/badge/UI-Jetpack%20Compose-purple.svg)](https://developer.android.com/jetpack/compose)

*Zero ads, zero telemetry, zero network permissions. 100% on-device processing.*

[English](README.md) | [简体中文](README_zh.md) | [Download Releases](https://github.com/eviau512/Hachimi-Scan/releases)

</div>

---

## 🌟 Key Features

### 1. Camera & Computational Photography
* **Hardware ISP Tuning**:
  - Leverages Camera2Interop to inject high-quality capture parameters, ensuring hardware OIS and optimal tone mapping.
* **50MP Multi-Frame Super-Resolution Burst (Experimental)**:
  - Capitalizes on involuntary physiological hand tremor ($0.1 \sim 0.8\text{ px}$ subpixel displacement) across rapid frames, utilizing ORB + RANSAC homography to accumulate polyphase samples onto a $2\times$ canvas ($8160 \times 6144 \approx 50.1\text{MP}$), reconstructing high-frequency optical detail beyond single-frame Nyquist limits.
* **Orientation Awareness & Dynamic UI Rotation**:
  - Fully adaptive rotation across viewfinder controls, automatically normalizing physical pixel orientation and EXIF headers for convenient landscape shooting of wide banners and station signs.

### 2. Edge Detection & Perspective Correction
* **Text Saliency & Wide Aspect Ratio Support**:
  - Integrates a Sobel gradient integral image to rapidly assess internal stroke energy density (Text Saliency), prioritizing text-bearing signs over empty, reflective glass doors in challenging environments.
  - Accommodates wide aspect ratios up to $12:1$ for subway route signs, banners, and long receipts.
* **Interactive Quad Cropping with 2.8x Loupe**:
  - Floating crosshair magnifying glass displays during corner adjustments for pixel-accurate positioning.
  - Quick aspect presets: A4, A3, 4:3, 16:9, 8:7, and freeform Custom, with automatic foreshortening recovery.

### 3. Document Enhancement Filters
* **Retinex Magic Color**:
  - Isolates the L channel in CIE-Lab color space using low-frequency illumination division to neutralize cast shadows and paper creases, paired with dynamic white balancing to preserve colorful highlighter marks and official stamps.
* **Sauvola Adaptive Binarization (B&W Document)**:
  - Accelerated by double-precision integral images for $O(1)$ local dynamic thresholding, removing photocopy background gray without breaking delicate letterforms.
* **Smooth Grayscale**:
  - Balanced contrast stretching tailored for ID cards, pencil sketches, and invoices.

### 4. Privacy & Document Export
* **100% On-Device**: All C++ NDK and OpenCV computations execute strictly locally.
* **Zero Telemetry**: No user accounts, zero analytics or advertising SDKs.
* **Vector PDF & Image Export**: Compiles clean multi-page PDFs using Android's native `PdfDocument` alongside batch JPEG exports.

---

## ⚠️ Known Issues & Technical Debt

This project was rapidly prototyped through human-AI pair programming. There are recognized architectural compromises and technical debt in the current codebase that are slated for refactoring:

1. **File I/O Serialization Overhead**:
   - The current multi-frame pipeline writes burst frames to temporary JPEGs on disk, decodes them back in C++ via `imread`, processes them, and writes them back. This introduces unnecessary disk wear and re-compression latency. We plan to transition to a zero-copy memory buffer pipeline using `ImageProxy` / `HardwareBuffer`.
2. **Monolithic UI & ViewModel Components**:
   - Files like `CameraScreen` and `CameraViewModel` currently manage multiple concurrent responsibilities (sensor monitoring, CameraX lifecycle, UI animations, and capture coordination). These will be decoupled into single-responsibility UseCases and Controllers.
   - An in-memory StateFlow repository is currently used; a persistent SQLite/Room database is needed to protect unexported batches against process death.
3. **JNI Interface Refinement**:
   - Several JNI methods pass raw pointer handles (`jlong matAddr`) and flat arrays without comprehensive RAII memory management wrappers.

We welcome constructive critiques, issue reports, and community pull requests.

---

## 🛠️ Build & Run

### Prerequisites
* Android Studio Ladybug (2024.2.1) or newer
* Android SDK 35 (Android 15)
* Android NDK 26+ / 30
* JDK 17 or JDK 21
* CMake 3.22.1+

### Build from Command Line

```bash
# Clone the repository
git clone https://github.com/eviau512/Hachimi-Scan.git
cd Hachimi-Scan

# Build HachiCam Release APK
./gradlew assembleHachimiRelease

# Or build standard flavor Release APK
./gradlew assembleStandardRelease
```

Generated APKs are located in `app/build/outputs/apk/hachimi/release/`.

---

## 📄 Technical Specifications

All core algorithms are documented with detailed mathematical models under [`specs/`](specs/) (dedicated to public domain under CC0 1.0):
* [`SPEC_00_ARCHITECTURE_AND_TECH_STACK.md`](specs/SPEC_00_ARCHITECTURE_AND_TECH_STACK.md): System architecture, pipelines, and tech stack overview
* [`SPEC_01_RETINEX_MAGIC_COLOR_ENGINE.md`](specs/SPEC_01_RETINEX_MAGIC_COLOR_ENGINE.md): Retinex CIE-Lab illumination division model
* [`SPEC_02_ADAPTIVE_BINARIZATION_ENGINE.md`](specs/SPEC_02_ADAPTIVE_BINARIZATION_ENGINE.md): Sauvola integral image adaptive binarization
* [`SPEC_04_BURST_FUSION_ENGINE.md`](specs/SPEC_04_BURST_FUSION_ENGINE.md): Multi-frame alignment and 50MP subpixel super-resolution
* [`SPEC_15_METRO_SIGN_AND_TEXT_SALIENCY_DETECTION.md`](specs/SPEC_15_METRO_SIGN_AND_TEXT_SALIENCY_DETECTION.md): Wide-aspect detection and text saliency scoring

---

## 📄 License

* **Source Code**: Licensed under the **[GNU General Public License v3 (GPLv3)](LICENSE)**.
* **Specifications**: Dedicated to the public domain under **[Creative Commons Zero v1.0 (CC0 1.0)](specs/LICENSE)**.
