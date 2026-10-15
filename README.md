> [!NOTE]
> The owner of this repo built this entirely via vibe coding, and isn't quite sure what this Agent actually does. ¯\\\_(ツ)\_/¯

> Current contracts and implementation limits: [specifications](specs/README.md), [TODO](TODO.md) and [rc14 performance report](specs/reports/RC14_REGISTRATION_PERFORMANCE_2026-10-15.zh-CN.md). Build the test APK with `scripts/build-test.ps1`.

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

## Current features

- CameraX/Camera2Interop JPEG capture, supported back views/zoom, tap AF/AE and upright output. Camera requests are capability dependent, not guaranteed OEM behavior.
- Full HDR/multi-frame capture enabled by default: requests up to four base and three shorter frames. Only locally supported observations contribute to denoising, experimental regional reconstruction or highlight recovery. Output size and recovered detail are separate; native 50 MP is not guaranteed.
- Debug-only experimental Vulkan support maps/ECC proposals with numerical and cost calibration, bounded resources and CPU fallback. Release stays CPU. Full GPU fusion and phone speedup are not yet accepted.
- Tracked document outlines, manual four-corner correction, line snapping and planar perspective/aspect correction. Detection remains heuristic.
- Three filters: Original, Magic Color and continuous-tone Paper. B&W/Grayscale have been replaced; paper/color/weak-stroke quality still needs device comparisons.
- Immutable masters, generation-checked derivative commits, export read leases and bounded committed-page checkpoints. Restart restores saved pages; interrupted captures fail explicitly or retain a complete master. Pending bursts and unsaved edits are not replayed.
- JPEG and raster-page PDF export; no OCR or vector text layer. HachiCamTest diagnostics can export up to four matching completed burst groups with final images and phase timings. Release does not probe root or collect device identifiers/raw diagnostic groups.

## Current limits

Lamp/screen HDR and scene-specific JPEG response remain active quality work. Regional reconstruction is experimental and can retain the original output size when evidence is insufficient. RAW availability is recorded in test builds, but RAW capture/fusion, dense alignment, automatic capture, book flattening and finger cleanup are not completed features. Host regressions are distinct from real-camera/thermal/latency acceptance.

## Build and checks

The repository pins SDK 35, build-tools 36.0.0, NDK 30.0.16248370 and Gradle 8.14. Use JDK 17/21 and installed CMake; the wrapper defaults to 3.22.1 and supports `-Pscanner.cmakeVersion=4.1.2` for the configured workspace.

```powershell
.\scripts\build-test.ps1
# Release, with a configured signing key:
.\gradlew.bat :app:assembleHachimiRelease
```

The test package is `moe.hachimi.cam.test`, visibly HachiCamTest; release is `moe.hachimi.cam`, HachiCam. Universal debug output is under `app/build/outputs/apk/hachimi/debug/` and contains arm64-v8a, armeabi-v7a and x86_64.

`scripts/test-quality-native.ps1` runs CPU image fixtures; `scripts/test-gpu-native.ps1` exercises actual host Vulkan kernels and failure policy. See [native test instructions](tests/native/README.md). Android unit checks, packaged identity/signature checks and device tests serve different purposes.

## Specifications and workflow

The [English authoritative index](specs/README.md) links SPEC_00 through SPEC_06. Chinese reports explain evidence and do not replace the contracts. Every change follows problem analysis → spec analysis → spec revision → implementation and verification; [AGENTS.md](AGENTS.md) also defines commit-and-push handoff.

## License

- Source: [GNU GPLv3](LICENSE).
- Specifications: [CC0 1.0](specs/LICENSE).
