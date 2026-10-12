> [!NOTE]
> 本项目的作者完全通过 Vibe Coding 构建了本项目，且并不完全清楚这个 Agent 到底偷偷写了些什么代码。 ¯\\\_(ツ)\_/¯

> 当前实现、缺陷修复及未实装功能以 [2026-10-12 实现审计](specs/IMPLEMENTATION_AUDIT_2026-10-12.md) 为准。下方历史功能介绍中的固定 50MP、GPU/OEM 对等和矢量 PDF 描述不应视为已验收承诺。测试版构建入口：`scripts/build-test.ps1`。

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

## 🌟 核心功能

### 1. 相机与计算摄影 (Camera & Computational Photography)
* **硬件级 ISP 调优**：
  - 通过 Camera2Interop 注入高画质捕获请求，启用传感器硬件级光学防抖（OIS）与精细色调映射。
* **多帧超分辨率连拍 (50MP Super-Res，实验性)**：
  - 针对手持拍摄时的生理微颤（$0.1 \sim 0.8\text{ px}$ 亚像素位移），通过 ORB 特征与 RANSAC 计算亚像素单应性矩阵，在 $2\times$ 物理画布上进行多相累加融合，重构超越单帧传感器采样的高频光学细节并抑制反光。
* **横竖屏自适应与方向感知**：
  - 取景界面与操作按钮支持全向动态旋转，拍摄时自动校正物理像素与 EXIF 朝向，便于横向拍摄长条形标牌与宽幅书籍。

### 2. 边缘检测与几何校正 (Edge Detection & Perspective)
* **文字显著度与长宽比放宽**：
  - 引入 Sobel 梯度积分图，对候选多边形内部的高频笔画能量（Text Saliency）进行快速评估，在复杂背景下优先锁定写有文字的标牌，避免误选反光玻璃窗。
  - 支持高达 $12:1$ 的长宽比识别，适配地铁站牌、条幅与收银小票。
* **交互式四边形裁剪与 2.8x 放大镜**：
  - 拖拽角点时唤起带十字准星的高清浮动放大镜，支持像素级精确定位。
  - 提供 A4、A3、4:3、16:9、8:7 及自定义比例快速切换，并内置俯仰角透视缩短补偿。

### 3. 图像增强滤镜 (Document Filters)
* **Retinex 魔法色彩 (Magic Color)**：
  - 在 CIE-Lab 色彩空间解耦 L 亮度通道，利用低频照度除法模型消除阴影与纸张折痕，结合动态白平衡还原纸白，完整保留彩色笔迹与公章。
* **Sauvola 局部自适应二值化 (B&W Document)**：
  - 基于双精度积分图快速计算局部动态阈值，在去除复印底灰的同时保护极细文字线条不断裂。
* **平滑灰度 (Grayscale)**：
  - 针对票据、证件复印件提供宽动态范围的对比度拉伸。

### 4. 纯净隐私与导出 (Privacy & Export)
* **100% 本地运算**：全链路 C++ NDK 与 OpenCV 算法在设备本地完成。
* **零追踪、零权限**：无账户系统、无网络通信、无第三方分析统计 SDK。
* **标准文档导出**：基于 Android 原生 `PdfDocument` 组装高清矢量 PDF，支持批量 JPEG 导出。

---

## ⚠️ 已知不足与技术债务 (Known Issues & Tech Debt)

客观而言，本项目由作者与 AI 结对进行快速原型探索，当前版本存在较为明显的工程短板，我们并不回避这些问题，并已列入后续重构计划：

1. **磁盘文件 I/O 往返开销**：
   - 目前连拍与多帧融合采用了“落盘 JPEG $\to$ C++ 解码 $\to$ 算法处理 $\to$ 重新编码”的保守机制，引入了不必要的磁盘读写延迟与有损重编损耗。未来计划重构成基于 `ImageProxy` / `HardwareBuffer` 的零拷贝内存共享管线。
2. **大单体组件待解耦**：
   - `CameraScreen` 与 `CameraViewModel` 承担了过多职责（传感器监听、CameraX 绑定、动效交互、捕获流水线），后续将逐步拆分为遵循单一职责原则的 UseCase 与 Controller。
3. **缺少持久化数据库支撑**：
   - 当前已扫描页面临时存储在内存 StateFlow 列表中，如未及时导出且遭遇系统杀后台（Process Death），存在批次丢失风险，需引入 Room SQLite 持久化支持。
4. **JNI 原生指针与类型安全**：
   - JNI 接口层存在较多裸指针地址（`jlong matAddr`）传递，有待补充更为规范的 RAII 资源安全封装。

欢迎大家提出宝贵批评，也期待社区有经验的开发者提交 PR 共同改进。

---

## 🛠️ 构建与运行 (Build & Run)

### 环境要求
* Android Studio Ladybug (2024.2.1) 或更高版本
* Android SDK 35 (Android 15)
* Android NDK 26+ / 30
* JDK 17 或 JDK 21
* CMake 3.22.1+

### 命令行编译

```bash
# 克隆仓库
git clone https://github.com/eviau512/Hachimi-Scan.git
cd Hachimi-Scan

# 构建 Release APK (HachiCam 定制版)
./gradlew assembleHachimiRelease

# 或构建标准版 Release APK
./gradlew assembleStandardRelease
```

生成安装包位于 `app/build/outputs/apk/hachimi/release/`。

---

## 📄 架构规格书 (Specifications)

本项目核心算法均配有详细的设计与数学规范，存放在 [`specs/`](specs/) 目录下（采用 CC0 1.0 公共领域贡献）：
* [`SPEC_00_ARCHITECTURE_AND_TECH_STACK.md`](specs/SPEC_00_ARCHITECTURE_AND_TECH_STACK.md)：系统整体分层架构、数据流与技术栈全景
* [`SPEC_01_RETINEX_MAGIC_COLOR_ENGINE.md`](specs/SPEC_01_RETINEX_MAGIC_COLOR_ENGINE.md)：Retinex CIE-Lab 照度分解数学模型
* [`SPEC_02_ADAPTIVE_BINARIZATION_ENGINE.md`](specs/SPEC_02_ADAPTIVE_BINARIZATION_ENGINE.md)：Sauvola 积分图局部自适应二值化
* [`SPEC_04_BURST_FUSION_ENGINE.md`](specs/SPEC_04_BURST_FUSION_ENGINE.md)：多帧配准融合与 50MP 亚像素超分辨率
* [`SPEC_15_METRO_SIGN_AND_TEXT_SALIENCY_DETECTION.md`](specs/SPEC_15_METRO_SIGN_AND_TEXT_SALIENCY_DETECTION.md)：大长宽比标牌识别与 Sobel 积分图笔画显著度打分

---

## 📄 开源许可证与声明

* **源代码**：遵循 **[GNU General Public License v3 (GPLv3)](LICENSE)**。
* **规范文档**：遵循 **[Creative Commons Zero v1.0 (CC0 1.0)](specs/LICENSE)**。
* **美术资产**：应用内部分图标及表情元素衍生自中文社区同人文化二创，开发者不对相关形象主张专有版权；若有版权异议请随时联系下架或替换。
