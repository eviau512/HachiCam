# Specification 02: Computational Photography and HDR Reconstruction

> Status: Authoritative. Revised 2026-10-15.
> Filename retained for compatibility; Vulkan HDR is not integrated.
> Language: English.
> Noise follow-up: [2026-10-13 contract](research/NOISE_REVISION_2026-10-13.md).
>
> Quality revision contract: [2026-10-12 follow-up](research/QUALITY_REVISION_2026-10-12.md). Experimental implementation; numerical tuning and physical-device acceptance remain unverified.

## 1. Scope and superseded claims

The current capture pipeline consumes ISP processed JPEG frames, not RAW sensor
samples. The CPU pipeline has experimental Vulkan registration primitives in
test builds; HDR/reconstruction are not a complete GPU pipeline. Experimental multi-frame reconstruction and
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

The SPEC_01 dark-clipped-foreground profile may replace the automatic exposure
probe with a verified lower-product four-anchor burst. The probe is diagnostic
evidence only, never an equal-exposure anchor. Existing HDR, alignment, phase and
regional qualification gates remain unchanged. Lower base clipping is an
acquisition improvement and must not be counted as native HDR recovery or SR.

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

### Document alignment research contract (2026-10-14)

**Research candidate; application integration not implemented.** Evaluate bounded
coarse block matching followed by robust pyramidal Lucas-Kanade translation on
overlapping document patches against the current ECC fit. Public descriptions of
RAW burst SR motivate this experiment; they do not establish equivalent results
on HachiCam's ISP JPEG input. An offline prototype is not a product replacement.
Also evaluate the same ambiguity/consistency checks around the existing ECC
optimizer as a smaller incremental candidate. Prefer the least costly validated
change; block/LK need not replace ECC merely because it is used in a public paper.

Use grayscale intensity/gradients with valid unclipped observations, rather than
binary edges alone. Report the two eigenvalues of the local gradient Hessian:
an isolated straight edge does not determine displacement along its tangent.
Require a distinct matching minimum, bounded displacement, usable two-dimensional
structure, residual support and forward/reverse consistency before granting
detail eligibility. A consistent reverse match alone cannot resolve repeated
text. Photometric nuisance gain/offset may assist geometric fitting but may not
modify original samples, certify radiance, or authorize clipped-target recovery.

The mapping convention is source position = reference position + local offset
in the globally warped domain. Reconstruct from original observations using the
composed inverse-global mapping; do not repeatedly resample aligned JPEG plates
as if they were new independent observations. Flat/ambiguous/occluded patches
receive no SR credit. Native denoising and existing highlight fallback remain
separate decisions. Motion-boundary interpolation may not mix incompatible
displacements. Finer tiles are experimental; increased accepted area is not
itself evidence of better stroke fidelity.

Before integration, compare against the **complete** existing registration/gate
path, not just an ECC optimizer. Use independent pixel-integrated synthetic
observations plus held-out real document bursts; report accepted endpoint error,
acceptance rate, false acceptance, per-pixel support, edge/stroke outcomes,
end-to-end reconstruction against matched interpolation, and mobile resource
cost. Freeze exploratory thresholds before held-out evaluation. Do not change
rc11 regional/HDR gates merely to admit this candidate. The handoff and bounded
offline scope are recorded in
[the document alignment study](reports/DOCUMENT_ALIGNMENT_RESEARCH_2026-10-14.zh-CN.md).

**Main feasibility review (2026-10-15; offline validation, not app admission):**
freeze the study's parameters while comparing actual 321x321 interior production
nodes under a shared unclipped, smoothing-safe mask. Record original production
eligibility separately from a same-mask production refit. Apply existing
correlation, signal/noise, detail-ratio and pixel residual checks to each new
proposal; a patch optimizer's acceptance alone is not final SR eligibility.
Report differences in OpenCV versions, common-mask loss and support explicitly.
Without real flow truth, disagreements remain disagreements. A high feasibility
rating for product integration additionally needs original-observation end-to-end
stroke improvement and a native/mobile resource result; neither an offline
precision figure nor relaxed thresholds can substitute for those requirements.

The [Main review](reports/UPSCAN_REVIEW_2026-10-15.zh-CN.md) completed synthetic
reproduction and the shared-mask 321-pixel node/post-gate comparison. Production
and LK each retain 15/27, with three different nodes on either side; real flow
truth, field/renderer benefit and Android cost remain unknown. This does not
meet the high-feasibility product-integration condition. Keep current application
alignment, denoising, HDR and reconstruction unchanged. The next integration
candidate must be a bounded native selective refinement with separate denoise
and detail eligibility; complete end-to-end acceptance before activation.

## 4. Multi-frame sampling and resolution

**Experimental implementation:** compact-support accumulation samples accepted
source frames. The current path can output twice the width and height when
at least three base frames, qualifying independent two-dimensional phases and
sufficient local support are present; otherwise it
uses native dimensions. Current phase diversity gating is a heuristic, not proof
of increased resolving power or complete two dimensional phase coverage.

The **rc10 experimental parameters** are SR requested, at least three accepted
base frames, input <=16 MP, detail-node fraction >=0.15, centre-offset phase
determinant >=0.04 and mean whole-image support >=0.50. These historical gates
are superseded by the experimental rc11 regional contract below.
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

**Implemented in rc11; device verification pending:** retain a structured per-job SR decision with
each eligibility check, measured value, threshold and denial reason in test
diagnostics. Distinguish actual phase diversity from the combined SR decision;
do not overload `phases2D` with a coverage failure. Keep requested native capture,
achieved source size, reconstruction scale and final cropped/rotated dimensions
separate. This diagnostic requirement does not restore viewfinder MP captions
removed by SPEC_05. Do not lower gates or unconditionally enlarge an image merely
to make successive files carry the same megapixel count. Regional eligibility is
experimental in rc11; physical resolving-power/device-quality acceptance remains pending.
Regional research acceptance must check phases and support throughout eligible
text tiles, preserve the reference in unsupported/moving regions, and compare
stroke fidelity with same-size single-frame interpolation. A single ROI-centre
phase check or lowered whole-image threshold does not satisfy that acceptance.

### rc11 regional reconstruction contract (specified before implementation; experimental implementation)

Replace the whole-image support/centre-phase authority with a bounded regional
qualification map. The original local registration, occlusion, border and noise
gates remain. A 64-source-pixel tile qualifies only with the reference plus at
least two same-exposure candidate plates having >=0.70 supported tile pixels,
detail-qualified contributing registration nodes and usable local offsets at
a distributed 3x3 sample grid. At each probe, evaluate the actual source mapping
(inverse global transform plus local offset), not a global centre shortcut;
independent fractional displacement pairs must have determinant >=0.04.
At least four tiles and 0.02 of the common source crop must qualify before a
requested, resource-admitted <=16 MP burst selects scale=2. These initial values
are experimental and require device/independent-chart calibration.
The common crop multiplied by four must also remain <=56 MP before allocating
the enlarged image. A nominal 16 MP input alone does not guarantee that bound;
output-budget denial retains native fusion and is reported separately.

At each output location inside a qualified tile, recheck actual supporting
candidate detail, residual gate, usable mapping and two-dimensional phases.
Only these samples enter observation-driven regional refinement. Locations without that
evidence use the valid native fusion/reference estimate; denoise-only samples
must not receive resolution credit. No post-sharpening or fabricated strokes
can substitute for correspondence. A 2x file has partial eligible coverage,
not a promise of uniformly improved 50 MP resolving power.

Independent chart validation rejected the original compact point kernel (higher
error than matched interpolation); it must not be activated merely by revised
eligibility. Replace its regional renderer with bounded iterative back-projection:
initialize a source-derived cubic estimate; project original same-exposure pixel
observations through the verified global/local mapping with a 2x2 pixel-footprint
forward model; distribute residuals using the adjoint of those weights. Keep a
source-resolution halo, bounded per-worker patch buffers and at most eight
iterations. Robust residuals are limited to 14 encoded levels, and final correction
to +/-24 levels relative to the initial estimate. A tile must improve modeled
observation error by at least 5% (initial mean squared error >1); otherwise retain
the valid native estimate. Smooth tile-border blending prevents discontinuities.
The JPEG pixel-footprint model is experimental, not known OEM ISP/RAW physics.
Each fit needs >=256 usable observations from each of at least three plates;
bound an observation search to 128x128 source pixels and total stored observations
to 50,000 per worker patch. Failed/oversized fits preserve native fusion.
With the two-stage renderer, contributor fractions describe the native fusion
grid; record that domain explicitly, separately from high-grid reconstruction.
Chart error and observed strokes/noise must pass; fitting JPEG data alone does not
prove restored optical detail. HDR recovery remains a separate radiometric stage.
Protect native-grid recovery from changes in sampling during enlargement. The
host replay and original Android recovery counts differ substantially; same-host
old/new comparison does not establish a recovery loss caused by enlargement.
Complete native fusion and HDR before cubic enlargement; perform regional
refinement afterward with a dilated native preservation mask for recovered pixels
and originally clipped targets. Refinement may not overwrite those HDR/evidence
regions. Pre-recovery clipping and recovered fractions now explicitly use the
native fusion grid; neither is post-output remaining clipping.
Flat locations inside an otherwise structured tile keep native denoising: require
source 5x5 luminance range >=max(12, six times the patch's estimated noise sigma)
before applying reconstruction. This experimental local-contrast guard must not
erase faint strokes: ineligible locations retain their native fused estimate.

The per-job structured decision records requested/admitted mode, source/crop,
anchor count, historical mean support/centre-phase (diagnostic only), regional
tile counts/area, thresholds, denial flags, selected scale and actually
reconstructed pixel fraction. Return this through JNI with an explicit packet
schema; retain it in the test-only paired manifest and capture journal. Streaming
has an explicit native-only decision. Keep resource denial separate from image
evidence denial. Device image quality remains unverified until measured.

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

**rc12 screen follow-up, required/research:** the paired 19:50 slide has useful
short-frame text but only about 0.000843% recovered native pixels. A global JPEG
response estimate of 64.55 versus metadata exposure ratio 5.66 is not automatically
an invalid request; ISP response is unknown. Screen-region probes yield different
gains and do not establish that another single scalar fixes alignment/support.
SPEC_01 first repairs target AE continuity at acquisition. Retain the rc11 fusion
guards until an independently validated local/response model passes PHOTO-14;
do not substitute blanket darkening, copied unaligned short frames or sharpening.
The other two new scenes are accepted provisionally by the user and remain
regression references. Output MP/whole-image reconstruction coverage cannot
certify small distant screen text.

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

**rc15 partial-channel recovery correction (specified before implementation):**
The rc14 PPT white field develops blue/dark impulses during native HDR, before
reconstruction. A scalar response estimate plus a shoulder driven by the largest
short-channel radiance can strongly rescale reference channels that never
clipped. Compatibility checked before that shoulder does not protect the output.
For a base-merge pixel with any channel >=247, preserve every channel <247
exactly through highlight replacement; only clipped channels may be reconstructed
from the independently geometrically admitted short sample. All-clipped pixels
retain the existing common radiance/shoulder formula and observed-hole policy.
Pixels without any clipped channel retain native fusion rather than applying a
scalar-response shoulder to already observed highlights. Do not turn clipping
noise across 246/247 into changes in the other channels. This conservative JPEG
rule does not recover unknown local/channel response or certify all white fields.
Version it `clipped-channel-preservation-v1` in paired diagnostics; apply the same
math to ordinary and streaming paths. Preserve real single-channel colored/text
recovery, all-channel clipped text/rings, independent geometry/occlusion guards
and base-frame fusion. Test adversarial out-of-domain clipped-channel response
and noisy single-channel thresholds; compare native before/after HDR on the
provided white field, including any residual clipped-channel impulses.

Current `clippedFraction` counts pixels with a base-merge channel >= 250 **before**
highlight replacement, divided by output pixels. `recoveredFraction` counts
output locations actually changed by accepted short observations. Neither is a
post-HDR clipping metric or a guarantee of readable recovered content.
**Implemented in rc13:** the appended metric below records post-recovery clipping.
**Required / not implemented:** useful recovery in relevant text/light regions
and confidence validation in JPEG
response estimates when they differ greatly from metadata exposure ratios.
Do not force a match to either ratio without observed signal/geometry evidence.

**First HDR diagnostic follow-up (specified before implementation; implemented):** report
post-recovery channel clipping on the actual byte native-fusion grid, before any
2x resampling/JPEG encoding, alongside the existing before/recovered fractions.
Append a separately versioned two-slot extension to the 42-slot JNI packet;
old packets leave the new metric unknown, not zero. This measurement changes no
pixel/tone formula and does not certify a target ROI.

Avoid an unconditional `HDR` outcome merely because one pixel changed. Classify
no clipped baseline, no reliable recovery, limited pixel changes and partial
highlight recovery separately. The initial experimental partial-recovery label
requires >=128 changed native pixels and >=1% of the pre-clipped pixel count;
other actual changes remain recorded as limited adjustments. These are outcome
label parameters, not acceptance thresholds or reasons to discard pixels.
Neither the label nor a global clipping reduction establishes readable screen
text or restored lamp shape. Keep the requested Full HDR mode separate from the
achieved outcome and preserve reference white balance.

### Demand-bounded highlight registration (2026-10-15; rc14 implementation, device acceptance pending)

The rc13 16:19 screen capture records 26.265 s native processing, including
15.284 s local registration. Short plates fit the complete grid although HDR
consumes only native-merge pixels with any channel >230. Preserve the existing
pixel formula, physical correspondence gates and plate admission requirements.
This is a scheduling optimization, not a new tone model or a relaxed HDR gate.

For the ordinary in-memory path, finish base registration and native-size
denoising before evaluating short plates. Freeze the existing base crop and
derive a reference-coordinate byte demand mask from the actual rounded native
merge. rc15 supersedes the former >230 threshold with any channel >=247, matching
the clipped-channel recovery contract; do not substitute luma, reference-only clipping,
an estimated screen rectangle or output megapixels. The mask must not become a
geometry-validity mask or remove unclipped context from the fitting patches.

Retain every local node that the existing highlight offset lookup can consult
at any demanded pixel, including conflicting neighboring nodes and clamped
edge-node centres. The current footprint is <=140 source pixels per axis; keep
the existing nearest-node selection and 1.5-pixel disagreement rejection. Fit
each retained node using its full original patch, masks, noise and thresholds.
The bounded GPU proposal path remains subject to existing numerical/physical
checks and CPU fallback. No missing conflict node may turn rejection into
acceptance. Unrequested nodes can remain unavailable.

A pruned field's gate is a lower bound on the complete gate only at demanded
pixels (removing conflicts elsewhere need not be monotonic). For the existing
>=1024-pixel plate admission, count only demanded supported pixels as a certified
lower bound: >=1024 proves admission; zero means the plate cannot affect the
output. For 1..1023 supported demanded pixels, run the original full-node path
to decide admission. Never drop a useful small target solely because pruning
reduced whole-image coverage. Record retained/total nodes and exhaustive fallback.

Do not enable pruning in the separate streaming/native-high path until its
loading, crop, ownership and demand propagation receive independent acceptance.
Account for overlapping native byte output and demand-mask lifetime under the
shared memory reservation. Release demand/workspaces before 2x reconstruction;
avoid allocating another full-size native color output or float demand map.
Acceptance requires frozen ordinary CPU outputs identical to the exhaustive
path, including clipping/recovered counts, crop and scale; test sparse/border/
single-channel demand, conflicting nodes, admission fallback and no demand.
Actual GPU fidelity checks remain required; host timing does not certify phone
latency, thermal behavior or a new resolving-power gain.

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

### 7.1 GPU compute migration (2026-10-15; partly implemented)

The user requests faster processing in subsequent versions. Vulkan compute is
the backend for the connected debug experiments in sections 7.2–7.4; CPU remains
the release implementation and the calibrated/failed workload fallback.
The linked `vulkan_compute_engine.cpp` and embedded shader have no execution
caller in the inspected product source. Their old two-frame luma-graft/LTM
semantics omit current registration, photometric, clipping and recovery masks.
Do not activate that shader as an implementation of this contract.

The first migration preserves the current JPEG input and accepted mathematics.
It does not activate RAW, native-high capture, new LK registration or new tone
mapping. Upscan's separately gated research is not accepted merely by moving its
computation to GPU. AF/AE, capture scheduling and ISP remain SPEC_01 concerns;
JPEG decoding/encoding, queue delay and storage are measured separately.

Implement and validate in this order, with bounded experiments per stage:

1. **Profiling and backend boundary.** Add monotonic, per-capture timers for
   shutter-to-input-complete, queue, decode/reference selection, global fitting,
   local fitting and verification, native fusion/HDR, reconstruction, rotation,
   encoding and successful save. Add upload, submission, fence wait and readback
   timings for GPU jobs. Record execution backend and explicit fallback reason.
   Overlapping work must not be presented as additive wall time. Existing log
   timestamps are checkpoint evidence, not complete profiling.
2. **Bounded Vulkan infrastructure and regular image kernels.** Probe a hardware
   compute device, API/features/formats/workgroup and storage limits at runtime;
   do not select by phone model or assume optional extensions. Reuse pipelines,
   descriptor/command resources and bounded buffer pools. Start with grayscale,
   pyramids, gradients, warps, masks and batched residual/reduction operations
   consumed by current registration; keep transform selection and eligibility
   policy on CPU initially. Batch patches and return compact reductions, avoiding
   full-resolution transfers after each kernel.
3. **Registration acceleration.** Profile and port the hot iterative fitting and
   verification kernels while preserving ambiguity, noise, motion and detail
   safeguards. Keep any global optimizer/control on CPU until its GPU variant is
   separately qualified. Failed correspondence must not receive SR/HDR credit to
   make the GPU path appear faster. Compare equal patch sizes and masks as in
   ALIGN-R04, then test the full field and renderer, not only optimizer residuals.
4. **Contiguous fusion and reconstruction.** Keep admitted image/field/mask data
   resident across native-size multi-frame denoising, short-exposure recovery and
   bounded regional backprojection. Preserve output/crop provenance, original
   observations, protected highlights, per-pixel gates and source/output limits.
   Gather/reduce in bounded tiles without assuming floating-point atomic support;
   keep halos and border treatment equivalent. Native-high streaming must retain
   its reference-plus-one-candidate bound; it cannot allocate all native-high
   decoded frames to imitate the ordinary backend.

The phases describe scope, not four guaranteed releases. The profiling results
may change kernel priority, but not the quality and ownership contracts.
CPU and GPU implementations require explicit channel order, pixel type/stride,
linearization, interpolation, border and precision conventions. Establish a
versioned numerical tolerance and fidelity profile before calling a port
verified. Initial experiments use FP32 for image arithmetic; any CPU higher-
precision decision/reduction that cannot be matched must remain on CPU or
receive a separately tested equivalent. FP16 is not an assumed optimization.
Do not equate a small pixel error with unchanged punctuation or HDR eligibility.

Input validation precedes allocation: checked dimensions/byte arithmetic,
matching input geometry, safe strides and finite parameters. Unsupported memory
types must return unsupported, not silently select memory type zero. Check all
allocation/map/descriptor/command/submit results. Use explicit memory barriers
and host visibility rules, with per-job completion fences and bounded polling;
avoid a whole-queue idle wait per image kernel. No allocations or buffers may be
released while their work is in flight. Timeout/device loss disables that backend
for subsequent jobs; use a bounded completion/reset path, then re-admit CPU work
with the retained immutable inputs. If safe resource recovery is unavailable,
produce an explicit failure rather than race cleanup or commit a partial image.

GPU fallback must cover unsupported/software devices, unavailable features,
budget failure, initialization/submission/device failure and unfavorable measured
performance. Recheck the shared SPEC_00 heavy permit before CPU fallback; do not
keep full GPU and CPU working sets resident concurrently outside the reservation.
Do not repeatedly retry a failed device for every frame. Backend choice and
technical status belong in diagnostics, without adding clutter to the camera UI.
Detailed capability/device logging follows the existing test-build privacy rule.

Acceptance is GPU-01 through GPU-04 in SPEC_06. Host shader/failure and image
regressions validate the connected kernels, including CPU fallback; they do
not establish phone speedup, sustained operation or full-migration acceptance.

### 7.2 First performance implementation (specified before implementation; experimental debug integration)

The user prioritizes shutter-to-final-diagnostic readiness, then fidelity, and
requests a consolidated build after the useful performance/quality work. RAW and
restart recovery may be included when supported and beneficial; OCR, book
flattening and changes to the accepted crop workflow do not block this batch.

The first GPU kernel accelerates the full-resolution local photometric support
map for ordinary same-exposure plates. CPU retains fitting, high-pass/noise/detail
qualification and HDR patch support initially. Its input is the existing CPU
smoothed gray pair and immutable local field; its output is the same byte gate.
No new transform, SR authority, tone or white balance is granted by this kernel.
Use packed byte storage and a compact float node field, at most 16 MP, <=64 MiB
of persistent/in-flight GPU allocations, counted in the shared resource overhead.
Avoid an additional full-resolution float representation or per-pixel host packing.
Among compatible host-visible/coherent types, prefer device-local memory, then
host-cached memory, using reported properties rather than enumeration order.
Log selected flags/allocation bytes without device identifiers. Keep the same
64 MiB allocation cap and measured CPU fallback when available memory is unsuitable.

Port offset interpolation, motion-discontinuity rejection, boundary checks and
residual thresholds unchanged. FP32 cases near a motion-distance, source-boundary
or residual decision must return an ambiguity marker for CPU rechecking at that
pixel, not an automatic positive. A capability/runtime self-test must pass before
using the kernel; unknown values, mismatched dimensions, unhealthy execution or
unsupported workload use the existing CPU gate. Record backend, whole call time
(including copies and ambiguity checking) and fallback. Preserve HDR's separate
patch support path until independently qualified. This first kernel is one
performance stage, not completion of the full section 7.1 migration.

Initial activation is experimental in debug/test builds only. The first real
support map in each <=1 MP / <=4 MP / <=16 MP bucket runs a full CPU comparison;
it must match resolved bytes exactly and have GPU call time <=90% of CPU time
before subsequent maps in that bucket use GPU. Record both calibration times.
This bounded warm probe protects against obvious transfer/driver regressions;
it does not satisfy GPU-03 end-to-end/sustained acceptance. Release remains CPU
until that acceptance is independently completed.

Use per-job fences and bounded wait; on an unresolved timeout retain and disable
the bounded in-flight context rather than release it early or submit again.
An actual submission error also leaves execution uncertain; mark submission as
in-flight before the driver call and quarantine that context instead of freeing
possibly referenced storage. Known host failure before submission may clean up.
The reserved persistent GPU overhead remains accounted on the CPU path. No
fallback may publish a partial GPU result. CPU-only builds retain identical
semantics without requiring a Vulkan loader.

Per-frame native profiling separates preparation, local fit/verification and
support-map construction. Structured capture timing additionally separates
acquisition, queue/heavy-permit wait, decode, native call, rotate/encode/save and
diagnostic snapshot. Export timing separately measures log commands, snapshot
wait and ZIP writing; it must not imply the final image already exists while a
newer job is still processing. Timings are monotonic, bounded, keyed by captureId
and governed by existing test/release diagnostic privacy.

Independent bounded app/root log commands may run concurrently after worker
readiness, with separate stage timers and unchanged per-command output/time
bounds. Cancelled export must propagate cancellation and never open a partial ZIP.

CPU optimization may reuse immutable reference gray/high-pass/blur/noise data
within one ordinary burst, with matching dimensions, source identity and node
layout. Release that workspace before final fusion allocations. If every local
node lacks confidence, the photometric gate is identically zero; skip additional
blur and per-pixel lookup without granting support. Zero-orientation encoding may
read the owned immutable result directly instead of allocating a full-size copy.
Other rotations still use a separate buffer; do not mutate the master or release
the borrowed input. These optimizations preserve existing equations and source
decisions and require differential/native regression checks.

Global candidate scoring may cache immutable downscaled gray/high-pass masks,
identity correlation and an exactly identical transform's computed score within
one source/reference pair. Scope the cache to that alignment call; validate
source addresses, shape and type, and release it before fusion allocations.
Candidate order, equations, thresholds and selected transform stay unchanged.
No early acceptance threshold may be lowered to reduce runtime.

Encoded JPEG decode may allocate the same one-channel byte Mat and copy the
immutable ByteArray directly with Mat.put, avoiding Kotlin vararg-spread copies.
Do not change decode flags, orientation, source ownership or decode limits.
Test memory records may sample Java/native allocated bytes and bounded own-process
VmRSS/VmHWM. Label VmHWM process-lifetime high-water memory, not this capture's peak;
missing proc data stays unknown and release does not perform these diagnostic reads.

Per-call native profiling is part of the paired result, not a global last-job
variable or only expiring logcat. Append a versioned six-slot extension after
the 44-slot highlight packet: schema, total native time, global registration time,
local registration/validation time, published GPU support-map count and GPU fit
proposal count. Accept old 42/44-slot packets as unknown for this extension;
validate finite nonnegative times and bounded counts. Normal and streaming calls
measure nonoverlapping global/local wall intervals in their own scope; native
remaining time includes reference selection, decode in streaming, fusion and
reconstruction and must not be described as pure SR. A proposal count does not
prove a physical detail gate or pixel contribution passed. Preserve these values
under the captureId in its manifest so four-group export survives logcat rollover.

Within ordinary global candidate selection, defer full-resolution BGR warping
until the unchanged scoring/order has selected the final transform. Internal
candidate verifiers use the original pair and scoring-grid warps; they do not
need each discarded candidate's full-color output. Direct public alignment
calls retain their warped-output contract. Normalized HDR alignment may likewise
discard its unused normalized-color warp, then warp the original short once.

### 7.3 Batched translation fitting (specified before implementation; experimental debug integration)

The first native phase timers show fitting/verification dominates support-map
construction on the executed host fixtures. Prototype translation ECC on GPU,
with at most sixteen <=321x321 patches per batch and the existing 30-iteration,
1e-4 convergence, 3x3 Gaussian/input-mask preprocessing and source-gradient
semantics. Preserve the optimizer's fixed interpolation/nearest-mask convention,
bounded original observations and CPU high-pass/noise/detail/residual checks.
This ports the current ECC proposal, not Upscan's unaccepted LK replacement.

Use stable centered statistics and per-patch reductions/2x2 solves. Failed,
singular, unsupported or nonconforming GPU candidates use the existing CPU fit;
do not skip a valid CPU observation for speed. Startup/frozen known-shift tests
require <=0.05 source-pixel discrepancy for accepted CPU/GPU proposals and bounded
correlation difference <=0.002. Before experimental reuse, compare a small real
batch on the same device against CPU and include packing/upload/wait/readback in
cost. A profile with disagreement or unfavorable measured cost remains CPU.
Fitting and support kernels share one bounded context/pool; staged CPU patches
and retained GPU buffers must fit the reserved overhead together. No large
image-wide five-plane float upload is permitted. End-to-end text/ghost/noise and
phone p50/p95 acceptance remain required; an accurate patch is not that pass.

**rc14 parity follow-up (specified before implementation; experimental debug integration):** rc13's GPU solver
selected the best earlier scored pose while calibration compared it with the
CPU optimizer's returned transform after its final update. These are different
return contracts, especially for oscillating masks or iteration-limited patches.
The GPU proposal must follow the same final-update and termination convention as
the current OpenCV CPU path. Its returned correlation is an optimizer diagnostic
from the final evaluated iteration, not an independent score of the updated pose.
The existing physical high-pass/noise/detail check independently evaluates that
pose before admission. Record `ecc-return-parity-v2`; keep <=0.05 px /0.002
calibration limits and fallback unchanged. Exercise non-monotonic and limited
iterations in addition to easy known shifts. This supersedes rc13 best-pose
selection, without changing the CPU algorithm or granting recovery/SR eligibility.
The return convention is verified against the
[OpenCV 4.10 implementation](https://github.com/opencv/opencv/blob/4.10.0/modules/video/src/ecc.cpp).

An iteration-limited GPU solve is unavailable and uses the original CPU solve;
it is not a converged GPU proposal. Calibration must compare every GPU-returned
usable proposal with a usable, numerically matching CPU proposal. A missing GPU
proposal may use the already measured CPU fit, without disqualifying the other
matching GPU proposals. Include actual CPU completion of missing proposals in
the GPU-path cost, preserve it on subsequent batches, and record usable/missing
counts. A mismatching successful GPU proposal still disables that profile; this
does not loosen discrepancy limits or remove any valid CPU evidence. Calibrate
the complete <=16-patch batch rather than dispatch alone. Sixteen five-plane
321x321 patches use <=33 MiB packed storage; persistent old allocations and
staging/preprocessing must still satisfy the existing 64 MiB GPU pool and shared
host reservation. Larger batches require a revised resource/cost contract.

GPU scheduling may restrict proposals to already strongly matched two-frame
structure while leaving every other node on the unchanged CPU path. The initial
experimental selector uses the existing noise-corrected high-pass correlation
>=0.90, detail ratio >=0.68 and both signal variances >=max(2.25, four times
noise variance), with the original valid patch mask. These are GPU scheduling
conditions, never replacement acceptance gates or reasons to remove CPU evidence.
Large/multimodal residuals and noisy/weak correspondence stay CPU; no wider GPU
discrepancy tolerance is permitted. Include selector/noise preparation in timing,
cache each computed source noise estimate for the subsequent physical check,
and preserve deterministic original node ordering in GPU batches.
If a usable GPU proposal fails the independent physical detail/geometry check,
retry that node with the original CPU fit and check before dropping evidence;
reused CPU calibration fits need no duplicate retry. Count these completions in
diagnostics and complete registration time, not as GPU-admitted detail.

**Tiled ECC execution follow-up (specified before implementation; experimental debug integration):** one workgroup
per 321x321 patch serializes hundreds of samples per lane and exposes few groups
per dispatch. Split each patch into <=1024-sample chunks, with up to 128 lanes
per chunk. Keep the packed reference/source/gradients/mask resident throughout
all thirty bounded iterations. Each chunk computes local masked means and
centered moments in one workgroup; a per-patch workgroup combines them with
weighted mean-difference corrections and performs the 2x2 solve. Preserve
unmasked gradient/input contributions and masked gradient sums when changing
the centering origin; avoid subtracting large raw intensity-square sums.
This uses two dispatches per iteration, separated by explicit
compute-write to compute-read/write barriers. Record all dispatches in one
bounded command submission and wait/read back only once per batch; no CPU
per-iteration synchronization. Converged/failed patches skip later arithmetic,
and a final stage publishes only converged proposals.

The temporary state/partial reductions share the existing output storage buffer
and pool cap; initialization must precede every reuse, index every variable-size
patch safely and keep published results separate from scratch. Track complete
packing/submission/wait/readback cost, preserve timeout quarantine and CPU
fallback, and regenerate checked-in shader artifacts. Run the actual tiled
shader on known shifts, non-square/partial chunks, masks and mixed/failed batches
before phone activation. Existing numerical and physical checks remain unchanged.
Specialize the six fixed ECC stages at pipeline creation, rather than selecting
all stage branches at runtime inside one compiled kernel. Reuse the six bounded
pipelines across batches; do not compile per patch or iteration. Preserve the
same descriptor/workspace/barrier contract and include one-time creation in
actual cold-capture timing. The explicit buffer pool cap is not a measurement
of opaque driver pipeline memory, which remains covered by the shared overhead
estimate and requires device memory acceptance.

**Resident preprocessing follow-up (specified before implementation; experimental debug integration):** ECC input
packing currently repeats float conversion, Gaussian filtering, source gradients
and mask filtering on CPU for every overlapping patch. Upload packed reference/
source/mask bytes (one 32-bit word per pixel) instead. Keep the original >=256
input-mask requirement, and generate the five float planes on GPU once per batch.
The 3x3 Gaussian on float-converted bytes uses the exact binomial coefficients
and isolated patch BORDER_REFLECT_101 convention; gradient filtering follows
Gaussian completion, with the same central derivative and binary smoothed mask.
For the 3x3 binary mask, the existing 0.5/0.95 scale and rounding retain a pixel
only when all nine samples are valid. Do not erode the source image's actual
clipped boundaries beyond this existing optimizer-mask operation.

Device-written float planes, packed bytes, job headers, states and reductions
remain under the shared 64 MiB pool including previous capacities. Two explicit
preprocessing stages precede iteration reads, with compute barriers; no CPU
float-plane round trip. Host-test-only inspection must compare all five actual
GPU planes against CPU preprocessing, including masked holes, non-square/edge
patches and small masks, before accepting this optimization. Shipping builds
must not contain the inspection hook. End-to-end cost still includes CPU byte
packing, transfers, synchronization, readback and CPU completion/fallback.

Keep fitting cost profiles separate for <=8, <=16 and larger eligible workloads,
and for base/short proposals. A small CPU-preferred batch must not disable larger
GPU batches. Charge the entire selector duration to the GPU workload estimate.
At first calibration measure one cold and one reused-buffer GPU batch against
the unchanged CPU batch, validate successful proposals from both GPU runs, and
include measured CPU completion of missing proposals. A conservative workload
estimate uses the actual number of GPU batches (including the partial last batch)
and fractionally scales CPU batch cost by actual patch count. Cold GPU allocation
is charged once, warm call cost to subsequent batches; no dispatch-only estimate.
Enable only if this complete estimate is <=90% of CPU and proposals conform.
Report both measured batch costs and the explicitly estimated workload costs;
end-to-end paired/device acceptance remains independent of this policy estimate.
The estimate describes workload execution after validation, not calibration
capture latency: actual capture timing must include the extra CPU baseline and
repeated GPU validation call. Do not subtract those costs from measured latency.

**rc15 shared preparation and scheduling correction (specified before implementation):**
The phone reports four matching GPU fits in 19.661 ms versus CPU 89.299 ms, but
92.077 ms of selector work makes the profile CPU-preferred. Part of that work
estimates source noise and high-pass moments that the CPU physical path also
requires. Prepare node reference/source noise and interior high-pass variances
once for every active node, independently of backend, and reuse them for CPU
qualification and GPU scheduling. Keep the original calculations, patch margins,
active-node/cached-reference/single-patch semantics and final acceptance intact.
Report this common work in registration preparation time; charge all remaining
GPU-exclusive selector work to GPU, without subtracting it from end-to-end time.
Shared context initialization remains in actual capture timing; fitting execution
cost must not be charged initialization already shared with support-map execution.

Retain the rc14 strong selector and existing base/short and <=8/<=16/larger cost
classes. A medium-correlation experiment (>=0.70 and signal variances >=2.25)
failed frozen CPU/GPU conformance by approximately 0.18–0.60 px in otherwise
usable proposals, beyond the unchanged 0.05 px bound. It remains rejected research
and must not be enabled or weaken calibration limits for utilization numbers.
Charge the complete remaining GPU-exclusive selector duration, preserve original
ordering and include calibration overhead in capture time. Log prepared-node
count so zero GPU proposals cannot be mistaken for offloaded fitting. CPU-only
registration/reference/crop/eligibility must remain the same, apart from the
explicit HDR correction. Larger GPU migration remains unimplemented.

The optimizer mask excludes the same ten-pixel **patch boundary** already excluded
by the independent high-pass check. This prevents constant-border resampling at
an artificial ROI edge from driving the fit/score. Apply it equally to the CPU
baseline and GPU proposal, still requiring >=256 input-mask samples. It does not
erode clipped-target boundaries throughout the image or widen HDR support. Record
registration parameter version `masked-ecc-edges-v1` and optimizer margin 10;
compare qualification loss and full reconstruction against the previous contract
before release, including the existing clipped-screen regression.

### 7.4 rc16 bounded compute expansion (specified 2026-10-16 before implementation)

The rc14 phone workload still reports three GPU support maps and zero GPU fits,
with 24–25 seconds in native processing. Move additional *existing* arithmetic
to Vulkan in the debug test build; increasing utilization cannot relax fitting,
HDR compatibility, clipped-channel preservation, cropping or SR eligibility.

1. Node noise grids: upload a packed byte gray image once and compute the original
   three separations (1,2,4), step-four, patch-relative 2x2 differences in GPU
   workgroups. Integer histograms select the exact upper median, excluding values
   outside 4..251 and retaining the >=16-sample requirement. Convert the returned
   integer median to double sigma on CPU with the original divisor/cap. Active
   HDR nodes may be sparse; never change edge patch origins or sample phase.
2. Highlight support maps: extend the packed-byte kernel to the original nearest
   measured patch-interior lookup, including all nine neighbors, pairwise 1.5 px
   conflicts, <=140 px interiors, deterministic nearest tie order, reference
   validity and residual <=18. Ordinary interpolation/residual <=14 is unchanged.
   Numerically ambiguous decisions complete on CPU; resolved masks must be exact.
3. Native same-exposure color merge: process <=256x256 output tiles. Pack only
   conservatively bounded original source rectangles, gates and compact node
   fields for the admitted anchors. GPU evaluates local offset/confidence, inverse
   projective mapping, original BGR bilinear sampling and confidence-weighted
   accumulation in the original anchor order. Use tile-relative projective
   coefficients to bound FP32 error. Re-evaluate ambiguous geometry, motion or
   byte-rounding pixels on CPU. Tile output bytes and contributor counts must
   agree with the existing CPU merge; failed/unsupported tiles complete on CPU.
   No HDR tone kernel or post-HDR SR solver is substituted by this stage.

Initial merge precision envelope is <=2 absolute affine coefficients, <=1024
source ROI extent/relative origin and a normalized denominator in [0.75,1.25]
over the tile plus a conservative 16-source-pixel offset bound. Experimental
CPU completion bands are 0.003 px near ROI sampling boundaries, 0.001 near the
1.5 px motion conflict and 0.06 byte levels around a half-integer final rounding
decision. These are numerical scheduling margins, never relaxed quality gates.
Validate exact bytes with random/high-contrast/projective fixtures and actual
PPT replays; unsupported transforms complete on CPU rather than extend bounds.

These stages share the existing single mutex/context, three storage buffers and
64 MiB allocation cap, including retained capacities. After a completed fence,
reclaim incompatible retained capacities before changing workload layouts;
never reclaim quarantined in-flight work. Keep CPU staging bounded per tile,
respect the shared 96 MiB reservation and existing ordinary <=16 MP source
limit. Streaming/native high-resolution remains the original sequential CPU path.
Single-patch streaming calls do not schedule these ordinary-burst GPU kernels.
No full-frame float color accumulator or additional GPU context is permitted.

New stage profiles require actual GPU/CPU parity and complete call-cost comparison
(packing, upload, wait, readback and CPU ambiguous completion) before reuse.
Keep <=90% CPU cost activation, separate noise/base-gate/highlight-gate/merge
profiles and CPU fallback. Record actual executed stage counts/times, ambiguous
completions and calibration time; no GPU count may represent CPU fallback.
Append performance packet version 2 (54 values), retaining the old 42/44/50
decoders: indices 50/51 are published GPU noise grids/merge tiles, 52 counts
CPU-completed ambiguous merge pixels and 53 is complete GPU merge-call time.
The stage time is nested in total native time, never added as a disjoint interval.
The first calibration costs remain in capture latency. Initialization and
failure/timeout quarantine retain the existing contracts. The planned expansion
is connected in the debug implementation and passes the host/build checks in
the rc16 report. Phone latency/quality and sustained acceptance remain pending;
release stays CPU and full compute migration is incomplete.

### 7.5 rc17 scheduling and GPU selection (specified 2026-10-16 before implementation)

Implemented in the debug backend with actual-GPU, CPU parity and build checks.
The dated rc17 report records frozen-input timing and selected stages separately;
phone sustained speed and the >=30% target remain unaccepted. Release stays CPU.

The rc16 bright-PPT device capture b2219fc5 took 26.442 s in native work, including
19.850 s global/local registration. It published three GPU noise grids, three
support maps and no GPU fits/color tiles. A conforming 13-patch fit measured
75.403/48.648 ms cold/warm versus CPU 143.245 ms, but 63.219 ms of selection and
6.849 ms of an empty completion loop kept the cost profile CPU-preferred.
This supersedes earlier lifetime-only cost decisions and the cold-only execution
estimate; it does not change any image, fitting or physical acceptance criterion.

**Bounded profile reuse:** distinguish unmeasured, admitted GPU, unfavorable cost
and numerical rejection. Numerical disagreement remains blocked for that profile;
device/submission failure retains the existing whole-context quarantine. A cost
rejection may be probed again after eight eligible opportunities, with at most
three calibration attempts per process/profile. Never probe on every frame.
Use measured warm complete-call cost plus one quarter of positive cold-minus-warm
cost to estimate four compatible reused calls. Retain the <=90% CPU admission
threshold. Publish cold, warm, CPU, selection/completion and estimated costs;
all actual cold/calibration work still belongs in the real capture latency.
Profile identity separates workload size, base/highlight and fit batch size;
small work must not permanently disable large workloads. When no fitting class
is admitted, a cost rejection may suppress three subsequent expensive selector
attempts for that base/highlight scope; CPU still completes every original node.

Only launch/charge CPU missing-proposal completion when a GPU proposal is actually
missing. A zero-work OpenMP team is calibration overhead, not future completion
cost. Reuse measured CPU fits for calibration output when CPU is selected.
Shared physical checks and retry semantics remain unchanged, including <=0.05 px,
<=0.002 correlation and the rejection of medium-correspondence scheduling.

**GPU strong selector:** preserve the original CPU cheap interior-variance and
noise preconditions. Upload only candidate-patch reference/source/validity byte
planes with four-byte padding per plane; transfer the already prepared high-pass
patch pairs, <=16 at a time, directly into the shared mapped workspace. No
full-image byte upload is needed for this selector. GPU performs the original
source/reference byte mask, ten-pixel optimizer margin, masked sample count and
centered high-pass moment reductions. Keep >=256 valid samples, noise-corrected
correlation >=0.90 and ratio >=0.68. Perform final scalar decisions in double on
CPU; complete numerical neighborhoods (initial experimental bands 0.002 around
correlation/ratio thresholds, and tiny corrected variance) with the unchanged
CPU selector. Resolve every returned flag against CPU during real calibration.
Treat corrected variance below max(0.1, 10% of either supplied noise variance)
as a CPU completion neighborhood to avoid amplifying FP32 cancellation.
This kernel schedules existing ECC proposals; it grants no image support by itself.
Fitting estimates include the full selector call, transfers and CPU completions.
When the selector itself is being calibrated, report actual selector wall time
separately from its validated reused-call estimate; fitting's execution estimate
uses the latter plus all exclusive preparation. Do not charge CPU parity/repeated
validation calls as recurring selector execution, or hide them from capture time.

Two-stage <=1024-sample partial reductions and per-patch combination use explicit
barriers and one fence per batch. Patch bytes and high-pass pairs stay resident
for both stages; include each batch's row copies and padding in execution cost.
Respect the single three-buffer context, combined actual <=64 MiB allocation cap,
shared overhead and <=16 MP ordinary source limit; no full-frame float upload or
second context is allowed. Unavailable/unfavorable selection uses original CPU
flags. Count GPU-computed and CPU-completed selector patches separately.

**Representative color calibration:** validate up to three distinct tiles drawn
from the centre, first and last tile, rather than only a weak corner. Compare exact
bytes/counts, include complete tile packing/wait/CPU ambiguity cost, and estimate
the remaining image using measured warm/CPU costs per sampled pixel. Preserve
calibrated tile outputs within the existing bounded tile reservation. Unsupported
or nonconforming work uses CPU without publishing partial results.

Persist bounded backend decision details with each capture, including reused CPU
preferences whose original log lines have expired. Keep them attached to the
specific synchronous native result; never read a global last-job string later.
Performance extension v3 appends GPU selector patch count, CPU ambiguous selector
patch count and full selector call time to v2; legacy packets remain decoded.
The packet has 57 values: indices 54/55 are the counts and 56 is selector time.
Release remains CPU. Actual selected backend counts, same-input text/HDR fidelity,
complete paired timing and phone sustained acceptance remain required.

### 7.6 rc18 resident global ECC (specified 2026-10-16 before implementation)

Implemented in debug with actual-GPU optimizer, CPU-golden, full-image and build
checks. The dated rc18 report records selected calls and paired host timing;
phone sustained speed and the >=30% complete-capture target remain unaccepted.

The rc17 device group d16040ae took 32.191 s shutter-to-ready and 26.950 s
native, with 7.727 s global and 12.228 s local registration/verification. Actual
GPU local proposals increased to 46 but did not remove the main wait. Continue
with full-image Euclidean and homography ECC, rather than only routing helpers.

**Implemented contract:** keep the existing 480/1920 Euclidean pyramid,
1280 homography preparation, initialization, iteration limits, epsilon, candidate
ranking and physical/photometric verification unchanged. In this first rollout,
cross-exposure HDR global alignment remains CPU: the shifted-highlight fixture
revealed that a sub-0.05-pixel global difference can cross downstream evidence
boundaries. It requires separate recovery-equivalence qualification, not a relaxed
support threshold. Apply the new optimizer to base-exposure candidates only.
The debug GPU backend
may own Gaussian preparation, gradients, warped sampling, masked mean/variance,
Jacobian/Hessian and image projections. Keep raw bytes/prepared planes resident
through the entire optimization. Download only bounded reduced statistics each
iteration; CPU may solve the 3x3/8x8 system and update the transform. It must not
download full warped images or allocate a full-image Jacobian per iteration.
This supersedes the CPU-global-optimizer restriction only for these validated
variants; ORB, phase and independent structural/physical checks stay unchanged.

Honor OpenCV's five-tap Gaussian/reflected border, zero outside warp border,
linear interpolation grid, nearest validity mask, masked means, unmasked warped
gradients and final-update/returned-correlation semantics. Include outside-mask
warped-gradient Hessian terms and avoid raw second-moment cancellation. Preserve
reference-to-source transform direction and source-scale coordinate mapping.

Use the existing single three-buffer context and <=64 MiB actual allocation;
ordinary source admission is unchanged. Global optimization is limited to <=1920
in each dimension and <=2,800,000 prepared pixels, with >=16 KiB compute shared
memory. Unsupported hardware/layouts disable this stage without disabling other
validated kernels. Device/submission failures retain context quarantine.

Calibrate separately by motion model and prepared-resolution class against the
unchanged CPU call from the same initial transform. Both cold and warm results
must agree in correlation within 0.002 and in mapped source coordinates within
0.05 original-source pixel on a distributed 5x5 grid. No numerical rejection is
re-probed or relaxed. CPU calibration results are reused when selected; original
CPU exceptions/fallback semantics are retained. Complete packing, preparation,
iteration submissions/waits, reduction transfer and CPU solve belong to the GPU
cost; apply the existing 90% cost rule and bounded cost re-probes. Record actual
calibration latency in total time, regardless of amortized reuse estimates.

Performance v4 appends global GPU optimization-call count, iteration count and
complete adopted-GPU call time at indices 57/58/59 (60 values). The call time is
nested inside global registration, not an additional wall interval. Keep legacy
packets supported and attach model, resolution, costs and failure reasons to the
same result trace. Release stays CPU. Actual global adoption, full capture time,
frozen text/HDR fidelity and phone sustained acceptance are separate gates.

### 7.7 rc20 bounded-iteration local ECC return (2026-10-18 contract)

The rc19 device capture cce962ae took 23.505 s native, including 13.322 s local
registration/verification. Strong-fit calibration includes CPU completion for
3/16 and 9/12 missing GPU proposals. Code inspection identifies a CPU/GPU return
contract discrepancy: the CPU COUNT|EPS call can return a finite final transform
when 30 iterations expire without epsilon convergence, whereas the current
shader publishes only the epsilon-converged state. Missing GPU output can thus
force valid bounded work back onto CPU. This is not proof that all observed
missing proposals have that cause or that accepting them is physically valid.

The local GPU optimizer may publish either its epsilon-converged state or the
finite final update/last correlation after exactly the unchanged 30-iteration
limit. Preserve failure/singular/mask/offset rejection and all original physical
checks. Reaching the count limit is not itself convergence or quality approval.
Keep 0.05-source-pixel/0.002 CPU calibration, separate cold/warm/full hybrid
cost including real completion, the 90% rule, numerical quarantine and CPU reuse.
Do not broaden eligibility, disable CPU completion or relax text/HDR/SR gates.

Count-limited proposals need their own per-workload numerical admission, separate
from the existing epsilon-converged proposals. If any newly count-limited cold or
warm proposal disagrees with CPU, quarantine that optional return class and use
CPU completion for it; do not poison an otherwise conforming converged class.
Unknown count-limited classes cannot be adopted before a CPU comparison. Reuse
the same bounded calibration observations and retain all CPU results on fallback.

Test known bounded-count outputs against the same CPU COUNT|EPS call, retaining
flat/insufficient-mask/numerical-failure negative cases. Require actual GPU
execution and a bounded-count-return observation; CPU fallback alone is not a
GPU pass. Re-run complete native CPU/GPU image regressions and failure policy,
then freeze the latest and prior text/white-PPT/lamp cases for matched-output
checks and paired total native timings. Report remaining CPU work and phone
validation separately; this changes return parity, not the entire GPU pipeline.

## 8. Acceptance and limits

Apply PHOTO-01 through PHOTO-09 and CAP-01 through CAP-05 in
[SPEC_06](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md). Required cases include
colored clipped lights, white screens with dark small text, repeated text lines,
handheld parallax, partial burst failure, ignored exposure requests and identical
frames. Record baseline comparisons and explicit fallback reasons.

Executed synthetic/build checks and test APK identity are recorded in the dated
rc4/rc6/rc7 reports. They do not establish a real-camera image-quality benchmark
or competitor runtime comparison. Physical-device acceptance remains pending.

