# Specification 02: Computational Photography and HDR Reconstruction

> Status: Authoritative. Revised 2026-10-12.
> Filename retained for compatibility; Vulkan HDR is not integrated.
> Language: English.
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

**Experimental implementation:** shorter exposure candidates need valid observed
exposure and alignment. Recovery uses channel clipping evidence, shared unclipped
JPEG response samples and a monotonic highlight shoulder. Prefer useful signal
with lower noise; fall back to the reference when reliable recovery is absent.

JPEG transfer inversion and exposure ratios approximate scene response. Vendor
local tone mapping prevents treating these samples as calibrated radiance.

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

SPEC_01 steadiness measures preview scene motion. It does not prove focus, no
motion blur during exposure, no flicker or no clipping. The future document
readiness profile combines these separate signals and may recommend a retake.

Document capture must prioritize small readable strokes; photo capture must
retain natural tone. HDR, super-resolution and paper whitening are independent
choices. No mode should enable all processing merely because a document quad
exists. Scene-specific defaults are **required / not implemented**.

## 7. Resource and failure contract

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

Apply PHOTO-01 through PHOTO-05 and CAP-01 through CAP-03 in
[SPEC_06](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md). Required cases include
colored clipped lights, white screens with dark small text, repeated text lines,
handheld parallax, partial burst failure, ignored exposure requests and identical
frames. Record baseline comparisons and explicit fallback reasons.

No new APK, real-camera image-quality benchmark or competitor runtime comparison
is delivered by this specification revision. Existing sampling unit checks are
not a substitute for these end-to-end cases.

