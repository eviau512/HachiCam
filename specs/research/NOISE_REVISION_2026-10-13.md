# Noise-first multi-frame revision

Status: implementation contract, 2026-10-13. English clean-room handoff.

## Evidence and problem

The user supplied an rc5 log and a 4096 x 3072 JPEG of a screen/room. The JPEG
and log agree: reference 3, anchors 1, highlights 0, scale 1, no source crop.
Four base captures used approximately 16.65 ms at ISO 573. Three globally aligned
base candidates passed with high-frequency correlation 0.938/0.965/0.964 but
local support was 0.095/0.095/0.105; the 20% whole-frame admission rule discarded
all three. All short exposures also had zero local support. The final image has
visible dark-region grain, but it cannot establish the original frames' noise
independence, focus or possible reconstruction gain. The source burst was not
provided. Moire removal is outside this revision's priority.

Inspection identifies two coupled risks: noise can make a flat area exceed the
fixed 1.5 high-pass texture threshold, then fail the strict correlation gate;
requiring every interpolation node to be valid expands the excluded region.
A whole-frame fraction discards even correctly supported local observations.
These mechanisms must be covered by realistic independent-noise regressions.

## Revised fusion contract

1. Separate denoising evidence from subpixel/detail evidence. Estimate stochastic
   noise robustly from diagonal high-frequency differences, compare structural
   energy after accounting for estimated filter noise, and keep the estimates
   bounded. JPEG noise estimation is heuristic, not sensor calibration.
   Subpixel-warp regression exposed reduced coverage with single-pixel diagonal
   noise estimates: interpolation correlates grain. Use bounded diagonal
   estimates at separations 1, 2 and 4 and retain the largest robust estimate.
   This tuning prevents correlated resampling noise from becoming false texture;
   it does not promise removal of noise correlated between captured frames.
   Flat/structure classification must use the same interior region as later
   correlation, excluding the 10 px patch boundary. The shifted fixture exposed
   boundary-only edges causing random-grain ECC fits and failed interior scores.
2. A low-texture region may participate in native-size denoising after global
   validation and local low-frequency photometric agreement. It must not count
   as detail or as independent SR evidence. Do not optimize an ECC translation
   to fit random grain. Give trustworthy same-exposure flat samples useful
   averaging weight instead of automatically retaining noisy reference grain.
3. Meaningful structure still needs bounded translation refinement, noise-aware
   structural correlation and blur/occlusion checks. Preserve the previous
   reference fallback across unsupported or discontinuous motion. Do not solve
   noise by indiscriminately loosening text alignment or spatially blurring all
   output. Distinguish denoise-only nodes from detail-qualified nodes explicitly.
   If base-frame noise is present, strongly supported coarse registration
   (ECC >=0.95) plus band correlation >=0.80 and detail ratio >=0.68 may qualify
   native denoising only. The >=0.90 band gate remains required for SR/detail.
   This follows the subpixel-noise regression; no-noise and blur/occlusion
   safeguards remain stricter, not waived by this alternative.
4. Keep globally verified frames with meaningful locally verified support,
   including sparse support below 20%; apply their masks at each output pixel.
   Initial admission floor is 0.5% and 1024 supported source pixels. Common
   geometric coverage still obeys <=5% crop per edge. Unsupported areas retain
   the selected reference; retaining a frame is not a claim it contributes
   throughout the image.
   Native streaming's raw RGB residual gate uses a bounded threshold reflecting
   both frames' noise and gray-to-channel variance (initial 6.35 gray sigma,
   clipped to 14..64 levels). A narrow reference-dependent gate biases averages
   toward reference grain; structural/blur/occlusion checks remain mandatory.
5. Rank references using noise-aware structural energy, with deterministic ties,
   so stochastic variance cannot by itself earn a sharpness advantage. Retain
   blur and clipping safeguards. No universal best-focus claim.
6. Keep conditional 2x reconstruction. Only structured, reliable alignment and
   independent 2D phases count; flat noise cannot qualify a resolution increase.
   Native-size multi-frame denoising remains useful without 2x eligibility.
7. Return/log actual multi-frame pixel fraction and mean contributors separately
   from admitted-frame count. Record denoise-only/detail support and noise
   estimates. EXIF must describe achieved processing. If only one base survives,
   show visible single-frame fallback guidance even when native processing did
   not throw an exception. Report requested versus negotiated capture dimensions.

## Camera applicability

Preserve the default back/main camera and add user switching among back camera
IDs actually exposed to CameraX. Retain logical-camera zoom where supported;
zoom ratios must not be advertised as guaranteed physical lens changes. Query
capabilities for the selected camera, disable rebinding during capture, clear
motion/crop/focus state when switching, and restore a usable preview on bind
failure. Do not invent inaccessible vendor camera IDs. Shared burst frames and
metadata must stay associated with the captured camera/session.

User steering after the initial analysis: Full HDR becomes the default capture
path; native high-resolution capture becomes a separate opt-in experimental
feature. Retire the old full_hdr_enabled opt-in key so an old default false does
not silently suppress the new default; preserve an explicit new single-frame
override. Enabling native high resolution also enables multi-frame capture.

Do not assume a sensor's advertised megapixels equal a CameraX JPEG mode.
Prefer supported capture sizes within the current tested source budget rather
than permanently hard-code one 4096 x 3072 target. Default source budget remains
16 million pixels. The experimental profile may request the highest supported
JPEG size above 16 MP and at most 56 MP, with observed negotiation and visible
fallback when unsupported. No rescaling can masquerade as native capture.
High-resolution output stays at native scale; no further 2x allocation.

High-resolution jobs are exclusive from admission through capture and save.
Limit retained encoded burst data to 96 MiB and check available memory before
capture and decoding. Estimate native peak conservatively at 16 bytes per
source pixel plus 128 MiB, require an additional availability margin, and refuse
or visibly return to the normal profile if not admitted. These are initial
engineering bounds; device RSS/thermal validation remains pending.

Persist high-resolution compressed inputs in temporary app-private files,
decode reference plus at most one candidate at a time, use bounded native
patches for local validation, and accumulate accepted base samples into uint16
sums plus uint8 contributor counts. Four base frames fit without overflow;
divide once at the end to avoid cumulative byte rounding. Do not retain all
decoded input plates or allocate full-resolution floating-point pyramids.
Release temporary files on success/failure. This is not durable job recovery.
Retain highlight validation and <=5% common crop. Report actual native dimensions,
contribution coverage and achieved modes. Preserve supported conditional
reconstruction in the normal profile.

Official API references (not competitor implementation):
[CameraX selection](https://developer.android.com/media/camera/camerax/configuration),
[resolution selection](https://developer.android.com/reference/androidx/camera/core/resolutionselector/ResolutionSelector).
Higher-resolution output modes may slow capture; no universal frame-rate claim.

## Required verification

Use owned clean targets with independent Gaussian/shot-like and chroma noise,
sparse/faint text, flat shadows, parallax, occlusion, repeated patterns and
blurred/noisy reference candidates. Compare same-size output to the clean truth
and best single frame. Initial synthetic gates: four usable independent base
frames in the noisy sparse-text case; flat-region MSE <=55% of the best single
frame; text MSE improves without losing annotated contrast; identical noisy
frames and flat noise alone cannot qualify 2x. Correlated noise has no promised
sqrt(N) improvement. Retain all existing blur/occlusion/geometry regressions.

Test sparse validated support below 20%, mean contributors/coverage reporting,
noise-driven reference ranking, camera choices, size budget and session resets.
Compile debug/release and all three native ABIs. Host OpenCV differs from
Android; raw burst replay, physical-camera switching and real noise/detail
improvement remain device acceptance work until actually measured.
