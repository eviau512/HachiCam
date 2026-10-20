# Specification 06: Image Quality and Acceptance

> Status: Authoritative acceptance contract. Added 2026-10-12.
> Language: English.
> Updated 2026-10-15 with future GPU migration and benchmark acceptance.
>
> Quality revision contract: [2026-10-12 follow-up](research/QUALITY_REVISION_2026-10-12.md). Experimental implementation; numerical tuning and physical-device acceptance remain unverified.
> Execution status: the initial document-only revision did not run these checks. Subsequent rc4/rc5 reports record bounded synthetic/test-build verification; the full device matrix remains pending.

## 1. Purpose and evidence boundaries

The scanner must preserve readable document evidence, not merely produce a
bright image or a large output file. This contract complements subsystem
specifications with observable outcomes and explicit failure behavior.

A static APK component establishes artifact presence only. It does not establish
active runtime use, exact algorithm, quality, local execution, device support or
feature availability. Reference documents may suggest a test but cannot prove
an implementation. No competitor quality ranking is established here.

Use three separate records: observed research evidence, independently phrased
requirements, and HachiCam implementation/verification status. Only sanitized
requirements and specification differences cross the analyst handoff.

## 2. Independently owned fixture set

Before quality implementation is accepted, create and version synthetic,
self-photographed or appropriately licensed fixtures with source, permitted use,
ground truth and capture metadata. Do not reuse competitor binaries, proprietary
assets, decompiled implementation or extracted parameter tables as fixtures.

Cover:

- White/cream/dark/blank paper, printed borders and internal tables; receipts up
  to 12:1; weak edges on matching backgrounds and textured surroundings.
- Fine Latin and Chinese text, small digits and punctuation, table rules,
  faint pencil, faded thermal printing, colored ink/stamps and highlighter.
- Smooth and hard shadows, uneven mixed lighting, noise, clipped channels,
  glare, focus error, camera motion, screen/mains flicker and rolling bands.
- Flat and curved pages, occlusion, repeated lines, parallax and moving content.
- Camera and imported images, all four rotations, EXIF mirror cases,
  letterboxing, zoom/crop, different input sizes, alpha and color profiles.
- Processing pressure, obsolete results, deletion, corrupt input, save failure,
  low memory, cancellation and process death.

Each fixture must identify its expected result or allowed manual/fallback
outcome. Use the same source, geometry, output size and encoding when comparing
alternative algorithms. Keep training/tuning cases separate from final
evaluation cases where parameters are fitted.

### 2.1 Full-body character photography fixtures (2026-10-18)

**Digital artifacts created and verified; physical print/display/camera checks
pending.** Deliver two separate portrait
images, each exactly **6000 x 8000 pixels** (48 MP): Gemini / Hachimi cat girl
and GPT white dragon girl. These are standalone photography-test artworks, not
settings-banner replacements. The latest user clarification requires full-body
figures, superseding the earlier half-body choice.

Use the familiar cropped Lenna image only as a head/shoulder pose reference:
three-quarter rear-facing torso, head turned back over the near shoulder, eyes
looking toward the viewer. Extend that pose naturally to a standing, full-body
view. Do not copy the real person's face or import photo pixels into the output.
Both feet, footwear, cat ears / dragon horns, hands and tails remain visible with
margin. Maintain the selected community identities: blue-violet long hair,
cat ears/tail, prismatic star ornament and magenta/gold eyes for Gemini; white
long hair, pale eyes, two pale horns, dragon wings/scaled tail and white/silver
fantasy costume for GPT. Use the established moe anime illustration style.

The user-selected footwear is dark blue-violet **over-the-knee boots** for Gemini
and white/silver **high-heeled shoes** for GPT. The gown must not hide GPT's shoes.
Use a natural standing pose, a gentle expression and one character per file.
The user's follow-up requires Gemini's boots to read as leather, with subtle
grain, credible bending creases, stitching, panel seams and shaped reflections,
and slimmer heels than the first generated candidate. Revise only those boot
materials and heel width while preserving the scene and character. The user
approved the GPT candidate on 2026-10-18; preserve that artwork and only perform
the previously requested resolution/export processing on it.
The latest Gemini direction request mirrors her **pose orientation only** relative
to candidate v1: face/toe direction switches to the opposite side while she
retains the three-quarter back view and over-the-shoulder gaze. The atelier's
window/light side and dark furniture/curtain side stay fixed; do not horizontally
flip the entire scene. This supersedes matching the two subjects' facing sides.

The scene must offer usable detail in all four qualitative combinations:

| Tone / spatial detail | Example physical scene material |
| --- | --- |
| Bright / fine | Lace, individual feather strands, fine silver engravings |
| Bright / broad | Smooth sunlit plaster, pale silk folds and soft skin shading |
| Dark / fine | Low-contrast velvet embroidery, boot stitching, walnut grain |
| Dark / broad | Deep navy drape folds and smooth shadow gradients |

Use a coherent atelier / still-life setting with a bright side and a substantial
shadow side. Keep the dark textures visibly separated above black, with some
low-contrast detail rather than rendering the shadows as a flat black silhouette.
Use deep focus and legible materials; do not hide the test details in extensive
bokeh, haze, artificial grain or heavy bloom. Both subjects should have similar
lighting/material coverage; exact scene pixels need not match.

For this artwork set only, the initial **experimental 8-bit code-value profile**
requires at least 15% of pixels with Rec.709-weighted display-code luma Y' <=64,
at least 5% with Y' >=200, less than 1.5% with Y' <=2 and less than 3% with
Y' >=253. Annotate at least one bright fine-detail, bright smooth, dark
fine-detail and dark smooth ROI per final. A dark fine-detail ROI must have mean
Y' in 12..64 and standard deviation >=3. These are scene-distribution checks,
not calibrated scene luminance, dynamic range, sensor-noise or MTF measurements.
If a candidate misses the profile, revise lighting and repeat the affected checks.
The initial <2% near-white target was revised to <3% before final export:
Gemini v2 measures 2.40% at native size, concentrated in intentional bright
window/specular cores; GPT measures 1.83%. Retain those bounded highlight-stress
areas and the user-approved artwork. This provisional change does not classify
clipped pixels as recovered detail; fine bright ROIs must be inspected separately.
The approved source dimensions are 1086 x 1448. Use dedicated anime 4x SR to
4344 x 5792, then one high-quality resampling to exact 6000 x 8000. Record both
stages rather than claiming 48 MP of independent native source detail.
Verification found raw SR introduced extra endpoint clipping in Gemini (2.07%
Y' <=2 and 3.85% Y' >=253), versus 1.05% / 2.40% in her native source.
Before accepting that export, apply a source-guided endpoint guard: compare
against a bicubic resampling of the selected native scene; taper excess darkening
below Y'=16 and excess brightening above Y'=239 back toward source luma.
Preserve midtones, geometry and the selected boot/pose design. Record the guarded
result and remeasure, rather than silently increasing the clipping budget.

Save opaque lossless RGB PNG masters and record sRGB interpretation / profile,
actual native-generation dimensions, any super-resolution stages, final pixel
dimensions, hashes, prompts and selected reference roles under
`artwork/photography-test-2026-10-18/`. Per the 2026-10-20 repository handoff,
version only the two final PNG masters from this photography set. Preserve
native sources, previews, prompts, processing code and verification manifests
locally under ignored paths; do not delete them when removing Git tracking.
Keep attribution, rights limitations and the final-file hashes in the tracked
root `ARTWORK_NOTICE.md`. Apply the same images-only policy to the existing
settings-banner artwork directory, retaining its selected image deliverables.
This policy does not exclude authoritative specifications or the root notice.
If generation
does not produce 6000 x 8000, use the established dedicated anime SR workflow
and export exact final dimensions, without claiming native 48 MP source detail.
These invented scenes support qualitative repeatable photography comparisons;
they are not calibrated resolution/color charts or natural-scene sensor truth.
Reference-photo pixels and third-party reference files are not app resources.

Acceptance ART-FIXTURE-01: verify two independently decodable 6000 x 8000 RGB
PNGs, full-body Lenna-inspired pose and selected footwear/identities, all four
annotated tone/detail regions, measured code-value profile and complete local
source/processing records. Repository acceptance additionally requires final
images only under tracked `artwork/` paths, retained attribution and hashes,
and ignored supporting files still present locally. Physical print/display/camera capture acceptance remains
pending until those actual capture conditions are measured.
Executed result: both final PNGs decode as 6000 x 8000, 8-bit opaque RGB with
sRGB/gAMA tags; every PNG chunk CRC passes. Gemini dark/bright shares are
33.69% / 17.69%, endpoint shares 1.10% / 2.19%; GPT shares are 31.00% / 32.45%
and 0.69% / 2.60%. Both dark fine-detail ROIs pass the stated mean/variation
profile. Source images, user-approved GPT and output hashes are retained in
the artwork set; superseded Gemini source/SR candidates are distinguished.

## 3. Measurements and release gate

| Concern | Required evidence |
| --- | --- |
| Boundary | Annotated outer-boundary intersection-over-union, normalized corner error, content cut-off count and fallback rate |
| Document detail | Annotated stroke/mark retention, edge spread, merged/broken strokes, ringing and readable critical strings |
| Noise and lighting | Flat-region noise and background uniformity with text/color preservation measured separately |
| Color | Tagged color reference and patch differences; paper white and colored ink must be evaluated independently |
| Reconstruction | Matched-size reference/interpolation baseline, phase diversity, detail/alias/ghost measurements |
| Preview parity | Same recipe/source comparison at matched display scale, especially faint strokes and borders |
| Performance | Input/output size, device/build, temperature, sample count, median/p95 stage latency, peak memory and failures |
| Reliability | Final page/file state, resource release, source checksum, rejected obsolete results and recovery outcome |

Hard invariants below are pass/fail. Numerical image-quality, confidence,
performance and tuning thresholds must be recorded in a versioned acceptance
profile before implementation is called verified. They are currently
**uncalibrated**. A table of metrics without limits and completed results is not
a release pass.

OCR may be an external evaluation instrument if available; using it for testing
does not imply that HachiCam implements OCR or exports searchable text.

## 4. Priority and acceptance matrix

P0 protects document evidence/data. P1 protects usability and quality. P2 is
deferred functionality. Status refers to code inspection, not successful testing.

| ID | Priority | Scenario and acceptance | Current status / owner |
| --- | --- | --- | --- |
| CAP-01 | P1 | Static but defocused/clipped page: motion steadiness must not claim complete readiness; quality evidence is separate and manual capture remains possible | Motion state implemented; combined quality profile absent; SPEC_01 |
| CAP-02 | P0 | Unsupported/ignored manual exposure, missing metadata or dropped frame: retain correct tier identities, use actual evidence, fall back visibly and restore camera controls | Substantial safeguards implemented; device verification pending; SPEC_01 |
| CAP-03 | P1 | Mains lamps and emissive screens: measure banding, clipping and readability across supported control policies; do not invent universal settings | Test/profile gap; SPEC_01 |
| SCAN-01 | P0 | Outer sheet plus inner table: select annotated supported outer boundary or explicit manual fallback; never silently remove annotated content | Heuristic detection present; robust ambiguity contract incomplete; SPEC_03 |
| SCAN-02 | P1 | Blank/weak/dark/long/partly visible page: include stated supported class or expose uncertain/manual outcome, with declared score limits | Partial heuristics; confidence/fallback interface incomplete; SPEC_03 |
| SCAN-03 | P0 | Change scene, orientation, zoom or candidate: expire incompatible results and do not project a stale quad onto a new capture | Freshness and common-crop mapping implemented/tested in pure math; full device/zoom mapping unverified; SPEC_03 |
| SCAN-04 | P2 | Future auto capture: require fresh boundary and quality evidence, dwell, release hysteresis, duplicate suppression and manual override; one acquisition per eligible event | Not implemented; SPEC_03/05 |
| GEO-01 | P0 | Synthetic corners through view/analysis/source/output, rotations and mirror: round-trip error at most one source pixel; exclude letterbox taps; saved crop matches indicated content | Common rotations/FIT_CENTER implemented; full crop/zoom matrix unverified; SPEC_01/03 |
| GEO-02 | P0 | Nonfinite, crossed, duplicate, collapsed or excessively large quad: reject before warp allocation and preserve last valid page without crash | Validation incomplete; SPEC_03/04 |
| GEO-03 | P1 | Known-ratio chart and curved page: measure aspect/straight-line error; free mode must not claim calibrated ratio; unsupported curvature uses original/manual fallback | Planar warp and heuristic ratio present; automatic dewarp absent; SPEC_04 |
| FILT-01 | P0 | A -> B -> Original and repeated edits: source checksum is unchanged and Original recovers the same geometry-corrected appearance within declared encoding limits | Source-based rendering partial; crop rotation violates full immutability; SPEC_04 |
| FILT-02 | P0 | Faint pencil, stamps, highlighter, digits and fine rules under shadows: no annotated critical mark may disappear, merge or be invented; quantify uniformity and preservation together | Filters exist; evidence protection acceptance unverified; SPEC_04 |
| FILT-03 | P1 | Same recipe across thumbnail/preview/final sizes: annotated stroke decisions and crop must agree at matched scale; parameter units/border policy are recorded | Full scale-aware parity not established; SPEC_04 |
| FILT-04 | P0 | Paper retains continuous tones and color; removed B&W/Grayscale cannot be selected; future binary output needs a distinct contract | Three-filter dispatch implemented; device visual acceptance pending; SPEC_04/05 |
| FILT-05 | P1 | Alpha, wide-gamut, grayscale, tiny/uniform and corrupt inputs: deterministic declared conversion or explicit rejection, finite statistics and safe resources | Format normalization partial; explicit profile contract incomplete; SPEC_01/04 |
| PHOTO-01 | P1 | Early blurred frame plus sharper later frame: future reference selector must improve chosen evidence and use deterministic fallback when quality is unknown | Experimental quality ranking implemented; synthetic blurred-first regression; device verification pending; SPEC_02 |
| PHOTO-02 | P1 | Known subpixel phase versus identical frames: report resolving gain separately from size/sharpness; identical evidence cannot be labeled restored detail | Sampling reconstruction experimental; end-to-end gain unverified; SPEC_02 |
| PHOTO-03 | P0 | Single-channel clipping, useful short exposure and all-clipped input: recover only observed supported signal; absent detail remains absent and fallback is explicit | Recovery experimental; device quality pending; SPEC_02 |
| PHOTO-04 | P0 | Repeated text, parallax and motion: rejected local evidence must not duplicate/change annotated strokes; reference fallback is permitted | Global and sparse local translation/residual gating implemented; physical-device stroke evidence pending; dense alignment absent; SPEC_02 |
| PHOTO-05 | P1 | Photos, colored art and document white: natural-tone output must not silently apply paper whitening or a forced cinematic curve | Separate future profiles absent; SPEC_02/04 |
| CAP-HEADROOM-19 | P0 | Dark high-gain gray/black background with clipped white strokes: one bounded JPEG probe may trigger SPEC_01's verified lower-product four-anchor group; retain gaps, focus/AWB, normal bright-paper behavior and actual exposure evidence. No mixed probe/replacement anchors, fabricated detail, relaxed geometry/SR gates or guaranteed 50 MP. Missing/ignored controls, failed clipping check and session changes restore owned controls or fail explicitly. Record complete added acquisition cost and preserve adopted probe outside fusion frames in the existing capture group | Experimental implementation; pure-policy and archive regression pass; physical-device readability, flicker, controls/decoder and latency acceptance pending; SPEC_01/02 |
| LOAD-20 | P0 | Per-capture sampled app CPU, device CPU and supported device GPU busy ratio with units/provider/stage/freshness, null missing values, bounded shared lifecycle/overhead and immutable same-group export; release does not probe or retain load | Implemented in rc20; CPU/parser/trace identity/archive/privacy unit checks and host helper stop check pass. Android/root lifecycle, physical-device counter coverage and sampling overhead remain pending; SPEC_00 |
| LOAD-21 | P0 | 50 ms target process/device CPU and supported GPU read cadence, raw CPU counters and independent device intervals, no fabricated driver resolution, 250 ms freshness, 500 ms cached thermal status, 6000 rows / 2 MiB export and bounded helper | Implemented in rc21; counter/retention/export/privacy checks and host helper cadence/stop pass. Actual Android cadence, root/provider coverage and sampling overhead remain pending; SPEC_00 |
| GPU-ECC-20 | P0 | Local GPU COUNT\|EPS return matches CPU final-update/count-limit semantics without relaxing calibration/physical gates; actual bounded-count GPU positive and failed/flat/masked negatives, complete-image fidelity and paired native cost required | Implemented in rc20; actual GPU bounded-count parity and full synthetic HDR/SR/failure regressions pass. Frozen real-image comparisons and paired native timing recorded in rc20 report; device sustained latency remains pending; SPEC_02 section 7.7 |
| LIFE-01 | P0 | Rapid A/B filter changes, rotate, navigate and delete during work: only the latest valid page generation publishes; no deleted-page resurrection or native use-after-free | Burst deletion protection partial; full edit generation/ownership incomplete; SPEC_00/04 |
| LIFE-02 | P0 | Encoding/full-disk/commit failure: preserve source and last valid result; no READY pointing at nonexistent output; pending/failed export is explicit | Capture transactions partial; crop/review writes incomplete; SPEC_01/04 |
| LIFE-03 | P0 | At admission limit or memory pressure: reject/degrade visibly and release resources; recover committed pages after restart and fail interrupted captures explicitly; automatic pending replay requires a separate exactly-once contract | rc10 shared estimates/three-job admission; rc13 committed metadata/checkpoint pins; automatic replay and device/RSS acceptance pending; SPEC_00/01 |
| UI-01 | P1 | Displayed modes and achieved output agree; no unsupported Photo/dewarp/bilevel/GPU/50 MP guarantee; filter selection does not change crop | Three filters implemented; copy/workflow verification pending; SPEC_05 |
| UI-02 | P1 | Uncertain crop or failed enhancement: visible manual correction, source comparison and last-valid image; never silent content deletion | Controls partial; transactional comparison workflow incomplete; SPEC_05 |
| UI-03 | P1 | Steady, focus acknowledgment, requested HDR and processing progress are distinguishable; no one green state implies all quality criteria passed | Steadiness separation implemented; full quality guidance absent; SPEC_05 |
| DEL-01 | P0 | Record immutable APK name, bytes/hash, ZIP CRC, signature and alignment before delivery. Sender success and receiver integrity/install are separate outcomes. A reported corrupt receipt requires a distinct retry filename and rechecked source; do not mark installation or usable receipt passed without receiver evidence | rc13-0538 local CRC/signature/ZIP alignment/identity and copy hash pass; sender acknowledged 2026-10-15 09:48:18 +08:00; receiver hash/install pending; native page-size limits tracked separately under DEL-02; SPEC_00 section 6 |
| DEL-02 | P1 | Separate ZIP entry alignment from every bundled 64-bit native LOAD/RELRO check and 4/16 KiB runtime verification; transitive prebuilt failure prevents a full 16 KiB compatibility claim | rc13 ZIP and own 64-bit engine LOAD alignment pass; OpenCV 4.10 64-bit LOAD is 4 KiB and several libraries have unqualified RELRO boundaries; full 16 KiB support and device runtime acceptance pending; SPEC_00 section 6 |

Additional rc4 fixtures (see the linked quality revision):

| ID | Priority | Scenario and acceptance | Current status / owner |
| --- | --- | --- | --- |
| SCAN-05 | P1 | Jitter, alternating nested targets, missing/invalid quads and persistent changes cannot cause immediate target jumps; stale evidence expires | Kotlin tracker regression; device tuning pending; SPEC_03 |
| GEO-04 | P0 | Asymmetric source crop and reference translation through 0/90/180/270 rotations at scales 1/2 retain pixel centers within one output pixel | Kotlin mapping regression; end-to-end CameraX mapping pending; SPEC_02/03 |
| PHOTO-06 | P0 | Two-depth text motions, blurred first frame and occlusion: retain useful multi-frame support, improve local residuals and reject unsupported text; crop per edge <=5% | Production C++ synthetic tests; mobile/real-burst acceptance pending; SPEC_02 |
| FILT-06 | P0 | Continuous faint marks, color, dark paper, tiny/channel variants and unchanged input master | Production math/image synthetic tests; real mark/parity acceptance pending; SPEC_04 |
| UI-04 | P1 | Crop, single and batch review all expose Original/Magic/Paper only | Code/resource verification and build; device UI acceptance pending; SPEC_05 |
| UI-06 | P1 | Hachimi settings ends with a bundled approximately 16:9 GPT dragon-girl / Gemini cat-girl banner after the About/log-export card; entire image, ears/horns/faces/hands remain visible, decoration has no click or accessibility action, and standard settings has no blank banner space | Artwork inspected; both debug flavors compile/resource checks pass; bottom placement code verified; physical-device layout pending; SPEC_05 section 4.1 |

Historical artwork resolution verification (before the rc8 native-4x request):

- The earlier enhanced banner master targets 3840 x 2160, is saved
  separately from the 1672 x 941 version, and records its actual dimensions and
  enhancement method. Visually compare faces, ears/horns, expressions and the
  embracing hands with the selected scene. An unachieved size or changed detail
  must be reported accurately. Passed: 3840 x 2160 export and native 6688 x 3764
  result dimensions/hashes checked; visual comparison passed. That verification
  retained the earlier runtime image at that stage. The current native-4x WebP
  runtime contract is UI-07 below and SPEC_05 section 4.1.

## 5. Required result record

rc8 additional acceptance ([contract](research/DETAIL_RESOLUTION_REVISION_2026-10-13.md)):

rc9 follow-up ([contract](research/NIGHT_HDR_REVISION_2026-10-14.md)):

- CAP-08: shutter AF is bounded and held through the burst; only fresh stationary
  focused metadata grants manual distance lock; cleanup restores continuous AF.
  Deep gain-first planning respects ranges and actual full exposure evidence.
- PHOTO-11: clipped lamp-ring dark structure with independently verified context
  survives in normal/streaming paths; existing all-clipped, occlusion, midtone,
  noise, phase and crop protections pass. Real-device quality remains pending.
- UI-08: no separate zoom presets or capture/output/native/SDR-HDR captions in
  the viewfinder; circular real-view selectors remain usable. rc12 removes the
  EV button/menu per the user's subsequent request.
- DIAG-01: bounded latest completed burst and matching manifest, atomic replacement
  and ZIP export, original-photo label, previous snapshot survives failure,
  and release retains/exports no input photos. The 2026-10-14 10:07 diagnostic ZIP
  contains seven inputs and a matching latest completed result; decoded pixels
  match supplied P002. Replacement/failure/privacy device cases remain pending.
- PHOTO-12 (P1): successive 1x/2x reconstruction decisions must explain every
  eligibility check independently and preserve source/scale/crop/rotation identity.
  Current rc9 logs explain the observed 36.76% versus 66.54% mean-support outcome
  but overload `phases2D` and omit a complete structured denial record. That
  rc11 records a schema-versioned regional decision in the paired manifest/journal;
  helper/native checks passed, device acceptance pending. Stable output size is not a quality gate.
- PHOTO-13 (P1, rc11 experimental implementation; native chart/negative checks passed): text-ROI eligibility versus global
  coverage. The paired 13:25 screen burst fails the 0.50 global gate at 0.4873;
  host production probes show 72..91% support in the inspected text ROI and a
  passing centre phase heuristic. Validate distributed tile phases, strokes,
  aliases and noise on independent fixtures; include usable text with invalid
  background and invalid text with usable background. Unsupported regions retain
  a valid reference estimate. Centre phases and output MP alone are not passes.
- CAP-09 (P1, rc11 implemented; policy tests passed, device checks pending): record
  selected shutter tap/centre, monotonic tap age and session identity separately
  from AF acknowledgment/held distance. Taps persist beyond the old three-second
  window until geometry/session invalidation; no unrecorded claim that
  a confirmed user tap was reused or overwritten. Target-specific optical quality
  remains a separate device check.

rc11 acceptance contract (specified before implementation; ongoing device acceptance):
- PHOTO-12/13: regional eligibility uses distributed mappings and actual pixel
  qualification. Synthetic known two-dimensional phases with valid text and
  invalid background may qualify; identical/collinear phases, independent noise,
  blur/occlusion and denoise-only support cannot grant resolution credit.
  A valid background cannot qualify an invalid target. Qualified tile fallback
  retains a valid native estimate, including sparse holes and motion boundaries.
  Report actual reconstruction fraction separately from file scale. Compare a
  independently rendered high-resolution chart against same-size interpolation;
  numeric output dimensions alone are never a detail pass.
- CAP-09: taps remain the shutter target beyond 3 seconds in unchanged geometry;
  invalid taps, clock rollback, old sessions, zoom/layout/orientation changes do
  not select stale points. Test the pure target policy and inspect production
  routing. Device AF execution/optical focus remains separate from target choice.
- DIAG-02: versioned native packet round-trip preserves per-job denial flags,
  region counts/fractions and achieved scale; paired manifest/journal identify
  requested reconstruction, memory decision and selected shutter target without
  adding viewfinder technical captions. Release does not retain these records.
- ARCH-01 (P0, rc10 implemented; helper tests passed, device pending): crop/review rotations and four
  rotations followed by cancel/reopen preserve master checksum and consistent
  geometry. Latest recipe generation wins across filter/aspect/rotation/batch
  work; stale completions cannot replace result bytes or reset newer fields.
- ARCH-02 (P0, rc10 implemented; transaction tests passed, Android storage cases pending): false encoder result, full disk, null
  destination/stream, deletion during render/export and a failed input page do
  not publish ordinary success. Preserve the previous valid rendering; reject
  export or explicitly report partial page counts and roll back unfinished items.
- ARCH-03 (P1, rc10 experimental admission implemented; measured RSS pending): normal-size inputs with scale=2,
  multiple queued bursts plus review/export, and canceled work obey a shared byte
  budget. Record dimensions, stage RSS/heap and allocation ownership; no consumer
  failure may leave a silently accepting queue with permanently waiting pages.
- ARCH-04 (P1, rc10 implemented; journal tests passed, UI/device cases pending): start B while A completes/exports;
  bundle A has A's manifest and matching per-job evidence, never an unlabeled B
  snapshot. Late edge/line results cannot replace new-page/manual geometry.
- ARCH-05 (P0, rc10 guards/validation implemented; native geometry tests passed, JNI injection pending): malformed geometry/type/dimensions and
  recoverable OpenCV failures at every connected JNI entry point produce managed
  failures and release buffers. Native pointer corruption/process faults are not recoverable exceptions.
- ARCH-06 (P1, rc10 acknowledgment/rebind implemented; device failure injection pending): failed/timed-out base focus/AE/AWB apply or
  cleanup has explicit session-bound state; reused capture cannot silently assume
  restoration. Test acknowledgment failure separately from actual ignored settings.

Architecture scope and static evidence are recorded in the
[2026-10-14 architecture audit](reports/ARCHITECTURE_AUDIT_2026-10-14.zh-CN.md).
The [rc10 refactor report](reports/RC10_ARCHITECTURE_REFACTOR_2026-10-14.zh-CN.md)
records implemented protections and executed checks separately from device passes.

Refactor-specific acceptance (specified before implementation, ongoing device checks):

- Execute controlled old/new completion orders, filter+aspect composition,
  failed encode/rename, delete/re-add, cancellation before publication and reader
  retirement using the same production state/commit helpers as the app.
- Verify four quarter-turns and inverse source/display mapping at pixel centres,
  portrait/landscape aspect reciprocity, cancel and source hash preservation.
- An export pins all selected committed files before creating destinations.
  Missing/undecodable inputs and null/failed destination operations fail the whole
  requested export; no silently skipped page. MediaStore items stay pending until
  all content writes complete. Failure rolls back owned destinations; rollback
  failure is explicitly an error with possible partial items, never normal success.
  A zero-row delete counts as cleanup only if a follow-up query confirms absence;
  a surviving row or uncheckable destination is reported as incomplete cleanup.
- Later page deletion may finish an already leased export of its starting snapshot;
  it may not remove files mid-read. No restart recovery or multi-URI atomicity is
  claimed. Test transaction write/publish/rollback errors with a fake gateway.
- PHOTO-02 / PHOTO-07 device evidence: the same-burst P002 reference/result pair
  supports preliminary texture/noise and stroke inspection at matched scale.
  It does not certify restored resolution, OCR accuracy, night quality or a
  calibrated noise reduction percentage. See the
  [daylight evidence report](reports/RC9_DAYLIGHT_ANALYSIS_2026-10-14.zh-CN.md).

- CAP-06: grouped same-view endpoints retain different ordinary/high JPEG
  capabilities; native selection prefers a supported variant within budget and
  excludes maximum-pixel-mode-only sizes. Unsupported fallback shows actual MP.
- CAP-07: timestamp matching is exact and bounded; bracket planning preserves
  observed post-RAW boost/AWB lock and does not mix sensor and EXIF products.
- PHOTO-10: projective text alignment chooses stronger structural evidence than
  the first accepted Euclidean candidate; existing negative/phase/noise gates pass.
- UI-07: native 4x WebP decodes to identical RGB, preserves full composition at
  the settings bottom and uses bounded asynchronous display decoding.

These requirements do not certify phone high-resolution support or soft-text quality.

Additional rc6 checks ([contract](research/NOISE_REVISION_2026-10-13.md)):

Additional rc7 checks ([contract](research/CAMERA_HDR_REVISION_2026-10-13.md)):

- CAP-05: four back endpoints representing two fields of view yield two view
  choices; keep distinct tele/ultra-wide views, omit unknown auxiliaries, retain
  default and separate zoom. Test actual device grouping and bind fallback.
- PHOTO-09: run the production normal and streaming pipelines on clipped text
  with useful short exposure and a small supported bright region; require actual
  recovered pixels, retained midtones and no false recovery for ignored exposure,
  all-clipped/flat evidence or displaced/occluded content. Record pixel coverage.
- UI-05: SDR preview and HDR achieved outcome are distinguishable in diagnostics;
  rc12 removes the manual EV menu and its advice; long status labels cannot overlap shutter or
  view controls in either window orientation or with OS rotation locked.

These are acceptance requirements, not claims of physical-device verification.

| ID | Priority | Scenario and acceptance | Current status / owner |
| --- | --- | --- | --- |
| PHOTO-07 | P1 | Independent-noise sparse text/flat shadows: retain usable native-size multi-frame support, reduce measured noise without losing strokes, and report actual coverage | Production synthetic noise/subpixel/sparse regressions pass; rc6 real burst pending; SPEC_02 |
| PHOTO-08 | P0 | Noise-only, correlated/identical noise, blur, sparse masks and occlusion: no false SR or copied unsupported detail; noisy reference cannot win merely through grain energy | Production synthetic checks pass, including noisy blur and 18.1 MP streaming; real ISP/correlated-noise limits pending; SPEC_02 |
| CAP-04 | P1 | Select exposed back camera/zoom, reject unsupported capture size, switch/reset, failed binding and capture-time lock: UI and achieved geometry agree | Implemented/compiled; pure policy/reset tests pass; actual lens/size/peak memory verification pending; SPEC_01/05 |

Additional rc5 boundary checks ([contract](research/BOUNDARY_REVISION_2026-10-12.md)):

| ID | Priority | Scenario and acceptance | Current status / owner |
| --- | --- | --- | --- |
| SCAN-06 | P0 | Stacked sheets/clip/curl/right edge outside preview: show supported partial suggestion or manual guidance; never apply an extrapolated quad as an automatic crop | Experimental native fixtures, screenshot tuning and tracker tests pass; capture/import/re-detect guards inspected; device UI/crop pending; SPEC_03/05 |
| SCAN-07 | P0 | Desk-sized hull, uniform field and opposing side polarity: reject unsupported document proposals; complete blank/dark/receipt pages remain eligible | Production C++ synthetic fixtures pass; mixed-background recall and field accuracy uncalibrated; SPEC_03 |
| SCAN-08 | P1 | Test logs separate raw miss, tracker acquisition/dropout, partial/complete and shutter fallback with bounded output | Implemented behind test-build diagnostics flag; debug/release compile/privacy tests pass; rc5 device log pending; SPEC_03 |

rc12 target-exposure acceptance (specified before implementation; helper/build checks passed):

- CAP-10 (P0, implemented rc12; physical-device acceptance pending): shutter AF/AE keeps the selected point and disables auto-cancel;
  fixed-focus AE remains usable. AF success does not imply AE convergence. Require
  the fresh paired metadata contract in SPEC_01 before requesting AE lock; stale,
  changed-session/region, searching, already-locked and varying exposure/boost
  observations cannot certify settled target metering. Timeout remains bounded
  and preserves target regions without a false success claim.
- DIAG-03: paired evidence separates before-action preview, acknowledgment,
  convergence/timeout, lock requested, and exactly matched JPEG AE state/lock/
  compensation/regions/exposure. Null remains unknown. Pure metadata tests do not
  certify device AE regions, preview brightness, flicker or optical readiness.
- UI-08: no EV button/menu or lower-EV retake advice in either UI flavor/language;
  real circular lens controls, HDR brackets and capture-time switching guards remain.
- PHOTO-14 (P0, research/required): same-device small bright slide/dark background
  ROI comparisons for base, short and final detail/color/clipping. Whole-frame
  JPEG response gain must not be treated as calibrated screen radiometry. Record
  target short support and source limits before any response-model change; do not
  lower alignment/occlusion gates or force sharpening solely to accept these frames.
  The rc11 sample's source/short comparison does not establish a validated new
  tone model. The other two user-accepted scenes are regression references.

Document alignment research acceptance (2026-10-14; offline candidate only):

- ALIGN-R01: independently sampled known subpixel translations must report
  endpoint error and acceptance together. Compare robust block/LK and ECC fits
  under identical input/support; an optimizer-only comparison cannot certify
  improvement over the complete production gate. Exploratory target: accepted
  translation p95 <=0.15 source pixels; this is not a calibrated release limit.
- ALIGN-R02: flat areas, single-direction edges, periodic grids, corrupted or
  clipped correspondence, and mixed local motions must expose ambiguity or lack
  of support and receive no unsupported SR credit. Record rejection reasons and
  valid-detail loss as well as false acceptance. A reference-derived fallback
  must preserve punctuation, thin rules and faint marks.
- ALIGN-R03: phase-qualified end-to-end chart/held-out real-burst comparisons
  must improve resolved detail against matched single-frame interpolation while
  retaining strokes and bounded ghosts/noise. An improved residual, fitted flow
  or accepted area alone is insufficient. Measure Android p50/p95 capture-to-ready,
  per-stage time, peak memory and fallback; host patch timings are not phone timings.

- ALIGN-R04 (2026-10-15, offline review implemented; product acceptance pending): evaluate
  exact 321x321 production-node patches and a frozen shared valid mask. Distinguish
  original production, shared-mask production refit, new proposal, and proposal
  surviving existing physical detail/residual gates. Record support loss,
  rejection cause and host timing; do not label a small accepted subset better
  resolution without truth or an original-observation end-to-end comparison.

The [Main review](reports/UPSCAN_REVIEW_2026-10-15.zh-CN.md) reproduced 53/61
synthetic LK positives and completed the 321-pixel shared-mask comparison;
post-detail counts are 15/27 for both production and LK. This completes the
bounded review, not ALIGN-R03 or product acceptance. No real-error, phone timing,
full-field ghost/noise or restored-stroke pass is recorded.

For each executed case record fixture revision, application build, device/camera,
recipe and algorithm version, observed metadata, expected outcome, measured
outcome, pass/fail, fallback reason and residual limitation. Include comparative
source/output crops where permitted. Tests not run must be marked not run.

GPU backend acceptance (2026-10-16; partly implemented, device acceptance pending):

- GPU-01 (P0, fidelity): frozen same-burst CPU/GPU replays must record transforms,
  masks, accepted tiers, crop/scale, actual corrected/recovered pixels and matched
  source/output ROIs. Version numerical tolerances before implementation acceptance;
  preserve discrete rejection/provenance invariants. Include thin Chinese/Latin
  strokes, punctuation, faint marks, periodic grids, noise-only/identical inputs,
  clipping, parallax and occlusion. No unsupported recovery/SR or erased valid
  marks; optimizer accuracy or output megapixels alone cannot pass this case.
- GPU-02 (P0, resources/failure): run unsupported/software device and capability,
  budget/allocation/map/submission failure, timeout/device loss, cancellation and
  changed-generation cases. Verify bounded cleanup, re-admitted CPU fallback,
  source checksum, no stale/partial output commit and READY only after save.
  Include ordinary 12 MP with 2x output and native-high streaming separately.
- GPU-03 (P0, timing): record at least ten paired replay runs per representative
  scene/mode/backend, separate cold start and warm runs, interleave backends and
  record throttling/thermal conditions, peak process memory and accounted GPU/
  staging allocations. Measure p50/p95 processing and live shutter-to-ready
  separately, including upload, synchronization, readback and saving. GPU query
  times are optional when unsupported; host monotonic completion timing remains
  required. A provisional engineering goal is >=30% lower processing p50 with
  no processing p95 regression against the equal-quality CPU path. This goal is
  experimental, not a measured result or release SLA; publish variance and all
  results rather than selecting one fast run. Default enablement also requires
  improved end-to-end latency beyond observed run variability; otherwise retain
  CPU for the affected workload/device capability profile.
- GPU-04 (P1, sustained operation): repeated capture/edit/export under the shared
  admission contract must remain responsive without unbounded allocations or
  orphaned jobs. Record at least ten consecutive jobs, RSS/thermal trend and
  failures, including concurrent camera preview; one cool first run cannot certify
  sustained speed, power efficiency or broad device support.

The [GPU assessment](reports/GPU_ROADMAP_2026-10-15.zh-CN.md) records current CPU
execution and incomplete phone checkpoint timings at its earlier baseline.
rc13 connects two debug kernels and executes host primitive/failure/full-image
regressions. rc16 adds node noise histograms, highlight support and bounded native
color merge, with actual shader/CPU parity and failure checks. GPU-03/04
phone/end-to-end/sustained acceptance remains pending;
release remains CPU and a warm calibration does not replace those criteria.

First support-map implementation acceptance (specified before implementation):

- GPU-GATE-01: compare the actual shader and CPU gate for non-multiple dimensions,
  padded/ROI row strides, fractional offsets, unsupported nodes, 1.5-pixel motion
  discontinuities, near-boundary samples and residuals around 14. Ambiguous pixels
  must be CPU-resolved; resolved byte gates must match on these frozen cases.
  A device startup self-test does not replace large actual-burst/mobile timing.
- GPU-GATE-02: no Vulkan loader, software device, initialization/allocation/bind/
  map/submission/wait failures yield explicit CPU fallback. Test the disabled
  build and actual hardware path separately; CPU regressions do not prove shader
  execution. Timeout quarantine stays within <=64 MiB and rejects new GPU work.
- PERF-01: timing belongs to its capture/export identity and never a global last
  job. Include failed/fallback stages and distinguish final page readiness,
  retained diagnostic readiness, log command time and ZIP work. Publish paired
  metadata and actual final result together; earlier input-only or cached results
  cannot claim the current final-image diagnostic is ready.
- DIAG-04: four consecutive completed groups export distinct matching inputs,
  manifests, contexts and final images. Exercise byte/count eviction, legacy
  latest-only archive migration, failed replacement/export, disabled release and
  concurrent snapshot/export. Verify the latest remains valid on failure and
  the combined steady-state retained data stays <=192 MiB; ZIP timings and IDs
  must describe the actual exported selection. Worker-idle waiting includes
  snapshot completion, is cancellable/bounded and does not export an older group
  as a newer failed or still-processing capture.
  rc14 layout acceptance: latest and history use `captures/<captureId>/` only,
  with no `capture/` alias or duplicate ID; newest duplicate wins. Check a
  repeated-ID cache, single-group export and concurrent replacement, keep
  input/manifest/final pairing, and verify footer latest ID and actual paths.
- HDR-DIAG-01: a single changed pixel with no clipped baseline cannot receive a
  partial-recovery label; weak, zero/unknown and substantial changes remain
  distinct. Versioned appended post-HDR fractions validate finite [0,1] values;
  legacy packets remain supported with unknown post clipping. Check normal and
  streaming native byte-grid clipping against the returned scale=1 image,
  including all-clipped/no-usable-short evidence. This diagnostic is not a new
  tone model or an HDR image-quality pass.
- HDR-COLOR-01 (rc15, specified before implementation): noisy white PPT fields
  with one clipped channel and incompatible scalar JPEG response cannot rescale
  unclipped channels. Those channels are exact native-merge invariants; no-clipped
  pixels retain native fusion. Verify adversarial high-radiance clipped channels,
  clipping-boundary noise and actual pre/post-HDR white-field impulses, while
  retaining supported colored single-channel text and all-channel clipped holes.
- GPU-FIT-03 (rc15, specified before implementation): CPU/GPU share each active
  node's noise/moment preparation once. Test cached/uncached reference and single
  patch behavior, original strong scheduling, insufficient/missing proposals and
  disabled CPU-only builds. Keep 0.05 px /0.002 and physical guards. Report full
  latency and GPU-exclusive selector cost separately from common preparation;
  phone GPU proposal counts and p50/p95 remain separate acceptance.
  The medium-correlation scheduling experiment failed frozen conformance and
  remains disabled; tests or timing must not imply it is a shipped optimization.
- PERF-HDR-01 (2026-10-15, required before implementation): derive demand from
  the actual native merged byte image (rc15: >=247 in any channel, superseding >230). Retain all nodes
  the existing highlight lookup could consult, including conflicting neighbors
  and non-multiple edge centres. Compare exhaustive/pruned offsets and gates
  at demanded pixels, and run full-node admission for a nonzero demanded support
  count below 1024. A gate count outside demand is not a certified lower bound.
  Empty demand skips short work without changing output; sparse, single-channel,
  clipped-hole, occluded and border fixtures preserve recovery/rejection and
  actual metrics. Frozen ordinary CPU replays must remain byte-identical with
  the exhaustive contract; streaming remains unchanged. Record retained-node
  work and full-stage timing; no shutter-speed claim from host timings alone.
- GPU-FIT-01: actual shader known shifts, non-square/edge patches, masks, flat
  and singular/periodic/corrupted cases must expose usable/failed proposals and
  CPU fallback separately. Compare <=0.05 px and <=0.002 correlation only where
  both optimizers are usable; no failed GPU proposal may silently remove a valid
  CPU fit. Repeat current full physical detail gates and reconstruction regression,
  and benchmark complete batches rather than dispatch alone.
- GPU-FIT-02 (2026-10-15, required before implementation): CPU and GPU use the
  same final-update return convention and unchanged convergence/numerical limits.
  Include textured/periodic, masked and non-monotonic/iteration-limited patches,
  not just clean shifted noise. Optimizer correlation remains a diagnostic;
  actual returned transforms still pass the independent physical detail checks.
  Report real-batch conformance and complete CPU/GPU costs before activation;
  replacing best-pose selection must not hide failed or CPU-only workloads.
  Iteration-limited/missing GPU fits complete on CPU; every successful GPU fit
  still requires <=0.05 px /0.002 agreement. Test <=16-patch mixed batches and
  include CPU completion of missing fits in calibration cost and fallback counts.
- LIFE-RECOVERY-01: restore page order and committed recipe; truncated metadata
  uses the previous validated checkpoint, traversal/oversized/nonfinite metadata
  is rejected. Missing/corrupt masters become FAILED; incomplete captures never
  stay PROCESSING. Missing derivatives retain the readable master. Concurrent
  superseded persistence/edit/delete retains the durable files until commit;
  failed writes warn visibly and never delete the previous checkpoint's image.
  Process-death device acceptance remains separate from file-store unit tests.

- GPU-COMPUTE-16 (specified 2026-10-16 before implementation): execute actual
  Vulkan node histograms, highlight gates and tiled native color merge. Compare
  exact noise sigma, resolved support bytes, native BGR bytes and contributor
  counts against CPU on padded strides, non-multiple dimensions, sparse nodes,
  borders, masks, conflicting offsets, projective/fractional transforms and
  rounding boundaries. Test retained-layout transitions under the shared 64 MiB
  cap, failed/disabled backends and full-image HDR/SR regressions. Replay frozen
  PPT evidence, retaining rc15 unclipped-channel invariants. Report actual stage
  execution and full cost separately from eligibility and CPU completion; paired
  host timing cannot establish phone speedup or the >=30% device target.

- GPU-SCHEDULE-17 (specified 2026-10-16 before implementation): reproduce the
  13-patch cold/warm/CPU/selector observation, no-missing and mixed-missing fits,
  numerical rejection, bounded cost re-probes and skip windows. Verify cold cost
  is visible in total time and zero missing work launches no completion team.
  Run actual GPU selector/CPU parity on masks, borders, padded strides, flat/noisy
  and correlation/ratio threshold neighborhoods, mixed/partial <=16-patch batches
  and <=16 MP layouts. Ordinary CPU eligibility/crop must remain unchanged.
  Compare representative color tiles against full CPU rendering, retain exact
  native bytes/counts and test profile isolation across sizes/anchor counts.
  Per-result diagnostic traces must retain the original calibration costs and
  reused decision reason without leaking into another result; test truncation,
  legacy packets, disabled/release builds and failure quarantine. Frozen PPT and
  full image regressions still gate shipping; more GPU calls are not a speed pass.

- GPU-GLOBAL-18 (specified 2026-10-16 before implementation): exercise actual
  Euclidean and homography GPU optimization on known translations/rotations,
  nonidentity initialization, perspective, exposure changes, padded strides,
  borders, flat/uncorrelated input and maximum admitted layouts. Compare returned
  correlation and a distributed transform grid at original-source scale against
  CPU (0.002 /0.05 px); numerical failure must not enter normal GPU preference.
  Test resident layout transitions within 64 MiB, disabled/low-shared-memory
  hardware and all existing failure quarantines. Verify CPU goldens, frozen
  screen/white-field regressions and selected global candidates; report actual
  adopted call/iteration counts and cold/warm complete timing. V4 nested times
  and counts must be bounded and older packets remain readable. More adopted
  global calls alone do not establish device speedup or the >=30% target.

This revision establishes a quality backlog and acceptance contract. It does not
implement the missing features, add proprietary algorithms, benchmark competitor
quality, or establish numerical release thresholds without evidence.
