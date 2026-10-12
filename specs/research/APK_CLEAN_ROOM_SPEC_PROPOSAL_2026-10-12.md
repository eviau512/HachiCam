# Clean-Room Specification Proposal: Document Capture and Enhancement

Date: 2026-10-12. Status: specification proposal only; no application implementation or authoritative specification was edited by this analysis.

This document supplies independently phrased behavioral contracts for revisions to SPEC_00 through SPEC_05. It contains no competitor implementation, private interface identifiers, asset contents, reconstructed constants, or translated algorithm design. Public algorithm families are options for independent implementation, not requirements to reproduce another product.

## 1. Evidence and status boundaries

The comparison baseline is `specs/README.md`, SPEC_00 through SPEC_05, and the implementation-status document dated 2026-10-12. HachiCam application source was not inspected. An implementation claim below means **reported by the existing status document**, not independently verified by this analysis.

The supplied reference collection and ten APK/XAPK artifacts were inspected read-only. Inspection covered package structure, standard model-file signatures, packaged user-interface terminology, and selected native-interface metadata. Neither competitor APK code nor embedded models were executed. No suitable device or sandbox session was available for running the competitors. Consequently there are **no runtime observations** in this proposal.

| Evidence category | Sanitized observation | Limit on interpretation |
| --- | --- | --- |
| Direct static observation: CamScanner | Packaged terminology and interface metadata cover boundary geometry, illumination treatment, curved-page processing, image detail, automatic capture and quality guidance. | Presence does not establish which path runs, its exact mathematics, effectiveness, or availability without a service connection. |
| Direct static observation: Genius Scan | Geometry and enhancement interfaces are distinguishable; standard model-file signatures and multiple monochrome/quality choices are packaged. | This supports investigating separate processing concerns; it does not validate the supplied document's state thresholds or filter equations. |
| Direct static observation: ClearScan | The supplied package contains a standard learned-model payload associated with boundary detection, alongside native image-processing components. | The supplied reference's broad claim that the capture engine excludes such frameworks cannot be adopted as an established fact for this artifact. Model presence still does not prove activation. |
| Direct static observation: Microsoft Lens and Adobe Scan | The packages include geometry/capture-related components, model payloads, and scan-mode or capture-quality terminology. | Exact confidence rules, geometric models, glare handling, and timing claims remain unverified. |
| Direct static observation: vFlat | Standard model-file signatures, graphics-related payloads/API references, and terminology for curved-page processing, occlusion cleanup and scan resolution are present. | These observations do not prove a particular surface model, graphics execution path, reconstruction quality, or preview frame rate. |
| Direct static observation: TurboScan | Capture-quality guidance and a multiple-shot capture concept are packaged. The inspected native metadata provides insufficient independent evidence for the detailed algorithms asserted by the supplied reference. | Do not adopt the reference's scoring equation, pacing, fusion behavior or numerical benefit as verified behavior. |
| Direct static observation: Motorola camera artifacts and camera-extension helper | The camera artifacts contain focus/exposure/resolution guidance and components associated with HDR or noise processing. | This does not establish that HachiCam can access an OEM engine, that a standard quality request selects a particular hardware algorithm, or that a helper package performs the full processing itself. |
| Supplied reference documents | They assert additional algorithm designs, exact thresholds, timing, memory use and cross-product comparisons. | Treat these as research claims. They are not independent measurements or implementation requirements. |
| Inference | Separating geometry, capture quality, illumination, palette conversion and processing state is likely to make HachiCam easier to validate and safer to degrade. | This is an engineering inference from broad concerns, not a verified description of a competitor pipeline. |
| Proposed future behavior | The contracts below define testable HachiCam outcomes. | They do not establish shipping functionality or competitor parity. |

Status labels used throughout:

- **Already specified:** a contract exists in the current authoritative specifications; retain its valid intent and avoid counting it as a newly discovered feature.
- **Genuinely missing contract:** the specifications lack a complete observable input/output, failure, consistency or validation rule.
- **Correction required:** authoritative text conflicts with other authoritative text or the documented implementation status.
- **Experimental tuning:** parameters and quality/resource targets require a named evaluation set and device measurements before becoming release claims.
- **Unimplemented candidate:** a possible future feature, including an existing specification target that the status document reports as unfinished.

## 2. Corrections to the authoritative baseline

These corrections belong in the affected authoritative specification and its acceptance criteria. An audit entry or this proposal is not a substitute.

| Specification | Already specified | Correction required |
| --- | --- | --- |
| SPEC_00 | Local processing, subsystem separation, latency targets and fallback intentions. | Present performance figures as measurement targets with a device/workload definition. Do not describe GPU HDR, fixed capture latency, or complete recovery as implemented when the status document says otherwise. |
| SPEC_01 | Manual capture, tap metering, independent scene-motion guidance, exposure tiers, orientation normalization and bounded background work. | Replace unconditional seven-frame/exposure/size descriptions with capability-checked, actually verified capture outcomes. Keep requested exposure distinct from measured exposure. Distinguish surviving a screen change from surviving process death. Ordinary EXIF annotations are provenance descriptions, not tamper-evident signatures. |
| SPEC_02 | Alignment verification, auxiliary-frame rejection, fine-detail protection, reference selection and future local refinement. | Resolve competing historical fusion rules into one current contract. Identify JPEG-domain input, experimental reconstruction, actual output dimensions and unsupported RAW/GPU/local-motion targets. Remove unconditional perfect-detail, zero-ghosting and fixed-resolution claims. OEM product names must not be used as proof of equivalent behavior. |
| SPEC_03 | Wide document proportions, geometry/saliency ranking, temporal overlay smoothing, manual corners, edge snapping and a loupe. | Synchronize the saliency region with the documented polygon-based behavior. Treat preview dimensions as negotiated inputs rather than universal camera facts. Separate detection confidence/target identity from the already revised scene-motion state. |
| SPEC_04 | Planar rectification, color illumination normalization, grayscale and local thresholding. | The status document reports different background estimation, threshold-window selection and interpolation from the authoritative text. State the verified current choices or label replacements as future work. Replace guaranteed physical aspect recovery and occluded-text reconstruction with the limits in Sections 3 and 8. |
| SPEC_05 | Review modes, capture feedback and processing indicators. | Reconcile the HDR explanation with the current capability-checked pipeline. Label the Photo mode, asynchronous filter thumbnails and reorder interaction according to their documented implementation status. A visible control must not imply an unavailable processing feature. |

## 3. Boundary detection, coordinate systems and crop geometry

### 3.1 Image and transform contract — genuinely missing contract; P0

Every captured/imported image shall have an immutable source identity, decoded dimensions, orientation/mirroring interpretation, color interpretation and actual sampling dimensions. A rendered page shall separately record its geometry, filter configuration, processing revision and resulting dimensions.

Specify one canonical upright image coordinate system and one explicit pixel-center/image-edge convention. Normalized storage coordinates are acceptable only when their conversion to pixels is defined. Sensor coordinates, analysis coordinates, capture crop, display fit/fill, zoom, rotation, mirroring and output scaling shall be represented as explicit transforms; equal normalized coordinates alone do not establish equal fields of view.

Detection output shall carry the image dimensions, orientation, transform identity and frame timestamp that produced it. A still-capture crop suggestion shall be derived from the shutter snapshot and the actual captured image. If that mapping cannot be established, perform still-image detection or present a manual crop suggestion; do not silently apply a stale quad in a different field of view.

Acceptance: use labeled synthetic points and document outlines through portrait/landscape capture, all EXIF rotation/mirroring cases, differing preview/capture aspect ratios, display letterboxing, zoom and native/reconstructed output sizes. Round trips shall agree within the declared coordinate quantization. Black-border touches shall not select image content. Rotating or rescaling the representation shall preserve the same physical crop.

### 3.2 Detection result and boundary validity — genuinely missing contract; P1

The detector shall distinguish a valid boundary, an uncertain suggestion, no detected document and processing failure. Its result shall include boundary validity, a confidence interpretation, evidence freshness and a diagnostic reason where available. A heuristic score shall not be presented as a calibrated probability without calibration data.

Candidate scoring may combine line/contour evidence, area, spatial preference and internal structure. It shall not require dense text for blank forms, photographs, sparse receipts or colored documents to remain eligible. Internal tables, picture frames and desk edges shall not automatically outrank an enclosing sheet merely because their contrast is higher. A user selection is preference evidence, not proof of semantic document identity.

A fallback inset/full-image quad shall be visibly and internally classified as a suggestion. It shall not be counted as successful detection or high confidence. Evidence expiry, target-switch hysteresis and smoothing reset rules are **experimental tuning**; their units and time basis must be stated. Contour changes shall not reset SPEC_01's scene-motion state.

Acceptance: evaluate outer-sheet selection on labeled nested-table, textured-desk, low-contrast, partial-page, blank-page, photo, long-receipt and strong-perspective fixtures. Report boundary error and coverage separately from abstention/false-positive rates. Test disappearance and reappearance so an old boundary cannot remain authoritative indefinitely. Numerical quality thresholds must be frozen against a named dataset before a release-quality claim.

### 3.3 Quad editing and full-image rectification — genuinely missing contract; P0

Before rectification, validate finite coordinates, consistent corner ordering, convexity, nonzero usable area, nonintersecting edges and an invertible well-conditioned mapping. Invalid edits shall preserve the last valid crop or request correction. They shall not crash, create an unbounded output allocation, or silently remove document content.

Manual edits shall take precedence over later automatic suggestions. Full-resolution border refinement is a proposed improvement, not an established existing feature; it may refine an initial suggestion but shall not overwrite a committed manual crop without an explicit user action.

Snapping shall use the same source/display transform as the handles and loupe. Near-parallel line intersections, remote intersections and unsupported edges shall not cause large corner jumps. Snap engagement, release and override behavior must be testable; attraction distances and hysteresis remain **experimental tuning**. A snap indicates nearby edge evidence, not guaranteed paper boundaries.

For an unknown document, planar rectification produces an estimated rectangular aspect ratio. True physical proportions require an explicit known-paper ratio, user constraint or validated camera calibration. Do not silently assume A4 or another paper standard. Output dimensions, rounding, interpolation, border fill and maximum allocation shall be declared. Preserve observed content; do not synthesize missing margins as if captured.

Acceptance: reject crossed, duplicated, collinear, nonfinite and near-degenerate corners; preserve the source. Verify drag/loupe/snap agreement across zoom and rotation. Known-ratio synthetic pages shall match the selected ratio; unconstrained cases shall be labeled estimates rather than physical measurements.

## 4. Illumination, color, grayscale and binary output

### 4.1 Separate enhancement responsibilities — genuinely missing contract; P1

A render configuration shall distinguish geometric correction, illumination compensation, background cleanup, palette conversion and detail treatment. These may remain simple user-facing presets; separate configuration does not require exposing every internal control.

The review meaning of Original shall remain geometry-corrected source color with enhancement disabled, while the immutable unwarped source remains available for re-editing. Color enhancement shall aim to improve document readability while preserving meaningful ink colors, stamps, highlighting, photographs, shaded table cells and colored paper. Grayscale shall retain continuous tonal values under a declared color/luminance conversion. Binary mode shall produce a genuine two-level processing result. The encoded/exported representation shall be stated separately: a binary-looking JPEG is not proof of a one-bit archival encoding.

The proposed Photo preset is an **unimplemented candidate** according to the status document. If implemented, define its treatment of photographic tones, texture and geometry explicitly; it shall not inherit document whitening or aggressive text thresholding by accident.

### 4.2 Illumination and shadow limits — genuinely missing contract; P1

Retinex/background-field estimation and edge-preserving filtering are acceptable public algorithm families for independent evaluation. Their required output is bounded illumination correction, not a claim of recovering exact physical reflectance.

Illumination estimation shall avoid treating dark ink or colored artwork as illumination alone. Correction shall remain finite and bounded on dark, uniform, saturated or nearly zero-valued input. Background whitening shall be conservative where paper/ink evidence is ambiguous and shall not erase faint pencil, fine rules or low-saturation highlights simply to achieve a white page.

Separate uneven lighting from information loss. Strong shadow noise, clipped glare, occlusion and defocus may be unrecoverable. Where sufficient source information is absent, retain a conservative result and provide a recapture or mode-change path. Do not claim shadow-free output or restored characters solely because the background became white.

Acceptance: paired evenly lit/shadowed versions of the same page shall show reduced background nonuniformity while preserving the labeled foreground strokes and colored content. Uniform black, uniform white, dark gradients and clipped highlights shall produce finite valid results. Severe no-information fixtures shall exercise the declared fallback and shall not acquire invented marks.

### 4.3 Scale and detail preservation — genuinely missing contract; P1

Local threshold windows, background-estimation scales, denoising and sharpening shall have a declared relationship to image resolution or estimated document structure. A fixed pixel window shall not be assumed to have the same document effect at thumbnail and final resolution. Parameters and their reference scale shall be versioned with the render configuration; values are **experimental tuning**, not copied competitor settings.

Fine-detail protection applies to both single-image filters and multi-frame photography. Avoid hollow glyphs, merged adjacent strokes, lost decimal points, erased thin rules, halos and false sharpening of noise. Public families such as adaptive thresholding, noise-aware denoising and bounded edge enhancement may be evaluated independently. Stronger apparent contrast alone is not evidence of more captured detail.

Acceptance: use annotated digits, punctuation, multilingual fine text, faint handwriting, signatures, thin table rules, halftones and colored annotations at multiple input resolutions. Compare preserved stroke topology/content, edge artifacts and background noise against the source and the current baseline. OCR may be supplementary evidence; it shall not be the sole fidelity gate. Repeated filter switching shall always re-render from the immutable source, avoiding cumulative degradation.

## 5. Capture quality, focus, motion and exposure

### 5.1 Independent quality signals — genuinely missing contract; P1

Scene motion, focus readiness, exposure validity, boundary completeness and final-image usability are separate signals. SPEC_01's steadiness state is already specified and shall remain independent of contour identity. A steady indication does not certify successful autofocus, correct exposure or sharp text throughout a tilted page.

Tap-to-focus feedback shall distinguish the tap acknowledgment from a confirmed focus outcome. Unsupported autofocus, fixed-focus optics, failed metering and timeout shall have defined unknown/failure behavior. Manual shutter availability shall be preserved; quality guidance shall be informative and actionable rather than an unexplained capture lock.

Assess final-image quality over the document region, including spatially separated text/detail areas when available. A sharp center cannot establish sharp corners; high-ISO noise or a bright page border cannot establish useful fine detail. Any sharpness/blur scoring and capture-readiness thresholds are **experimental tuning** and require noise/exposure/scale validation.

Acceptance: fixed-focus and autofocus devices, intentional defocus, directional shake, tilted pages, low light and clipped glare shall exercise distinct quality states. A failed focus request shall not display confirmed focus. Post-capture warnings shall retain the captured source and offer recapture or continued manual review.

### 5.2 Capability-checked capture and honest output — already specified intent; correction required; P0

Retain the existing requirement to check supported sensor controls and verify actual frame exposure/rotation. Requested settings are not measured outcomes. Standard camera quality modes do not prove use of a specific vendor temporal engine. Unsupported controls or invalid auxiliary exposures shall degrade to a supported capture path without mislabeling the result.

Retain frame association with capture identity, timestamp, orientation and measured exposure. Reference selection shall prefer usable document detail, not noise energy alone. Alignment uncertainty, periodic text ambiguity, local subject movement, parallax and missing frames shall cause conservative rejection/fallback according to the currently implemented capability; the unimplemented local-motion targets must remain labeled future work.

Output size shall reflect accepted sampling data and resource limits. Interpolation alone shall not be described as recovered optical resolution. Highlight recovery shall require valid, aligned, less-clipped source information; otherwise preserve a conservative reference result. All-input clipping and occluded content shall not be represented as restored detail.

Acceptance: unsupported manual control, missing metadata, repeated exposure, dropped auxiliary frames, repeated text patterns, moving foreground objects and fully clipped highlights shall complete through a declared fallback. Diagnostics shall distinguish requested mode, accepted evidence, actual output dimensions and degraded result. No competitor-equivalence claim follows from passing these tests.

## 6. Preview/final consistency and processing lifecycle

### 6.1 Revisioned rendering — genuinely missing contract; P0

Every preview, thumbnail, final page and export shall be attributable to the same source, crop/orientation, filter configuration and processing revision. Reduced-resolution previews may approximate the final render, but shall use consistent geometry and scale-aware parameter interpretation. They shall not show an unrelated stronger/weaker preset without disclosure.

Asynchronous results shall commit only if their page still exists and their revision is current. Rapid filter changes, rotation, recropping, deletion and processing completion shall not allow an older result to overwrite a newer edit or resurrect a deleted page. Export shall use the committed current revision, never a placeholder or unfinished intermediate.

Acceptance: deliberately complete jobs out of order while switching filters, rotating, editing crop and deleting pages. Only the newest applicable revision may become visible/exportable. Compare final renders with their display-sized previews on geometry, tonal direction, selected mode and annotated fine-detail visibility. Freeze numeric preview/final color-error tolerances only after choosing the preview approximation and benchmark.

### 6.2 Resource accounting and transactional completion — already specified in part; genuinely missing whole-pipeline contract; P0

Retain the existing single-worker and finite-queue intent. Define resource accounting across compressed captures, decoded source images, native intermediates, output buffers, thumbnails, filter jobs and export; a pending-JPEG budget alone is not a peak-memory bound. Include dimensions/overflow checks and a declared maximum pixel/output budget before expensive decoding or allocation.

Analysis shall prefer current evidence and shall not accumulate an unbounded backlog. Processing cancellation shall release owned resources. CPU/GPU acceleration is an implementation option; each supported path must satisfy the same output/failure contract and its own measured budget. A fallback path that exceeds the device's memory budget is not a valid fallback.

A page becomes ready only after a readable complete output is atomically committed and its metadata identifies the correct revision. If enhancement/fusion fails but a valid source/reference can be saved, record a degraded ready result and its reason. If source/output storage cannot be completed, expose a failed/retryable state; do not silently mark success. Retain the immutable source according to an explicit retention policy rather than deleting it merely because export occurred.

Acceptance: queue saturation, cancellation, background navigation, missing/corrupt input, encoding failure, full storage and low-memory preflight rejection shall terminate in a defined ready/degraded/failed state. No stuck processing state, deleted-page resurrection or exported partial file is acceptable. Repeated capture/edit/export/cancel cycles shall have bounded temporary storage and memory after jobs settle, excluding intentionally retained results.

SPEC_01's RAM spill, durable pending manifests and restart recovery are **already specified but unimplemented candidates** according to the status document. Their future acceptance must include process termination and restart. Until implemented, explicitly disclose that pending work/session metadata may be lost; continuing after navigation is not durable recovery.

### 6.3 Measured performance — experimental tuning; P1

Replace universal per-stage guarantees with named workload/device profiles. Measure preview analysis age and frame drops, shutter-to-last-frame time, shutter-to-ready time, filter-preview response, final render time, peak process/native/GPU memory and temporary storage. Include decoding, encoding and committing in end-to-end measurements. Report distribution/tail behavior and thermal conditions, not only a best run.

The existing performance figures may remain provisional targets. Neither static APK size nor packaged component names establish processing speed or memory consumption. A release claim requires repeatable measurements on the declared supported profile and a documented response when the budget cannot be met.

## 7. Automatic capture — unimplemented candidate; P2

Packaged competitor terminology supports considering automatic capture, but does not establish a need to clone a competitor state machine. This candidate must not replace the existing manual shutter or scene-motion contract.

If implemented, define independent eligibility from fresh valid document geometry, scene motion, supported focus/exposure evidence and available processing capacity. Unknown or stale evidence shall not trigger. Rearming shall require a page/scene change, leaving/reentering the eligible state or explicit user action, so an unchanged page cannot generate an indefinite capture loop. Auto-capture shall be user controllable and cease when the camera is no longer active.

Acceptance: one eligible unchanged page produces at most one capture per arming event; page turns can rearm; contour switches alone do not imply a new page; stale frames, poor focus, backgrounding and queue saturation cannot cause uncontrolled capture. Timing and page-change thresholds remain experimental until evaluated on real devices.

## 8. Curved pages and occlusion — already specified targets; unimplemented candidates; P2

Automatic book dewarping and finger cleanup are not completed HachiCam features according to the status document. Packaged competitor components do not validate HachiCam's surface estimation or justify claiming restoration of hidden text.

Any future curved-page mode shall define the supported document class, evidence needed to estimate the deformation, a nonfolding content-preserving mapping and user-visible failure behavior. Public mesh/spline/analytic-surface families may be evaluated independently. Insufficient boundary/baseline evidence shall fall back to planar/manual cropping. Double-page splitting is a separate candidate and shall preserve page order and source content.

Occlusion cleanup shall never promise recovery of characters that were not visible. Conservative margin cleanup may be evaluated only with a retained original, an explicit user action and safeguards against removing ink. Where occlusion intersects content, offer recapture/manual retention rather than synthesizing authoritative document text.

Acceptance before enabling these candidates: curved-grid and real-book fixtures shall retain all annotated content, avoid foldovers/duplication and improve measured line straightness without materially distorting glyphs. Occluded-text fixtures shall remain marked unrecoverable. The current UI shall not advertise these unvalidated capabilities as available.

## 9. Compact priority and acceptance matrix

P0 protects correctness, content and completion. P1 establishes measurable capture/enhancement quality. P2 remains future scope until the base pipeline is validated.

| Priority | Authoritative owner | Status | Contract or correction | Minimum acceptance evidence |
| --- | --- | --- | --- | --- |
| P0 | SPEC_01, SPEC_03, SPEC_04 | Genuinely missing; partly reported implemented | One source/orientation/coordinate transform contract. | Rotation/mirroring/aspect/zoom/output-size fixtures preserve the physical crop and exclude letterbox touches. |
| P0 | SPEC_03, SPEC_04 | Genuinely missing | Validate quads and preserve manual edits. | Degenerate/crossed/nonfinite inputs do not crash, overallocate or commit a destructive crop; delayed suggestions cannot overwrite manual work. |
| P0 | SPEC_04 | Correction required | Estimated versus known/calibrated aspect ratio. | Known-ratio fixture meets the chosen ratio; unknown pages are not silently forced to a paper standard. |
| P0 | SPEC_01, SPEC_02 | Already specified intent; correction required | Capability/actual-exposure gating and honest HDR/resolution labels. | Unsupported, missing, repeated and invalid frames produce documented fallback with actual dimensions and no false recovery claim. |
| P0 | SPEC_01, SPEC_04, SPEC_05 | Genuinely missing | Revisioned asynchronous rendering and export. | Deliberately reordered job completions cannot export an old edit or resurrect a deleted page. |
| P0 | SPEC_00, SPEC_01, SPEC_04 | Already specified in part; missing global contract | Whole-pipeline resource bounds and transactional completion. | Saturation/storage/encoding/cancellation tests end in explicit states with bounded retained work and no partial output. |
| P0 | SPEC_01, SPEC_05 | Already specified but unimplemented | Truthful recovery and pending-page behavior. | Current limits are visible; future recovery claims require process-kill/restart tests. |
| P1 | SPEC_03 | Genuinely missing | Boundary confidence, ambiguity, freshness and full-page selection. | Labeled nested/blank/low-contrast/partial fixtures report boundary coverage, error, false positives and abstentions. |
| P1 | SPEC_03, SPEC_05 | Already specified mechanics; missing quality/failure rules | Transform-consistent snapping and loupe behavior. | Rotated/zoomed and near-parallel-edge fixtures cannot produce unexplained corner jumps or mismatched magnification. |
| P1 | SPEC_04 | Already specified algorithms; missing preservation contract | Separate illumination/background/palette effects and their limits. | Paired shadow tests improve background uniformity while retaining annotated ink/color; no-information cases remain conservative. |
| P1 | SPEC_02, SPEC_04 | Genuinely missing common gate | Scale-aware filtering and text/fine-detail fidelity. | Multi-resolution punctuation, thin-rule, faint-text and color fixtures preserve labeled content without hollow strokes/halos; OCR is supplementary. |
| P1 | SPEC_01, SPEC_05 | Genuinely missing | Separate focus, motion, exposure and final-image usability. | AF failure/unsupported, blur/noise and tilted-plane tests yield correct unknown/warning behavior without locking manual capture. |
| P1 | SPEC_00, SPEC_01, SPEC_04, SPEC_05 | Experimental tuning | Preview/final consistency and measured performance profiles. | Common configuration/revision plus a declared benchmark; measured tail latency, memory and quality before asserting a target is met. |
| P2 | SPEC_01, SPEC_03, SPEC_05 | Unimplemented candidate | Optional automatic capture with explicit rearming. | Fresh eligibility, no repeated unchanged-page loop, manual override and safe capacity/background behavior. |
| P2 | SPEC_04, SPEC_05 | Already specified but unimplemented candidates | Curvature/occlusion/Photo capabilities with honest limits. | Enable only after content-preserving fixtures pass; hidden text is never presented as recovered evidence. |

## 10. Unresolved verification requirements

No competitor runtime sequence, latency, peak memory, image-quality score, offline availability or exact algorithm was verified. No HachiCam source, build or device test was performed by this analysis. The supplied reference documents' exact performance comparisons and proprietary implementation assertions remain unverified and shall not become acceptance values.

Before implementation claims change, synchronize the authoritative specifications with the latest verified HachiCam behavior, choose representative real-device/corpus profiles, freeze experimental quality tolerances before evaluation, and record failures as well as successes. Boundary quality, focus behavior, clipped highlights, shadow correction, fine text, preview/final equivalence, sustained workload and restart behavior all require their stated runtime checks.
