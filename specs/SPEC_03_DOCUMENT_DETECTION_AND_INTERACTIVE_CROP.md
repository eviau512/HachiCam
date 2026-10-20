# Specification 03: Document Detection and Interactive Crop

> Status: Authoritative. Revised 2026-10-12.
> Language: English.
> Boundary follow-up: [rc4 detection feedback](research/BOUNDARY_REVISION_2026-10-12.md).
>
> Quality revision contract: [2026-10-12 follow-up](research/QUALITY_REVISION_2026-10-12.md). Experimental implementation; numerical tuning and physical-device acceptance remain unverified.

## 1. Scope and current baseline

**Implemented:** downsampled luminance analysis, local contrast preprocessing,
gradient/contour candidates, side-supported line assembly, partial suggestions,
polygon scoring, touch preference, temporal overlay smoothing, line-assisted
snapping and manual crop handles/loupe. rc5 support thresholds are experimental.

These are image heuristics. They do not identify paper, glass, books or signs
semantically, and their presence does not prove robust boundary selection.

The existing long-receipt/sign aspect allowance up to 12:1 is retained.
Aspect ratio, rectangularity and text energy alone do not establish that a
candidate is the outer document boundary.

## 2. Detection input and coordinate contract

Preview resolution is negotiated with the camera; 1280 x 960 is not universal.
Respect actual valid crop, luma buffer position, row stride, pixel stride and
rotation. SPEC_01 owns image orientation and preview mapping.

The detector must state the coordinate space, dimensions, orientation and frame
timestamp with its result. It must distinguish a detected boundary from an inset
editing fallback. Absence of a reliable boundary must not be reported as a
successful detection. Numeric scores are not calibrated probabilities.

Working-tree status: found/quad, geometry, observation time, method,
partial/complete status, accepted-proposal count and weakest-side support exist.
Partial/manual UI and test-only raw/tracker/shutter diagnostics are implemented.
Calibrated confidence, a comprehensive public ambiguity/fallback interface and
full sensor-to-preview latency/crop calibration remain **not fully implemented**.

## 3. Physical boundary selection

**Required / partly implemented:**

- Use physical contour support and document containment together with text
  saliency. Prefer the supported outer sheet over an internal table, printed
  frame, photograph or screen panel.
- Measure saliency inside the true inset polygon, excluding outside texture.
  This correction exists in the working tree.
- Retain blank pages as valid candidates: text density is supporting evidence,
  not a condition that every document contain text.
- Handle a light page on a light surface, dark paper, textured tables, weak or
  broken edges, long receipts and a partly visible page.
- When evidence is ambiguous, allow manual choice or a full-image fallback
  instead of silently clipping content.
- A touch preference must expire and remain independent of focus and scene
  steadiness. It must not override invalid geometry.

Current scoring and sensitive edge thresholds are **experimental tuning**.
No fixed threshold is a universal solution to low-contrast boundaries.

The experimental rc5 implementation adds bounded line assembly and checks sides against
observed edges and coherent inside/outside contrast. Closed contours/hulls alone
are insufficient. The linked boundary revision owns initial limits and tests.
Expose method, partial/complete status, candidate count and weakest-side support;
these are diagnostic heuristics, not confidence probabilities. One extrapolated
corner within 25% outside the frame is permitted only for a partial suggestion.
Partial suggestions NEVER define automatic capture/import cropping. Preserve
the full available image for manual correction and clip the amber dashed
suggestion to the preview. Complete results retain in-frame pixel centers.

## 4. Temporal behavior and shutter handoff

**Experimental implementation:** normalized quad validation and target continuity
precede smoothing. Acquire after three consistent observations over 250 ms;
large position/area changes require three observations over 400 ms. Compatible
results use 0.22 new weight and a small normalized dead band. Missing candidates
retain a visual overlay for at most 250 ms. Rotation/dimension changes reset it.
Track completeness with selected geometry; complete and partial proposals need
their own acquisition. Missing raw detection cannot upgrade a retained partial
suggestion to a complete crop. Test-only diagnostics distinguish raw detection,
tracking, processing duration, observation age and shutter fallback.
Text saliency is limited to a 12% score bonus so dense internal tables do not
receive the former fourfold advantage over an outer blank sheet.

At shutter acquisition, choose the latest valid result after any steadiness
wait; evidence older than 250 ms is not applied. No reliable quad uses the full
image. Fusion may discard a bounded geometric border separately. Map pixel
centers through analysis rotation, first-source-to-reference homography, common
source crop, reconstruction scale and output rotation. Unknown reference mapping
or an out-of-crop corner retains the full available image for manual correction.
The source rectangle must never be compensated by stretching document corners.

Temporal continuity is not semantic recognition, focus or auto-capture. Full
confidence/ambiguity signaling and zoom/crop calibration remain incomplete.
The numeric defaults and detailed acceptance are recorded in the linked quality
revision; synthetic timing/mapping tests do not certify all physical scenes.

## 5. Geometry validity and manual interaction

Crop coordinates use the upright source image, in clockwise order:
top-left, top-right, bottom-right, bottom-left. The target validator must reject
nonfinite values, duplicate points, self-intersection, nonconvex/near-zero area
and unstable homographies before allocation or warping.

Integer pixel centers are 0 through width-1 and 0 through height-1. A UI may use
continuous image-edge bounds 0 through width and 0 through height only with an
explicit conversion; never mix these conventions implicitly.

**Implemented in rc10 for final planar edits:** managed preflight and native
rectification reject invalid geometry/type/shape before warp allocation. Optional
lookup inputs have bounded type/search checks and JNI exception guards. Automatic
detection confidence and all physical manual/snap cases remain unverified.

Manual edits take precedence over late automatic suggestions.
rc10 rejects stale crop edge/line/contour completions by page/draft generation.
Full-resolution border refinement is a future candidate and may not overwrite committed manual
geometry without an explicit user action.

Users must be able to correct, reset and confirm a crop. Dragging must not swap
corner identity or commit crossed edges. Snapping must have bounded attraction,
permit escape, and prefer supported physical edges over nearby text. Parallel
or distant line intersections are invalid snap targets.

**Implemented primitives:** line detection, corner/midpoint editing and a
magnifying loupe. The historical 150 dp search region, 36 dp snap radius and
continuous 60 FPS claims remain tuning/performance targets, not measured
guarantees. Image-pixel distances and view dp require explicit scale conversion.

## 6. Acceptance

Apply SCAN-01 through SCAN-04 and GEO-01 through GEO-03 in
[SPEC_06](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md).

Required fixtures include outer paper plus inner table, blank paper, weak edges,
a receipt up to 12:1, partial occlusion, rotated/mirrored imports and invalid
manual quads. Record annotated outer-boundary error, included content and
fallback rate. A fallback is preferable to a confident crop that removes text.

Manual drag/snap checks must include zoom, different display densities, image
letterboxing, crossed handles and late results after page changes. Source
selection and manual corrections must survive filter switching.
