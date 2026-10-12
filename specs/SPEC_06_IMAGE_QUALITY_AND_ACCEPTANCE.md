# Specification 06: Image Quality and Acceptance

> Status: Authoritative acceptance contract. Added 2026-10-12.
> Language: English.
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
| LIFE-01 | P0 | Rapid A/B filter changes, rotate, navigate and delete during work: only the latest valid page generation publishes; no deleted-page resurrection or native use-after-free | Burst deletion protection partial; full edit generation/ownership incomplete; SPEC_00/04 |
| LIFE-02 | P0 | Encoding/full-disk/commit failure: preserve source and last valid result; no READY pointing at nonexistent output; pending/failed export is explicit | Capture transactions partial; crop/review writes incomplete; SPEC_01/04 |
| LIFE-03 | P0 | At admission limit or memory pressure: reject/degrade visibly and release resources; future restart recovery must resume exactly one durable job without losing page recipe | Three-job burst bound present; byte budget and durable recovery absent; SPEC_00/01 |
| UI-01 | P1 | Displayed modes and achieved output agree; no unsupported Photo/dewarp/bilevel/GPU/50 MP guarantee; filter selection does not change crop | Three filters implemented; copy/workflow verification pending; SPEC_05 |
| UI-02 | P1 | Uncertain crop or failed enhancement: visible manual correction, source comparison and last-valid image; never silent content deletion | Controls partial; transactional comparison workflow incomplete; SPEC_05 |
| UI-03 | P1 | Steady, focus acknowledgment, requested HDR and processing progress are distinguishable; no one green state implies all quality criteria passed | Steadiness separation implemented; full quality guidance absent; SPEC_05 |

Additional rc4 fixtures (see the linked quality revision):

| ID | Priority | Scenario and acceptance | Current status / owner |
| --- | --- | --- | --- |
| SCAN-05 | P1 | Jitter, alternating nested targets, missing/invalid quads and persistent changes cannot cause immediate target jumps; stale evidence expires | Kotlin tracker regression; device tuning pending; SPEC_03 |
| GEO-04 | P0 | Asymmetric source crop and reference translation through 0/90/180/270 rotations at scales 1/2 retain pixel centers within one output pixel | Kotlin mapping regression; end-to-end CameraX mapping pending; SPEC_02/03 |
| PHOTO-06 | P0 | Two-depth text motions, blurred first frame and occlusion: retain useful multi-frame support, improve local residuals and reject unsupported text; crop per edge <=5% | Production C++ synthetic tests; mobile/real-burst acceptance pending; SPEC_02 |
| FILT-06 | P0 | Continuous faint marks, color, dark paper, tiny/channel variants and unchanged input master | Production math/image synthetic tests; real mark/parity acceptance pending; SPEC_04 |
| UI-04 | P1 | Crop, single and batch review all expose Original/Magic/Paper only | Code/resource verification and build; device UI acceptance pending; SPEC_05 |

## 5. Required result record

Additional rc5 boundary checks ([contract](research/BOUNDARY_REVISION_2026-10-12.md)):

| ID | Priority | Scenario and acceptance | Current status / owner |
| --- | --- | --- | --- |
| SCAN-06 | P0 | Stacked sheets/clip/curl/right edge outside preview: show supported partial suggestion or manual guidance; never apply an extrapolated quad as an automatic crop | Experimental native fixtures, screenshot tuning and tracker tests pass; capture/import/re-detect guards inspected; device UI/crop pending; SPEC_03/05 |
| SCAN-07 | P0 | Desk-sized hull, uniform field and opposing side polarity: reject unsupported document proposals; complete blank/dark/receipt pages remain eligible | Production C++ synthetic fixtures pass; mixed-background recall and field accuracy uncalibrated; SPEC_03 |
| SCAN-08 | P1 | Test logs separate raw miss, tracker acquisition/dropout, partial/complete and shutter fallback with bounded output | Implemented behind test-build diagnostics flag; debug/release compile/privacy tests pass; rc5 device log pending; SPEC_03 |

For each executed case record fixture revision, application build, device/camera,
recipe and algorithm version, observed metadata, expected outcome, measured
outcome, pass/fail, fallback reason and residual limitation. Include comparative
source/output crops where permitted. Tests not run must be marked not run.

This revision establishes a quality backlog and acceptance contract. It does not
implement the missing features, add proprietary algorithms, benchmark competitor
quality, or establish numerical release thresholds without evidence.
