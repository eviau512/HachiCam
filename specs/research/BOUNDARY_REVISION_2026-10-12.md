# Boundary revision after rc4 feedback

Status: implementation contract, 2026-10-12. English clean-room handoff.

## Evidence

The user supplied two rc4 preview screenshots and an 18:00 diagnostic log.
Steadiness becomes true in the log, but there is no boundary diagnostic channel.
Running the production detector on isolated screenshot previews finds a large
desk/frame contour in the first and no quad in the second. This reproduces a
detector failure independently of temporal filtering; screenshots are not
identical to sensor luma frames and cannot establish every live-frame cause.

The sheet overlaps other sheets, has a curled lower edge, a clip at the top
right, and extends beyond the preview on the right. A single closed four-corner
contour is insufficient. The missing corner is not observable. No competitor
implementation was read. All limits below are independently chosen tuning.

## Detection and uncertainty

Keep contour proposals, blank-page eligibility, receipts up to 12:1 and the
bounded 12% text bonus. Validate proposed sides against actual nearby edges and
signed inside/outside luminance, excluding unavailable image-border samples.
A large hull created by morphological joining is not physical edge evidence.

Add bounded line assembly for broken/occluded outlines. At the 640-pixel working
long edge, retain at most 20 nonduplicate long LSD segments, assemble separated
opposite-side pairs, and intersect the two pairs. Require a convex quad, valid
angles, meaningful area, observed support on all visible sides and coherent
inside/outside contrast. Initial line limits: length 45 px, opposite separation
45 px, angle difference <=35 degrees, at least 40% near-edge samples per visible
side and 65% average in a 3.5 px band. Require three sides with coherent signed
contrast >4 levels and no side contradicting that polarity by more than 3.
Bound extrapolation to 25% per dimension and at most one outside corner. Do not
invent an entire page from three lines or a minimum-area rectangle. Full-frame
image borders do not count as observed document edges.

An out-of-frame corner or a line intersection lacking nearby corner evidence
(initial 6 px working-space band) is a PARTIAL suggestion. A 5 px band rejected
one subpixel/luma perturbation of the user's curled lower corner at 5.19 px;
the revised bound retains that evidence while the occluded-corner negative
remains partial. This is tuning evidence, not held-out validation. At least three
corners must have observed evidence. Keep the unmodified
quad for continuity, clip its drawing to the preview, use a dashed amber outline
and show "Edges incomplete · adjust after capture". Partial suggestions NEVER
define an automatic capture/import crop; retain the full available image for
manual correction. Complete supported quads retain green outline/crop behavior.
Missing/pending boundaries display manual guidance. Steadiness is independent.

Keep existing acquisition, switching, smoothing and 250 ms expiry. Validate
partial suggestions separately with bounded outside coordinates; complete
results require in-frame pixel centers. Preserve selected completeness through
dropouts; a missing raw result must not upgrade a partial suggestion.

## Diagnostics and verification

Expose native method, completeness, accepted candidate count and weakest-side
support. Scores are heuristics, not probabilities. In test builds only, log raw
versus tracked state, acquisition/expiry reason, geometry, observation age and
analysis duration at most once per second. Log shutter complete-crop versus
full-image fallback. Add no release model/root or pixel-content collection.

Run production C++ fixtures for complete/broken/stacked/blank/dark/long pages,
inner-table, uniform and desk-only negatives, partial suggestions and validity.
Add timing tests for completeness transitions/dropouts and crop eligibility.
Re-run both screenshots as tuning cases, report partial/manual outcomes, and
keep unrelated synthetic regressions. Screenshot success is not a live-device
acceptance pass. Record host/Android OpenCV versions and latency; no 60 FPS claim
without device measurements.
