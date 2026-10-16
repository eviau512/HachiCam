> [!NOTE]
> 本项目的作者完全通过 Vibe Coding 构建了本项目，且并不完全清楚这个 Agent 到底偷偷写了些什么代码。 ¯\\\_(ツ)\_/¯

> 当前契约、实现及限制见 [规格索引](specs/README.md)、[TODO](TODO.md) 和 [rc16 GPU 扩展报告](specs/reports/RC16_GPU_COMPUTE_EXPANSION_2026-10-16.zh-CN.md)。测试版构建入口：`scripts/build-test.ps1`。

<div align="center">

<img src="app/src/hachimi/res/mipmap-xxxhdpi/ic_launcher.png" width="128" height="128" style="border-radius: 28px;" alt="HachiCam Logo" />

# HachiCam 🐾

**纯 Vibe Coding 打造的纯本地离线 Android 文档扫描仪。**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Specs: CC0 1.0](https://img.shields.io/badge/Specs-CC0%201.0-lightgrey.svg)](specs/LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Android%2010%2B-green.svg)](https://developer.android.com)
[![Engine](https://img.shields.io/badge/Engine-OpenCV%204.10%20%2B%20C%2B%2B17-orange.svg)](app/src/main/cpp)
[![UI](https://img.shields.io/badge/UI-Jetpack%20Compose-purple.svg)](https://developer.android.com/jetpack/compose)

*无广告、零追踪、无需联网权限。100% 本地端侧处理。*

[English](README.md) | [简体中文](README_zh.md) | [下载最新安装包 (Releases)](https://github.com/eviau512/Hachimi-Scan/releases)

</div>

---

## 当前功能

- CameraX/Camera2Interop JPEG 拍摄、可用后摄视角/变焦、点击 AF/AE 与正向输出。相机请求依能力执行，不能保证 OEM 的实际处理。
- 默认 Full HDR/多帧拍摄，请求最多四张基准和三张短曝光。只有通过局部验证的观测才参与降噪、实验区域重建与高光恢复；尺寸与真实细节分开，不保证原生 50 MP。
- 测试版接入 Vulkan 支持图/ECC 提案实验，先核对数值和成本，有界资源并回退 CPU。release 保持 CPU，全 GPU 合成及手机加速尚未验收。
- 文档轮廓跟踪、手动四角/直线吸附、平面透视及比例校正。识别仍是启发式算法。
- 原图、魔法色彩、连续色调类纸三种滤镜，B&W/灰阶已替换。纸白、色彩和弱笔画仍需实机对照。
- 不可变原图、按编辑代次提交派生图、导出读取租约和有界页面检查点。重启恢复已保存页面；中断拍摄保留完整原图或明确失败，不自动重跑丢失的连拍/未提交编辑。
- JPEG 与栅格页面 PDF 导出，没有 OCR/矢量文字层。HachiCamTest 可一次导出最近最多四组配对原始连拍、最终图和阶段计时；release 不探 root、不收集机型标识或原始诊断组。

## 当前限制

灯具/屏幕 HDR、局部 JPEG 响应仍需改善。区域重建为实验功能，证据不足时保留原尺寸合成。测试版记录 RAW 可用性，但 RAW 捕获/合成、稠密配准、自动拍摄、书页展开及手指清理尚未完成。宿主回归不等于相机画质、手机等待/内存/热状态验收。

## 编译与检查

仓库固定 SDK 35、build-tools 36.0.0、NDK 30.0.16248370、Gradle 8.14。使用 JDK 17/21 与已安装 CMake；默认 3.22.1，可按当前环境指定 `-Pscanner.cmakeVersion=4.1.2`。

```powershell
.\scripts\build-test.ps1
# 已配置签名密钥时构建 release：
.\gradlew.bat :app:assembleHachimiRelease
```

测试包 `moe.hachimi.cam.test`，显示为 HachiCamTest；release 为 `moe.hachimi.cam`、HachiCam。Universal 测试包位于 `app/build/outputs/apk/hachimi/debug/`，包含 arm64-v8a、armeabi-v7a、x86_64。

CPU 图像检查使用 `scripts/test-quality-native.ps1`；实际宿主 Vulkan 内核/故障策略使用 `scripts/test-gpu-native.ps1`，详见 [原生检查说明](tests/native/README.md)。Android 单元检查、安装包签名/身份检查与实机测试分别验收。

## 规格与工作流

[英文权威规格](specs/README.md) 包含 SPEC_00 到 SPEC_06；中文报告说明证据，不替代契约。每项修改按分析问题 → 分析 spec → 调整 spec → 实现与验证推进。[AGENTS.md](AGENTS.md) 也规定提交即 commit 与 push，并核对远端。

## 许可证

- 源代码：[GNU GPLv3](LICENSE)。
- 规格：[CC0 1.0](specs/LICENSE)。
