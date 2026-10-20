# Document alignment experiment

Date: 2026-10-14. Offline research only; no Android algorithm was replaced.
Authoritative scope: SPEC_02 section 3 and SPEC_06 ALIGN-R01..03. See the
[Chinese Main handoff](../../specs/reports/DOCUMENT_ALIGNMENT_RESEARCH_2026-10-14.zh-CN.md).

## What is implemented

- Equal-size, globally registered, 64..512 pixel grayscale patches.
- Half-resolution integer block search and robust two-level Lucas-Kanade
  translation with geometry-only affine gain/bias, bounded steps and displacement.
- Hessian aperture/flatness check, common-support peak uniqueness, residual
  support and forward/reverse rejection. Alternative peaks use the same fractional
  phase as the fitted minimum; integer-only competitors falsely accept periodic
  grids. Forward/reverse consistency is also insufficient on its own.
- Existing-style ECC fit, plus an ECC variant with those experimental guards.
- Deterministic independently pixel-integrated synthetic phases, disjoint positive
  evaluation seeds, and a read-only probe of current production registration on
  a locally available real screen burst.

The experiment contains no dense motion field, per-pixel support renderer,
rolling shutter correction, HDR recovery or end-to-end SR integration. Rejecting
a patch here means no research SR credit; it is not a reason to discard valid
native denoising. `accepted` is a heuristic verdict, not calibrated confidence.
No competitor code, assets, constants or models were copied. Public concepts and
OpenCV APIs were independently composed under the repository's license.

## Reproduce synthetic probes

Requires Python, NumPy and OpenCV. From the repository root:

```powershell
python research/document_alignment/benchmark.py --suite exploratory
python research/document_alignment/benchmark.py --suite heldout --output .tools/document-alignment/heldout.json
```

For this workspace's existing runtime, use its Python executable and add
`--opencv-path .tools/vision`. No dependency is downloaded by these scripts.
Exploratory cases use seeds 0..7; held-out positives use 100..119 with three noise
levels and randomized known shifts plus one brightness-change case. Six negative
scenario types are safety regressions, not a fully independent negative corpus.
Parameters remain fixed between suites. Correction of a fractional-phase
uniqueness bug preceded the final held-out run; no threshold was reduced to pass.

The source scene is generated at 8x sampling, optically blurred, independently
shifted, and area-integrated into each 128x128 observation. Shift-map interpolation
still has finite discretization; this model does not simulate OEM JPEG physics.
Scenes have generated strokes/marks, not natural Chinese glyphs. Real Chinese
text, JPEG artifacts, faint pencil and directional blur require separate fixtures.

## Reproduce the production-domain probe

```powershell
./research/document_alignment/run-production-probe.ps1
```

The default input is the existing private paired screen evidence at
`.tools/rc10-1325-evidence/paired-screen/{0..6}.rgb`, with 4096x3072 RGB dimensions.
It is unavailable in a clean checkout. This script intentionally validates that
specific fixture shape; it is not a generic capture reader. Override compiler,
host OpenCV and Python paths using the script arguments as needed.

The host executable calls production `alignBaseFrame` and `registerLocal`, then
exports 27 pairs of 128x128 grayscale patches in the globally warped domain.
Patch centers are a fixed 3x3 grid in the previously studied text ROI. It records
production offset usability, detail eligibility at each center, and mean native
pixel support in each patch. Python evaluates the new patch candidates separately.
Output goes to `.tools/document-alignment/real-patches`, which is ignored.

This is not a comparison of interchangeable full pipelines: production fits
321-pixel overlapping patches on a 256-pixel grid and interpolates its nodes;
the new experiment fits 128-pixel patches. A disagreement is not an endpoint
error. Do not label the rejected real patches wrong matches or the accepted ones
newly resolved text without independent evidence and a reconstruction comparison.
The C++ host uses OpenCV 5; the Python experiments use OpenCV 4.10.0. Android also
uses 4.10.0, but these host results do not certify Android execution or resources.

## Current result and integration boundary

Main's 2026-10-15 review reproduced the held-out synthetic result and added an
exact 321x321 production-node/common-mask comparison. See
[the review report](../../specs/reports/UPSCAN_REVIEW_2026-10-15.zh-CN.md) and
[compact results](MAIN_REVIEW_2026-10-15.json). Run from the repository root:

```powershell
./research/document_alignment/run-matched-review.ps1
```

The review uses the same private fixture dependency and host runtime/compiler as
the earlier probe. It exports 27 exact production-node patches (not the earlier
128-pixel ROI centres), forms one shared 9x9-eroded unclipped mask, refits the
production local node, and evaluates fixed guarded-ECC/LK candidates. Rejection
checks retain their own additional interior/common-support requirements. Native
post-assessment applies the existing high-pass/noise/detail-ratio/residual bounds;
its `computeECC` at a fixed proposal is not a rerun of ECC optimization, and no
full interpolated motion field or regional reconstruction is rendered.

On that common mask, native production and LK each retain 15/27 after detail
checks, with 12 overlapping, three lost and three gained. There is no real flow
truth; this does not establish that the exchanged nodes are better. Current
application integration remains unimplemented. The original 128-pixel study and
its dated results are preserved as historical evidence, not overwritten.

The disjoint synthetic positives favor block/LK for accepted displacement error,
but its Python median patch time is higher than guarded ECC. Both new candidates
accept only 12/27 real patches, while production offsets are usable at 26 centers
and detail-qualified at 18. There is no real displacement ground truth. Neither
candidate is accepted for product integration. See the dated report and compact
[result snapshot](RESULTS_2026-10-14.json) for exact counts and limits.

The next bounded comparison should preserve production denoising/HDR masks,
compare registration at identical patch sizes and supports, test the guarded ECC
and selective block/LK options, and reconstruct original observations through
composed mappings. Require held-out stroke fidelity and Android time/memory
before enabling the candidate. Do not simply decrease the ambiguity thresholds
to recover the old accepted fraction.
