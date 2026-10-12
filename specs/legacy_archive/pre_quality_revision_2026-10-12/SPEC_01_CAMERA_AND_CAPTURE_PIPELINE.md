# Specification 01: Camera Subsystem & Capture Pipeline
## 技术规格书 01：相机子系统与拍摄管线规范

> **Document Status**: Authoritative Subsystem Specification  
> **Consolidates**: Legacy SPEC_11 (ISP Tuning), SPEC_14 (EXIF Provenance), and SPEC_16 (Full HDR Pipeline)  
> **Target Audience**: Camera Engineers & Runtime Developers  
> **Languages**: Primary: English / Secondary: Chinese (Bilingual Executive Summaries)

---

## 1. Subsystem Overview / 概述

The camera subsystem coordinates device optical sensors, hardware Image Signal Processors (ISP), Optical Image Stabilization (OIS), and user metering interactions. It exposes two deterministic capture modes:
1. **Normal Mode**: Single-shot low-latency capture with hardware ISP high-quality enhancement and OIS enabled.
2. **Full HDR Unified Mode**: A 7-frame multi-tier exposure pyramid burst (4 EV 0 + 2 EV -2.5 + 1 EV -6.0) capturing high-frequency scene radiance and micro-motion for 50MP super-resolution fusion, highlight recovery, and cinema-grade tone mapping.

---

## 2. Hardware ISP Configuration via Camera2Interop / 硬件 ISP 参数调优

CameraX binds high-level lifecycle use cases (`Preview`, `ImageCapture`, `ImageAnalysis`). To bypass generic vendor down-tuning, low-level Camera2 keys are explicitly injected via `Camera2Interop.Extender`:

```kotlin
val camera2Extender = Camera2Interop.Extender(imageCaptureBuilder)
camera2Extender.setCaptureRequestOption(CaptureRequest.EDGE_MODE, CaptureRequest.EDGE_MODE_HIGH_QUALITY)
camera2Extender.setCaptureRequestOption(CaptureRequest.NOISE_REDUCTION_MODE, CaptureRequest.NOISE_REDUCTION_MODE_HIGH_QUALITY)
camera2Extender.setCaptureRequestOption(CaptureRequest.HOT_PIXEL_MODE, CaptureRequest.HOT_PIXEL_MODE_HIGH_QUALITY)
camera2Extender.setCaptureRequestOption(CaptureRequest.COLOR_CORRECTION_MODE, CaptureRequest.COLOR_CORRECTION_MODE_HIGH_QUALITY)
camera2Extender.setCaptureRequestOption(CaptureRequest.SHADING_MODE, CaptureRequest.SHADING_MODE_HIGH_QUALITY)
camera2Extender.setCaptureRequestOption(CaptureRequest.DISTORTION_CORRECTION_MODE, CaptureRequest.DISTORTION_CORRECTION_MODE_HIGH_QUALITY)
camera2Extender.setCaptureRequestOption(CaptureRequest.COLOR_CORRECTION_ABERRATION_MODE, CaptureRequest.COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY)
camera2Extender.setCaptureRequestOption(CaptureRequest.TONEMAP_MODE, CaptureRequest.TONEMAP_MODE_HIGH_QUALITY)

// Optical stabilization enabled; Electronic video stabilization disabled to prevent warping artifacts
camera2Extender.setCaptureRequestOption(CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE, CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE_ON)
camera2Extender.setCaptureRequestOption(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE_OFF)
```

---

## 3. Tap-to-Focus & Auto-Exposure Metering (AF / AE) / 点按对焦与测光契约

When the user taps the viewfinder preview:
1. **Metering Point Transformation**:
   The tap offset $(x_{\text{view}}, y_{\text{view}})$ is projected into the sensor coordinate space using `PreviewView.meteringPointFactory.createPoint(x, y)`.
2. **Action Dispatch**:
   A `FocusMeteringAction` is submitted with flags `FLAG_AF or FLAG_AE`, setting an auto-cancel timeout of 3.0 seconds.
3. **Visual Feedback Overlay**:
   A $72 \times 72\,\text{dp}$ square focus ring (1.5 dp solid border with rounded corners) appears immediately at the touch coordinates, animating an entry scale from $1.2\times$ to $1.0\times$ and fading out after 1.5 seconds.

---

## 4. Viewfinder Stability Detection / 取景稳定性检测

### 4.1 Problem and superseded contract (2026-10-12)

The user's `screen-20261012-153541-1791790535386.mp4` shows brief `Steady`
followed by repeated `Hold Steady`, while the selected contour changes from an
inner table to the paper boundary. The exact per-frame analyzer measurements are
not present in the recording. Code inspection establishes that the rc1 state
uses the largest consecutive displacement of any detected corner in a five-frame
window, a `0.005 × diagonal` threshold and a 300 ms timer; it does **not** implement
the previous five-frame variance/3.5 px formula.

Both corner-based contracts confound contour selection and corner jitter with
camera motion. Smoothing the visible overlay cannot correct the separate raw
corner test. This section supersedes the variance/3.5 px contract and the rc1
corner-displacement implementation.

### 4.2 Scene-motion measurement

`isStable` describes motion of the observed scene, independently of whether a
document quad is found. Document detection, contour selection and IIR corner
smoothing must not reset this state.

1. Analyze upright preview luminance, with the long edge reduced to at most
   480 px. No captured-photo or user-identity data is needed.
2. Track up to 100 scene corners using pyramidal sparse Lucas–Kanade flow.
   Reject tracks with failed forward/backward status, forward error ≥30,
   round-trip error ≥1 px or coordinates outside the image.
3. Fit a partial affine transform with RANSAC (1 px reprojection threshold).
   Require at least 12 inliers, at least 55% of the initially selected features,
   and feature coverage spanning at least 15% of image width and height.
   Insufficient texture or unreliable fitting yields **unknown**, not zero motion.
4. Evaluate transform displacement on a 3×3 grid at normalized coordinates
   `{0.1, 0.5, 0.9}`. Let `d` be the largest displacement divided by the analysis
   image diagonal. Normalize by consecutive camera frame timestamps:
   `speed = d / elapsedSeconds`. Rotation and zoom therefore count as motion.
5. Keep only one reduced previous frame. Reset on orientation/size changes,
   non-monotonic timestamps, gaps over 250 ms, capture reset and analyzer closure.
   Release native resources on every path. Do not introduce a second unbounded
   analysis queue.
6. Isolate motion-estimation and document-detection errors: an exception in the
   detector cannot invalidate a reliable motion result, and a tracking exception
   cannot suppress document detection. Test builds may log normalized speed,
   confidence and state at most once per second; release omits this debug trace.

This is sparse preview motion estimation, not the unimplemented dense motion
compensation/RAW fusion in SPEC_02. It indicates visual steadiness, not proof of
physical handset immobility or a guarantee of blur-free photos.

### 4.3 State transition and initial tuning parameters

Thresholds below are initial experimental defaults, expressed in image diagonals
per second, rather than fixed pixels or frame counts alone:

| Situation | Required behavior |
| --- | --- |
| Reliable speed ≤0.04 | Accumulate quiet time, capped at 120 ms per sample. |
| Acquiring, speed >0.04 and <0.25 | Subtract elapsed time from quiet time, floored at zero; decrease observation count by one. |
| Acquiring | Enter `Steady` after ≥350 ms accumulated quiet time and ≥4 reliable quiet observations. |
| Already steady, speed >0.08 | Leave after ≥100 ms continuous movement; lower speed clears that movement timer. |
| Speed ≥0.25 | Leave immediately and clear acquisition history. |
| Unknown/unreliable sample | Hold an existing steady state for at most 150 ms since the last reliable sample; after that clear state/history. Unknown samples cannot acquire steadiness. |
| Timestamp gap >250 ms, orientation/size change or analyzer closure | Clear state immediately. |

`Hold Steady` remains guidance, never a shutter lock. HDR may wait up to 400 ms
for `isStable`, then proceeds even if the wait expires. That upper bound does not
guarantee the scene has become steady or that the complete burst takes 400 ms.

### 4.4 Acceptance and verification limits

- Repeatedly switching between inner/outer document contours, or losing the quad
  while textured scene content remains static, must not reset motion stability.
- Static textured content should acquire steadiness in roughly 0.35–0.8 s at
  ordinary analysis rates; brief uncertainty and modest jitter must not flicker
  the label. Sustained panning/rotation must clear it promptly.
- Featureless/occluded scenes, invalid values and stale frames must not keep a
  steady indication indefinitely. Timestamp normalization must work at different
  analysis rates. Unit regressions must cover these temporal state rules.
- Replay of the preview crop from the supplied recording may guide tuning, but
  contains compression and the old overlay; it cannot replace a real camera test.
- Sparse LK API reference: [OpenCV 4.10 tracking](https://docs.opencv.org/4.10.0/dc/d6b/group__video__track.html).

中文：稳定提示改为跟踪预览纹理运动，和文档框选择分开。检测框跳动不等于
手抖；短暂不确定允许缓冲，持续移动或失去可靠跟踪必须退出稳定。数值为待
更多真机校准的初始参数，不承诺绝对无手抖或照片无模糊。

---

## 5. Apple Deep Fusion / Smart HDR 7-Frame Unified Capture Pipeline / 7帧曝光金字塔连拍管线

### 5.1 The Frame Sequence & Exposure Contract
To achieve wide dynamic range and super-resolution without severe exposure cliffs or AE hunting oscillation, the pipeline implements an **Apple Deep Fusion / Smart HDR 7-frame capture pyramid** structured into 3 exposure tiers:

| Frame Range | Tier & Exposure | Camera Control Mode | Exposure Parameter Formula | Functional Purpose |
|:---:|:---:|:---:|:---|:---|
| **Frames 0–3** | **Tier 1 (EV 0 Base)** | Active AE Locked (No 3A reset) | Camera Auto-Exposure (`baseIso`, `baseExpSec`) | Sub-pixel super-resolution anchors, temporal denoise ($1/\sqrt{4}$ SNR boost), geometric reference. |
| **Frames 4–5** | **Tier 2 (EV -2.5 Mid)** | Manual (`CONTROL_AE_MODE_OFF`) | $\text{midIso} = \text{clamp}(\text{baseIso}/4,\ 100,\ 3200)$<br>$\text{midExpSec} = \text{clamp}(\text{baseExpSec}/3,\ 1/2000,\ 1/30)$ | Smooth midtone transition, prevents glare blowout, preserves room and wall illumination tones. |
| **Frame 6** | **Tier 3 (EV -6.0 Short)** | Manual (`CONTROL_AE_MODE_OFF`) | $\text{shortIso} = \text{clamp}(\text{midIso}/8,\ 100,\ 800)$<br>$\text{shortExpSec} = \text{clamp}(\text{midExpSec}/4,\ 1/4000,\ 1/250)$ | Extreme highlight recovery: lamp filaments, lightbulb markings, and specular un-saturation. |

### 5.2 7-Frame Exposure Sequence Diagram
```mermaid
sequenceDiagram
    participant VM as CameraViewModel
    participant C2 as Camera2CameraControl
    participant S as CMOS Sensor & ISP
    participant Storage as Cache Storage

    Note over VM,S: Tier 1: 4 Frames at EV 0 (Base AE Locked)
    VM->>S: 1. takeSinglePicture() x 4 (Frames 0, 1, 2, 3)
    S-->>Storage: Tier 1 JPEGs (Base ISO, Base Exp)
    VM->>VM: Inspect Frame 0 EXIF (baseIso, baseExpSec)

    Note over VM,S: Tier 2: 2 Frames at EV -2.5 (Midtone Transition)
    VM->>C2: setCaptureRequestOptions(midExpNanos, midIso)
    VM->>VM: delay(40ms) for sensor register latching
    VM->>S: 2. takeSinglePicture() x 2 (Frames 4, 5)
    S-->>Storage: Tier 2 JPEGs

    Note over VM,S: Tier 3: 1 Frame at EV -6.0 (Deep Highlight Recovery)
    VM->>C2: setCaptureRequestOptions(shortExpNanos, shortIso)
    VM->>VM: delay(40ms) for sensor register latching
    VM->>S: 3. takeSinglePicture() x 1 (Frame 6)
    S-->>Storage: Tier 3 JPEGs

    VM->>C2: clearCaptureRequestOptions() (Restores AE)
    VM->>VM: NativeBurstFusion.fuseBurstFrames(mats, isScreenMode=true, superResolution=true)
```

---

## 6. EXIF Provenance & Image Normalization / EXIF 旋转与防伪签名

### 6.1 Fast OpenCV SIMD Rotation
To prevent expensive intermediate Java `Bitmap` re-decoding and re-compression:
1. `ExifInterface` extracts orientation tag (`ORIENTATION_ROTATE_90/180/270`).
2. The fused 50MP native `Mat` is rotated in-place using OpenCV SIMD `Core.rotate`.
3. The rotated image is written directly to disk via `Imgcodecs.imwrite` with JPEG Quality 95.

### 6.2 Tamper-Evident Provenance Stamping
Every saved image is inscribed with hardware and pipeline metadata:
- **`TAG_SOFTWARE`**: `HachiCam v<versionName> (<gitCommit>) [<Mode>]` (e.g. `[Full HDR]`, `[Normal]`).
- **`TAG_USER_COMMENT`**: `HachiCam v<versionName> (Build: <gitCommit>, <buildTime>), Mode: <Mode>`.
- **`TAG_ORIENTATION`**: Normalized strictly to `ORIENTATION_NORMAL` (1).

---

## 7. [v24 REQUIRED] Asynchronous Capture, RAM-First Pending Frames & Storage Policy / 异步拍摄、内存优先待处理帧与存储策略

> Status: **specified, not yet implemented.** Motivated by user observations on v0.1.2-rc0 (Ace3V): (1) after a tap the shutter spinner stays on for a long time; (2) the app's "cache" (actually disk) grows large.

### 7.1 Current behaviour (code reading, `CameraViewModel.captureFullHdrPhoto`)
- `_isCapturing` is set at tap and cleared only after **all** of: 7 `takePicture` calls (each writes a full JPEG file into `context.cacheDir`), two `delay(40)` sensor latches, JPEG decode of all frames, native fusion (≈ 5–6 s for 50 MP in the observed log), JPEG encode (q95) and page creation. The spinner therefore covers capture **and** processing.
- Frames: 0.7–2.5 MB JPEG each; the temporary files are deleted after fusion, but the fused result (`<uuid>.jpg`, tens of MB at 50 MP) and single-shot captures are also written to `cacheDir` and are **never deleted** by the app (only `LogCollector` prunes logs). Android reports `cacheDir` as "cache", and it can be evicted by the system while pages still reference it.
- Frame requests are issued through CameraX `ImageCapture`; CameraX serialises them, so the inter-frame gap is ISP/JPEG-pipeline latency + file write, not an artificial delay (except the two 40 ms latches).

### 7.2 Capture phase vs processing phase
1. **Capture phase** = from tap until the last burst frame has been received into memory. Only this phase shows the busy indicator / disables the shutter (~400–600 ms). The moment the last frame is in memory: clear AE overrides (`clearManualCaptureOptions`), set `_isCapturing=false`, reset stability, return to live preview; the UI **stays in the camera viewfinder** (MotoCam parity) and immediately accepts the next tap.
2. **Processing phase** (decode → align → fuse → encode → page) runs on a background worker with FIFO ordering. The UI shows a lightweight indicator on the bottom-right gallery badge (thumbnail with progress ring or count badge) and **must not** block the camera viewfinder.
3. **Page Lifecycle & Strict Anti-Frame-0 Guarantee**:
   - A page is inserted into `PageRepository` at the end of the capture phase with `status = PageStatus.PROCESSING` and a temporary thumbnail from the first frame.
   - On completion of background fusion, the page is updated in place with `status = PageStatus.READY` pointing to the 50MP fused image in `filesDir/scans/`. If fusion fails, fall back to the reference frame and mark `READY`.
   - **CropScreen & ReviewScreen Observation**: If the user taps the gallery badge to open `CropScreen` or `ReviewScreen` while a page is still `PROCESSING`, the screen displays a progress indicator ("Processing HDR...") and reacts to the page becoming `READY`. Frame 0 is **never** loaded into memory, cropped, or exported as the final image before fusion finishes.
4. **Worker policy**: one fusion at a time (native memory: decoded 50 MP BGR is ≈ 150 MB per frame), FIFO queue. Maximum queued bursts = 3; beyond that the shutter shows "busy" briefly instead of letting memory grow.
5. The 400 ms stability wait of §4 is kept; it is part of the capture phase.

### 7.3 Frame interval ("~10 ms" request)
- The achievable interval is bounded below by the sensor exposure + readout of each frame. The observed Tier 1 exposures are 58 ms at ISO ≈ 18000 in dim scenes, so a 10 ms gap cannot be reached there; in bright scenes (exposure ≤ 10 ms) it may.
- Requirements (best-effort, no new artificial delay): (a) remove avoidable gaps — do not wait for file I/O between requests (frames are delivered in memory, see 7.4); issue the next request as soon as the previous *capture* (not save) completes; (b) set Tier 2/3 manual parameters while the Tier 1 requests are still in flight where CameraX allows it, and reduce the 40 ms latch to the minimum verified on device (log shows whether the first plate after the change actually has the requested exposure); (c) evaluate `CAPTURE_MODE_MINIMIZE_LATENCY` for burst frames against image quality; (d) if CameraX remains the limit, consider Camera2 interop `captureBurst` as a follow-up, not as part of this change.
- Log per frame: request time, image timestamp, exposure, ISO, and the gap to the previous frame, so the real interval can be measured.

### 7.4 RAM-first pending-frame store
1. `ImageCapture.takePicture(executor, OnImageCapturedCallback)` (no file output). On `onCaptureSuccess(image)`: copy the JPEG plane bytes (`image.planes[0].buffer`) into the store and `image.close()` immediately; extract EXIF (ISO, exposure) from the bytes for the log/tier calculation (`ExifInterface(InputStream)`), no temporary file.
2. Store holds **compressed JPEG bytes** (≈ 1–2.5 MB each, ≈ 10–15 MB per 7-frame burst), kept in **direct (off-Java-heap) buffers** or native memory to avoid pressuring the Java heap. Decoded Mats exist only inside the worker for the burst being processed.
3. **Budget**: pending-store capacity = min(96 MB, 1/8 of device memory class) (≈ 6–8 bursts). If a new burst would exceed the budget, the oldest not-yet-started burst is spilled to disk (see 7.5) rather than rejecting the capture.
4. Frames of a burst are tagged `burstId`, `index`, `tier`, `iso`, `exposureNs`, orientation (needed to fuse and to orient output).

### 7.5 Spill to disk only when needed
- Pending bursts are written to `filesDir/pending/<burstId>/` (frames + a small JSON manifest) **only** when: (a) `onStop()` of the camera activity occurs while any burst is unprocessed (the app is leaving the foreground; `onDestroy` and process death have no reliable callback, so `onStop` is the hook), or (b) the RAM budget is exceeded.
- Processing continues from RAM if the process stays alive; when a burst finishes, its `pending/<burstId>` directory (if any) is deleted.
- On next launch (and on `onStart`), scan `filesDir/pending/`; resume unfinished bursts through the same worker, creating `PROCESSING` pages for them. A manifest older than 7 days or with a missing frame is discarded.
- Optional follow-up (not required now): finish the current fusion in a short foreground service so that backgrounding does not stall it.

### 7.6 Disk policy
- Fused photos and single-shot photos live in **`filesDir/scans/`** (via `ImageStorage`), not in `cacheDir`, so the system cannot evict them while pages reference them. `cacheDir` is reserved for true temporaries (logs, thumbnails).
- Cleanup: (a) on app start delete orphaned files in `cacheDir` matching `hdr_*` / `*.jpg` that no page references (leftovers of earlier versions and crashes); (b) when a page is deleted, delete its original and thumbnail; (c) after a document is exported/closed, original images may be deleted unless the user chooses to keep them (policy to be confirmed in settings).
- Settings screen: show app storage in two lines — "Photos (kept)" for `filesDir/scans` and "Temporary" for cacheDir + pending — with a "Clear temporary files" button that never touches pages still in use.
- JPEG quality of the fused output stays 95 until a size/quality trade-off is measured; do not change it silently.

### 7.7 Acceptance checks
- Tap → live preview returns in ≈ (sum of exposures + pipeline latency) without waiting for fusion; a second burst can be started while the first is processing; both end up as `READY` pages.
- No JPEG burst files are created in `cacheDir` during capture; `cacheDir` size after N captures stays ≈ constant; `filesDir/scans` grows only by fused photos.
- Backgrounding the app during processing leaves `filesDir/pending/<id>/`; relaunching completes the fusion and removes the directory.
- Log shows per-frame intervals and queue length.
