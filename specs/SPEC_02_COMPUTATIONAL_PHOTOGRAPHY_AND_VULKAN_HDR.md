# Specification 02: Computational Photography and HDR Reconstruction

> Status: Authoritative. Revised 2026-10-13.
> Filename retained for compatibility; Vulkan HDR is not integrated.
> Language: English.
> Noise follow-up: [2026-10-13 contract](research/NOISE_REVISION_2026-10-13.md).
>
> Quality revision contract: [2026-10-12 follow-up](research/QUALITY_REVISION_2026-10-12.md). Experimental implementation; numerical tuning and physical-device acceptance remain unverified.

## 1. Scope and superseded claims

The current capture pipeline consumes ISP processed JPEG frames, not RAW sensor
samples. It performs CPU processing. Experimental multi-frame reconstruction and
highlight recovery must be evaluated independently of output pixel count.

This revision supersedes the conflicting historical Mertens, fixed 50 MP,
reference-edge exclusion, white locking, full-image cinematic tone and screen
bypass rules. Those designs are historical material, not simultaneous required
passes. No equivalence to a named commercial camera algorithm is claimed.

## 2. Input and exposure contract

**Implemented with experimental tuning.** Each frame carries its intended tier,
observed exposure product and orientation. Frame count or average brightness must
not be used to infer a missing frame's tier. SPEC_01 defines capability checks,
relative exposure planning, and actual metadata validation.

**Experimental implementation:** rank eligible base frames by the upper-quartile
tile Laplacian energy after modest Gaussian smoothing, penalizing clipping and
observed exposure mismatch. Deterministic ties retain arrival order. Log each
score and the selected index. Other frames must match dimensions/type.
Document-region-only ranking, noise-calibrated focus and physical-device
reference comparisons remain required. The synthetic blurred-first fixture
checks that a sharper later frame is selected; it is not a universal sharpness
certification.

## 3. Registration and local reliability

The [rc8 contract](research/DETAIL_RESOLUTION_REVISION_2026-10-13.md) replaces
first-success base alignment with measured candidate comparison. Compare global
high-pass structural agreement; retain deterministic order for improvements
<=0.02 and allow early exit at >=0.985 (experimental). Keep source-local
reliability, crop limits, phase diversity and detail/coverage gates unchanged.
Log why SR is denied. A higher output pixel count does not itself satisfy text
contrast or resolved-detail acceptance.
Base candidates include Euclidean ECC, bounded projective ECC initialized from
the valid coarse transform, ORB homography and phase correlation. Projective
ECC uses a reduced grayscale image and pixel-center-correct conjugation back to
source coordinates. Require convergence, finite/invertible geometry, the same
8%-of-long-edge displacement bound and structural validation. This remains a
global model; it does not replace local validation or solve multi-depth parallax.

**Implemented:** global alignment candidates include photometric refinement,
feature based registration and phase correlation. Short exposure alignment has a
separate path. A source-scale overlapping tile translation refinement and local residual gate reject unreliable regions;
this is not dense optical flow, depth estimation, or rolling shutter correction.

**Required / partly implemented:**

- Validate transform geometry, coverage and residuals before accepting a frame.
- Evaluate periodic text, repeated table lines, weak texture, moving objects,
  page curvature and parallax explicitly. A plausible global transform is not
  proof that all local strokes align.
- Preserve reference pixels where local correspondence is unreliable.
- Do not categorically remove all text-edge samples: reliable edge samples are
  needed for reconstruction. Conversely, sharpening cannot validate misalignment.
- Specify each rejection reason and accepted frame count in bounded diagnostics.

Tile refinement is bounded to 8 source pixels and validated using high-frequency
correlation, relative detail and per-pixel support. Across motion discontinuities
unsupported samples retain the selected reference. This is experimental sparse
tile alignment. Dense local alignment is a **research candidate / not implemented**. It requires
independent quality and resource evidence before becoming a product feature.

## 4. Multi-frame sampling and resolution

**Experimental implementation:** compact-support accumulation samples accepted
source frames. The current path can output twice the width and height when
at least three base frames, qualifying independent two-dimensional phases and
sufficient local support are present; otherwise it
uses native dimensions. Current phase diversity gating is a heuristic, not proof
of increased resolving power or complete two dimensional phase coverage.

The **current experimental parameters**, recorded from the implementation, are:
SR requested, at least three accepted base frames, source size at most 16 million
pixels, qualifying local detail on phase-contributing frames (fraction >= 0.15),
centre-offset phase determinant >= 0.04 for a pair, and mean non-reference local
support >= 0.50. All conditions must pass for scale=2; otherwise scale=1. These
are uncalibrated eligibility heuristics, not an accepted resolving-power profile.
Selection is per burst, so successive captures can have different output sizes.
Neither capture order nor camera warm-up is an eligibility condition. The
whole-image support fraction is distinct from final per-pixel multi-frame
coverage; neither certifies a particular text region.

The 2026-10-14 13:25 paired screen evidence has mean support 0.4873 with the
other global conditions passing. A production-code host probe finds substantially
higher support in a text ROI, with a passing phase check at its centre. This
demonstrates the global gate's target-region limitation, not recovered detail or
a calibrated replacement threshold. See the
[screen analysis](reports/RC10_SCREEN_ANALYSIS_2026-10-14.zh-CN.md).

**Required / not implemented:** retain a structured per-job SR decision with
each eligibility check, measured value, threshold and denial reason in test
diagnostics. Distinguish actual phase diversity from the combined SR decision;
do not overload `phases2D` with a coverage failure. Keep requested native capture,
achieved source size, reconstruction scale and final cropped/rotated dimensions
separate. This diagnostic requirement does not restore viewfinder MP captions
removed by SPEC_05. Do not lower gates or unconditionally enlarge an image merely
to make successive files carry the same megapixel count. Region-specific
eligibility and measured improvement remain research / unimplemented work.
Regional research acceptance must check phases and support throughout eligible
text tiles, preserve the reference in unsupported/moving regions, and compare
stroke fidelity with same-size single-frame interpolation. A single ROI-centre
phase check or lowered whole-image threshold does not satisfy that acceptance.

Output dimensions must be derived from the accepted data and resource limits.
Upscaling, edge contrast or a larger megapixel label alone must never be reported
as recovered detail. Unsupported output locations retain a valid reference
estimate; they must not contain uninitialized or fabricated detail.

**Required acceptance:** use synthetic known subpixel phases and real text/line
charts. Compare the final result to native reference and matched-size single-frame
interpolation. Report resolved detail, edge spread, stroke preservation, aliases,
ringing, ghosting and noise separately. Identical frames and failed alignment
must not produce a claimed resolution gain.

The approximate 12 MP capture and 50 MP output goals are resolution selection
targets, not an unconditional exact output size. Audit actual decoded dimensions
and allocations before processing.

## 5. Highlight, noise and tone behavior

The [night/screen revision](research/NIGHT_HDR_REVISION_2026-10-14.md) separates
clipped-target radiometry from independently observed neighboring geometry. Keep
unclipped outer-boundary samples; a proposed blanket erosion failed the existing
clipped-screen regression and is not part of this contract. Clipped interiors
remain excluded from correlation. Unclipped targets
retain residual checks; clipped targets need validated local support and useful
short observations rather than agreement with the clipped reference value.
Observed dark structure may replace clipping only within 12 source pixels of
valid bright short signal, with verified local geometry. Unsupported
inputs cannot gain dark replacement from one-sided dilation: use closing of the
bright-signal mask to support enclosed small holes. All-clipped/occluded
inputs retain fallback; this rule grants no SR credit and remains experimental.

The rc6 noise revision requires separate native-size denoising and SR/detail
eligibility. Noise-aware flat-region support must not count as structure, and
locally verified masks below 20% must not be categorically discarded. Retain
bounded geometry/blur/occlusion checks, noise-aware reference ranking and
per-pixel fallback. Report actual multi-frame coverage and mean contributors,
not only admitted plates. The linked contract owns experimental tuning and
synthetic gates. Single-base survival must be visible to the user.

**Experimental implementation:** shorter exposure candidates need valid observed
exposure and alignment. Recovery uses channel clipping evidence, shared unclipped
JPEG response samples and a monotonic highlight shoulder. Prefer useful signal
with lower noise; fall back to the reference when reliable recovery is absent.

JPEG transfer inversion and exposure ratios approximate scene response. Vendor
local tone mapping prevents treating these samples as calibrated radiance.

Current `clippedFraction` counts pixels with a base-merge channel >= 250 **before**
highlight replacement, divided by output pixels. `recoveredFraction` counts
output locations actually changed by accepted short observations. Neither is a
post-HDR clipping metric or a guarantee of readable recovered content.
**Required / not implemented:** separately record post-recovery clipping and
useful recovery in relevant text/light regions, and validate confidence in JPEG
response estimates when they differ greatly from metadata exposure ratios.
Do not force a match to either ratio without observed signal/geometry evidence.

**Required / partly implemented:**

1. Detect individual color-channel clipping as well as luminance clipping.
2. Do not transfer noise, dark-frame readout artifacts or moving edges as detail.
3. Do not force paper-like white pixels to remain clipped after recovery.
4. Preserve ordinary midtones, colored ink and photographic tone unless the user
   selects a documented enhancement. Document whitening is a separate SPEC_04 pass.
5. If every input clips or an opaque glare region contains no valid observation,
   preserve the source and report the limit. HDR does not reconstruct absent text.

Temporal averaging can reduce independent noise, but correlated ISP processing,
misalignment and subject motion change the result. A four-frame count is not an
unconditional signal-to-noise or sharpness guarantee.

## 6. Capture quality versus preview steadiness

The [rc7 correction](research/CAMERA_HDR_REVISION_2026-10-13.md) requires short
exposure validation in an exposure-normalized comparison domain. Estimate the
effective JPEG response from shared unclipped samples, project the short image
to reference brightness with channel saturation, and validate local geometry
there. Keep original short pixels for recovery. Clipped reference pixels do not
provide independent stroke evidence; recovery needs adjacent matched structure,
bounded local displacement and normalized residual checks on unclipped targets.
Clipped targets follow the bounded observed-signal rule above. Entirely
unsupported/occluded regions must fall back. Normalization does not calibrate
vendor tone mapping, and dense motion/rolling-shutter correction remains absent.
Normalized short patches must also retain at least 0.68 of reference band detail
(experimental), so exposure normalization cannot admit a blurred short merely
because its broad shapes correlate.
For highlights only, use the closest validated patch within its measured
interior (at most 140 source pixels per axis from its center; experimental).
Do not require four neighboring nodes when only one patch observes useful
structure. Reject overlapping patches with displacement disagreement >1.5 px;
retain pixel residual gates. This bounded patch support is separate from base
frame interpolation/SR and cannot grant base-frame detail or phase credit.
An already globally aligned short patch may retain zero local refinement when
its measured high-pass correlation is >=0.95 and detail ratio >=0.68. This
verified identity candidate avoids discarding correct geometry merely because
masked ECC cannot converge beside clipping. Otherwise refine and validate as
usual; failure of both candidates retains reference pixels.
Exclude a reference sample from local geometry validation if ANY color channel
is clipped (>=247), even if its grayscale value is midtone. Carry this validity
mask into normal and streaming patch checks; it does not mask the recovery
target, whose clipping is precisely why a supported short observation is needed.

Do not discard a useful short frame solely because supported highlights cover
less than 5% of the whole image. Normal-mode admission requires at least 1024
locally supported source pixels; native streaming requires validated patch
support. No short frame grants SR phase credit. Log accepted short frames and
the actual recovered-pixel fraction separately; label the result HDR only when
short pixels actually change the output. If clipping is appreciable (>0.5%,
experimental) and no pixels recover, retain multi-frame denoising and visibly
state that highlights were not recovered. All-clipped inputs cannot invent text.

SPEC_01 steadiness measures preview scene motion. It does not prove focus, no
motion blur during exposure, no flicker or no clipping. The future document
readiness profile combines these separate signals and may recommend a retake.

Document capture must prioritize small readable strokes; photo capture must
retain natural tone. HDR, super-resolution and paper whitening are independent
choices. No mode should enable all processing merely because a document quad
exists. Scene-specific defaults are **required / not implemented**.

## 7. Resource and failure contract

The experimental native-high-resolution profile uses exclusive admission,
compressed temporary inputs, reference plus one candidate decode, bounded local
patch processing and uint16 sums/byte counts. It returns native dimensions,
without another 2x reconstruction. The linked 2026-10-13 contract owns initial
memory/pixel limits and cleanup requirements; rc9 raises the conservative estimate
to 20 bytes/pixel for highlight neighborhood masks. Device peak memory is unverified.

Use the single admitted burst worker from SPEC_01. Keep decoded buffers and
output allocations bounded; avoid multiple output-size floating-point pyramids.
Partial frame failure reduces available evidence and must not change tier identity.

If fusion fails, save a valid normalized reference with visible fallback
feedback. Mark READY only after successful save; failed reference save yields
FAILED. Compute a conservative common source interior, with at most 5% loss per edge;
reject an incompatible base frame rather than exceed that crop. Return the source
rectangle and first-frame-to-selected-reference transform for SPEC_03 coordinate
mapping. Highlights do not dictate the crop. Report actual accepted tiers, chosen
reference, source rectangle and output scale rather than a fixed
seven-frame or fixed 50 MP success claim.

Vulkan HDR/LTM, RAW fusion, dense motion compensation and guaranteed OEM hardware
temporal processing remain **not implemented / unverified**. Their presence in
earlier diagrams, metadata or static library inventories is not acceptance.

## 8. Acceptance and limits

Apply PHOTO-01 through PHOTO-09 and CAP-01 through CAP-05 in
[SPEC_06](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md). Required cases include
colored clipped lights, white screens with dark small text, repeated text lines,
handheld parallax, partial burst failure, ignored exposure requests and identical
frames. Record baseline comparisons and explicit fallback reasons.

Executed synthetic/build checks and test APK identity are recorded in the dated
rc4/rc6/rc7 reports. They do not establish a real-camera image-quality benchmark
or competitor runtime comparison. Physical-device acceptance remains pending.

