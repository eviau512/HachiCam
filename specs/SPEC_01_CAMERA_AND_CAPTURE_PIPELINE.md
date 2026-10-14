# Specification 01: Camera Subsystem and Capture Pipeline

> Status: Authoritative. Revised 2026-10-14.
> Language: English.

## 1. Modes and achieved outcome

**Implemented in rc6, device quality pending:** Full HDR/multi-frame capture is enabled by default and requests up to four
base frames, two shorter target frames and one deeper target frame. Frame loss,
unsupported manual exposure, invalid metadata or failed alignment may reduce
the usable set. An explicit single-frame override remains available. Native
high-resolution burst is a separate opt-in experimental profile, subject to
exposed JPEG sizes, exclusive admission and memory checks in the linked noise
revision. Neither seven valid frames nor a fixed 50 MP result is guaranteed.

The current pipeline is CameraX plus Camera2Interop and CPU JPEG processing.
It is not a direct Camera2 captureBurst, a RAW pipeline, or a commercial camera
algorithm. SPEC_02 owns reconstruction and achieved-output quality.

## 2. Capability-driven camera controls

The [night/screen revision](research/NIGHT_HDR_REVISION_2026-10-14.md) requires
bounded shutter AF and burst-long focus preservation when supported by fresh
stationary focused metadata. Auto-cancel must not expire within the burst;
cancellation-safe cleanup restores controls. Unknown focus stays unknown.
Use the first captured JPEG's exactly matched result to select the held distance;
preview metadata around the AF acknowledgment cannot substitute for that frame.
Deep brackets may prefer lower ISO and supported 100% post-RAW boost within a
16.667 ms shutter cap, preserving the full exposure product rather than the old
gain allocation. Validate actual metadata; ordinary tiers retain their plan.

The [rc8 detail/resolution contract](research/DETAIL_RESOLUTION_REVISION_2026-10-13.md)
requires retaining capture variants within each grouped view. Experimental native
high-resolution selection may use a compatible same-view endpoint with a larger
ordinary/high-resolution JPEG budget. Do not confuse the API 31 maximum-resolution
stream map with ordinary CameraX sizes: a separate supported sensor-pixel-mode
session is required and is currently unimplemented. Test diagnostics must list
all three maps and actual selected endpoint; logs retain achieved dimensions/MP.
The rc9 viewfinder removes these technical captions per the user's request;
settings explains experimental limits. This supersedes rc8 caption placement.
Keep the latest capability snapshot and current-burst metadata available to log
export independently of logcat retention. Newer burst IDs supersede older work;
do not mix their records. This snapshot contains metadata, not retained photos.

Match capture-result sensor timestamp to JPEG timestamp with bounded storage.
When available, manual planning uses sensor ISO/time and preserves the observed
supported post-RAW sensitivity boost, plus AWB lock across tiers. Do not infer
these controls from JPEG brightness. Use consistent sensor or EXIF evidence for
burst exposure comparisons; missing AF/lens/boost evidence remains unknown.

The [rc7 camera/HDR correction](research/CAMERA_HDR_REVISION_2026-10-13.md)
supersedes rc6 raw camera-ID enumeration: an ID is an endpoint, not a lens.
Keep the default back endpoint and only expose additional compatible JPEG/YUV
endpoints with a distinct metadata-derived field of view (12% experimental
grouping tolerance). Prefer the default within its group; omit unclassifiable
alternatives. Never infer hardware count from IDs or hardcode a phone model.
Separate lens/view selection from supported logical/digital zoom controls.
Record endpoint characteristics and grouping in test builds only.

The live preview is a single-exposure SDR stream; Full HDR describes post-capture
processing. The user's rc12 revision removes the manual EV button/menu. Reset
supported compensation to zero on rebind, acknowledging failure, and retain
achieved compensation in test diagnostics. Automatic metering at the selected
target replaces the manual aid; zero compensation does not mean constant exposure.

The [2026-10-13 revision](research/NOISE_REVISION_2026-10-13.md) additionally
requires supported back-camera selection and logical zoom controls, with
capture-time switching disabled and state reset on rebind. Capability queries
belong to the selected camera. Actual capture dimensions must be reported;
the normal source budget remains 16 MP; the experimental native profile permits
at most 56 MP with streaming decode/patch validation. Advertised sensor resolution and
logical zoom do not certify a native high-resolution mode or a physical switch.

**Implemented / hardware dependent:** prefer supported FAST noise reduction,
retain camera-default sharpening and tonemapping, request optical stabilization
where available, and disabled video stabilization for
still capture. Check camera capabilities, ISO/exposure ranges and AE/AWB lock
availability. Unsupported or ignored controls require graceful fallback.

The rc4 policy supersedes forced HIGH_QUALITY processing; the aim is to avoid
unconditionally strong ISP processing before text fusion. FAST and camera
defaults are device-selected processing requests, not guarantees of a particular
vendor temporal denoiser, multi-frame count, detail improvement or proprietary engine.
Do not infer ISP internals from a request key or a library name. See the
[Android noise reduction contract](https://developer.android.com/reference/android/hardware/camera2/CaptureRequest#NOISE_REDUCTION_MODE).

**Required / not fully implemented:** inspect request/result support consistently
for every injected option, document lens/device fallback, and distinguish
requested settings from achieved frame metadata.

## 3. Focus, exposure and document readiness

**Implemented:** a valid preview tap maps through the preview metering point
factory to AF and AE, with a three-second automatic cancel. The visible tap ring
acknowledges the request; it is not proof of successful focus.

**Historical rc10 shutter selection:** reuse the stored PreviewView metering point only
when its monotonic age is 0..3000 ms in the active session; otherwise use centre
AF-only, bounded as specified above. Session reset clears the stored point.
The 13:25 screen user confirmed tapping the screen, but its diagnostics lack tap
age/coordinates and shutter-target selection. Neither focus acknowledgment nor
held distance proves that the intended tap was reused. Recording selected
tap/centre, age and session identity in test-only per-capture diagnostics remains
implemented by rc11 below, with device acceptance pending. This does not establish
that the earlier sample's target was overwritten.

**rc11 revised target lifetime, specified before implementation and now implemented:** retain a valid
user preview metering target independently of the 3-second preview AF/AE
auto-cancel and 1.5-second ring. At shutter, refocus at that target until a new
valid tap, session reset, zoom change, preview layout-size change or applied
orientation change invalidates its geometry; then use centre. This is a position
in the current view, not semantic object tracking. A failed tap AF does not
silently select another subject. Snapshot/record target type, normalized point,
monotonic age, capture/session identity and actual acknowledgment separately.
Fixed-focus/missing AF support remains unknown, with manual capture available.
Capture snapshots its control/info under the session identity; an obsolete
session cannot refocus or clean up the subsequently bound camera. Actual device
target selection and optical sharpness still need verification.

### rc12 target metering continuity (specified before implementation; implemented, device checks pending)

The paired 19:50 rc11 slide sample reused the tap (age 1338 ms), held focus and
recorded EV compensation zero, yet its base JPEG already has weak screen detail.
Code and the installed CameraX 1.4.1 control confirm that a new AF-only action
replaces AE regions with an empty set. Locking AE afterward can freeze a different
metering result. Previous diagnostics do not establish the exact preview-to-base
exposure increase; this is a confirmed control discontinuity, not a measured EV jump.

At shutter retain both AF and AE at the same stored metering point (or centre when
invalid), with auto-cancel disabled through acquisition. Fixed-focus cameras may
still meter AE; unsupported modes remain unknown rather than blocking manual
capture. Separate action acknowledgment from optical AF success. Keep the existing
1500 ms action bound, then wait at most 750 ms for two new, timestamp-distinct
results after the action acknowledgment, in the same active session. Both must be
observed within 500 ms, AE CONVERGED with no pre-existing AE lock, and have positive
finite exposure products within 0.15 stops, with consistent boost availability.
For cameras with AE regions, require nonempty identical actual request-region
signatures on both observations. Lack of region support cannot certify metering
at a tap and uses the unknown fallback. These freshness/stability values are experimental.

Request burst AE lock only when that measured target exposure settled and the
device advertises lock support. Otherwise keep target AE active without claiming
a successful lock, record bounded fallback, and permit capture. Options futures
still require acknowledgment; result AE state/lock is evidence of execution.
Record before-action, settled/timeout, lock request and actual first JPEG AE state,
compensation, region count/signature and ISO/time/boost under the capture identity.
Never use repeating preview evidence as JPEG exposure evidence; exact timestamp
matching and first-JPEG focus hold remain mandatory. Session invalidation/cancel
must restore owned controls without operating a new camera.

This fixes control continuity, not emissive-screen tone response, flicker, optical
bloom, target tracking or all-source clipping. Verify the same device with a small
bright slide on a dark surround; compare preview, first base and final result.

**Required / not implemented as an integrated readiness profile:** separately
evaluate usable document boundary, focus/sharpness, motion/blur risk, clipping
and exposure usability. Unknown evidence must remain unknown. Low light may
trade shorter shutter against noise; this is a measured document-quality policy,
not a universal ISO or shutter constant. Evaluate usable detail in spatially
separated document regions; a sharp center cannot certify sharp tilted corners,
and noise energy cannot substitute for sharp text. Retain the capture and offer
a retake or continued manual review after a quality warning.

Flicker/banding under mains lamps and emissive screens must be included in
device tests. Where supported, anti-banding policy must be capability driven;
a still frame or a stable quad does not prove flicker-free exposure. Manual
capture remains available even when quality guidance is uncertain.

## 4. Viewfinder Stability Detection

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

## 5. Bracket planning and capture metadata

**Implemented with experimental targets:**

| Tier | Maximum requested count | Relative target |
| --- | --- | --- |
| Base | 4 in HDR, 1 in Normal | Current automatic exposure |
| Shorter | 2 | -2.5 EV |
| Deep short | 1 | -6 EV |

For planning only, let P = ISO * exposureTime and
P_target = P_base / 2^stops. Solve within actual device ranges and omit a tier
when no useful shorter exposure is possible. The former ISO/4 and time/3 rule
is approximately -3.58 EV, not -2.5 EV; it is superseded.

Await control application and validate actual available frame exposure metadata.
A control future succeeding does not prove the exact exposure was applied.
The current acceptance gate requires an observed product below 75% of the base
to retain a shorter tier; this is **experimental**, not exact target verification
or calibrated radiometry. Missing metadata must not be invented.

Carry bytes, orientation, intended tier and observed exposure together. Sensor
timestamps, source crop and achieved capture-result consistency are **required /
not fully carried end to end**. Tier switching must not rely on a fixed delay
as proof that a setting has latched. Restore automatic controls on success,
failure, cancellation and timeout.

## 6. Image orientation, mapping, decoding and provenance

**Implemented:** preview luma respects buffer position, row/pixel stride and
camera rotation; memory JPEG decoding ignores EXIF auto-rotation and applies the
CameraX rotation once before save. Imports use Android ImageDecoder to normalize
supported formats and mirrored orientation before writing actual JPEG.

The camera returns an image with a rotation instruction; do not double-apply
that instruction through both decoder and metadata paths. See the
[CameraX capture callback](https://developer.android.com/reference/androidx/camera/core/ImageCapture.OnImageCapturedCallback#onCaptureSuccess(androidx.camera.core.ImageProxy)).

**Required / partly implemented:** define source/crop/analysis/sensor/view/output
spaces and transforms, including zoom, lens changes, mirror, letterboxing,
negotiated dimensions and reconstruction scale. Current FIT_CENTER mapping
excludes bar taps, and shutter geometry is transformed to actual upright output
dimensions. The analyzer does not yet apply all valid cropRect/freshness cases.
A whole-frame normalization assumption is not a universal camera-space contract.
See [CameraX resolution and crop](https://developer.android.com/reference/androidx/camera/core/ResolutionInfo).

Imported-image budgets must be checked before full decode; unsupported/corrupt
input returns a visible failure without a successful page. End-to-end explicit
color profile conversion, alpha handling and HDR/gain-map policy remain
**required / not fully implemented**. See
[Android image decoding](https://developer.android.com/reference/android/graphics/ImageDecoder).

EXIF software/comment fields may record actual build identity and achieved
processing outcome. Normalize orientation after baking rotation. These mutable
strings are provenance descriptions, not cryptographic signatures or tamper
evidence. Release output excludes device Make/Model; debug diagnostics follow
SPEC_00 build policy.

## 7. Capture, processing and storage lifecycle

**Implemented:**

1. Reserve one of at most three total in-flight job slots before acquisition.
   The limit includes capture, waiting and executing work.
2. Copy compressed JPEG bytes into Java byte arrays and close each ImageProxy
   on every callback path. Do not retain camera-owned images while processing.
3. Insert PROCESSING after acquisition and submit to one application-wide FIFO
   worker. Restore controls and release capture busy state without waiting for
   reconstruction. Subsequent capture depends on admission capacity.
4. Keep PROCESSING pages out of final crop/export. Mark READY only after a valid
   final or explicitly indicated fallback reference is saved. A failed fallback
   save yields FAILED.
   The refactor installs the unique source JPEG and publishes READY under the
   repository ownership lock, only while that capture still exists as PROCESSING.
   Deleting a pending page invalidates installation; a committed READY source is
   never replaced by a later fallback. Temporary encoding is outside that lock.
5. Save completed captures under app private scans storage. Source frame data
   remains RAM-resident while pending. Continuing after screen navigation only
   holds while the process lives.

**Implemented in rc10, experimental:** a shared heavy-image permit, queued JPEG
reservations and output-aware admission/degradation estimates. **Required / not
implemented:** direct/native pending buffers, a measured byte
budget and spill policy, durable pending manifests, page metadata persistence
and restart recovery. The historical 96 MB and seven-day values are unvalidated
design proposals. Three admitted jobs do not prove bounded memory across all
review/crop tasks, and onStop alone cannot guarantee survival of process death.

**Implemented in rc10; device failure cases pending:** cancellation-safe resource release,
transactional source/derivative writes, active-file-aware cleanup, and last-valid
image preservation. Export or session closure must not implicitly delete source
photos. Storage UI must distinguish kept scans from disposable temporaries.

## 8. Timing and acceptance

Frame intervals depend on exposure, sensor readout, ISP, encoding and scheduling.
A fixed 10 ms interval or 400 ms complete burst is not guaranteed. The section 4
stability wait is a separate bounded wait, not the total capture budget.

Measure camera acquisition, queue time, processing and save separately, with
observed exposure and accepted frame counts. Apply CAP-01 through CAP-03,
GEO-01 and LIFE-01 through LIFE-03 in
[SPEC_06](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md).

**Implemented in rc10 (control-state ownership):** record control
request acknowledgment and actual captured settings separately. If base/focus
option application or final restore times out/fails, do not equate the elapsed
timeout with a known restored camera. Record the failure under its capture/session
identity and restore/rebind or explicitly degrade before normal reuse. The
base/focus callers inspect acknowledgment results and cleanup records clear/cancel
results before queue submission. Restore failure blocks normal capture until a
generation-matched rebind; actual metadata still determines fusion eligibility.
Acknowledgment alone does not prove the OEM executed the requested settings.

Device acceptance must include unsupported manual control, ignored settings,
missing EXIF, partial burst loss, all orientations, mirrored imports, zoom/crop,
flicker lighting, navigation during processing, saturated queues and save
failure. rc10 build/helper checks do not substitute for this device acceptance.
