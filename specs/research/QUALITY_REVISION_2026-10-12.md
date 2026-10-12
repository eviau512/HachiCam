# Quality revision: capture, crop and paper rendering

Status: implementation contract, 2026-10-12. English clean-room handoff.

## Evidence and boundary

The user supplied rc3 capture diagnostics and an 8192 x 6144 JPEG. The JPEG
reports four base frames, three highlight frames and scale 2. Base exposure was
approximately 16.65 ms, acquired over approximately two seconds. Preview motion
was already steady. Global registration accepted every base frame, but no local
stroke reliability, reference ranking or common source coverage was recorded.
The scene contains near and distant objects. Local parallax is a plausible cause
of soft/doubled text; without the original frames it is not a proven sole cause.
ISP processing and focus cannot be isolated from this final JPEG.

Read inputs: the authoritative English specifications, the sanitized APK proposal
and the other conversation's parent-level summary. No closed implementation or
decompiled algorithm was used. Numerical settings below are our initial tuning,
not competitor constants. The user's instruction is to KEEP multi-frame fusion;
discarding a modest unreliable image border is acceptable. Paper replaces BOTH
Black & White and Grayscale. The visible set becomes Original, Magic, Paper.

## SPEC_03: acquisition, continuity and freshness

Implement a normalized-coordinate quad tracker independent of camera steadiness.
Validate finite, convex, ordered, in-frame quads first. Acquire a candidate after
at least three observations over 250 ms. A change exceeding 3% of the diagonal
or an area ratio outside [0.75, 1.33] needs three consistent observations over
400 ms before switching. Same-target smoothing uses 0.22 new weight with a
0.15%-diagonal dead band. Missing detections may retain the visual overlay for
250 ms only. Resolution/orientation changes reset the tracker. These are
experimental defaults, not calibrated probabilities or document readiness.

Reduce internal-text saliency from a possible fourfold score to at most 12%.
Prefer a larger plausible enclosing boundary over nested printed regions;
blank paper remains eligible. This is still a contour heuristic, not semantic
object recognition. Do not claim all outer-boundary acceptance fixtures pass.

At shutter acquisition, revalidate the latest tracked result after any steadiness
wait and camera-control lock. The observation time is analyzer receipt time,
not completion time; slow processing cannot refresh old evidence. Sensor-clock
latency calibration remains unverified. Results older than 250 ms do not define a destructive crop. Missing or
invalid evidence uses the full image rather than an arbitrary inset. Fusion's
source coverage rectangle is separate from a document crop. Map analysis pixels
through unrotated source coordinates, subtract that coverage origin, scale and
rotate once. Pixel centers use endpoints 0..dimension-1; never stretch a detected
quad to compensate for discarded source borders.

## SPEC_02: retain fusion, protect local text

Reject differently oriented frames within one burst; metadata and saved EXIF
must belong to the chosen reference. Rank eligible base frames by robust tile
gradient energy, clipping and exposure;
use deterministic arrival order for ties. Log the selected index and scores.
All accepted base frames may still contribute. This is full-scene ranking;
document-only ranking and focus certification remain unimplemented.

After global registration, refine bounded local translations at source scale on
an overlapping tile grid. Validate local high-frequency correlation and relative
detail; blurred or inconsistent tiles cannot contribute text merely because a
global transform passed. Bound refinement to 8 source pixels and record accepted
tile fractions. Use local photometric residuals to gate samples; retain the best
reference estimate only in locally unsupported regions. This is sparse tile
translation, not dense flow, depth recovery or rolling-shutter correction.

Keep direct source sampling for experimental 2x reconstruction. Require at least
three base frames, distinct two-dimensional phases and sufficient reliable tile
coverage. Without that support, retain multi-frame fusion at native dimensions.
A larger output is not a proven detail gain. No generative text restoration.

Compute a conservative common interior rectangle supported by every accepted
base frame including local offsets and the interpolation footprint. Discard at
most 5% from each edge. If more is needed, reject the incompatible frame rather
than remove arbitrary content. Log/report reference index, source crop origin,
source crop size, accepted tiers and actual scale. Highlights do not dictate
the crop and contribute only at supported locally validated locations.

Do not force maximum ISP noise reduction, sharpening or tonemapping for every
capture. Prefer supported FAST noise reduction and leave other ISP choices to
the camera. This does not disable vendor processing or guarantee RAW detail.

## SPEC_04/05: three continuous-tone filters

Remove Sauvola/B&W and Grayscale from the public enum, JNI dispatch and all
editing surfaces. Add Paper (Chinese: 类纸), retaining Original and Magic.
Paper is a conservative color paper rendering, not binary or monochrome.

Estimate smooth paper illumination from a reduced image with a fixed 512-pixel
long-edge reference scale, morphological closing for foreground suppression and smoothing.
Use bounded luminance normalization, a soft white point and continuous contrast.
Preserve faint differences below the paper level and retain chroma. No bright
pixel threshold that maps a range of faint marks to white, no full-resolution
integral images and no repeated appearance processing of a prior filtered JPEG.
Magic uses the same protected estimator with stronger normalization/contrast.
Paper is gentler. Dark or predominantly colored backgrounds reduce whitening.
This model does not identify all ink or recover text hidden by clipping/shadows.

Use tiled Lab conversion to bound temporary memory. Neighborhoods are specified
relative to the same reference scale at preview/export; quantized reduced
previews can still differ. Full immutable edit recipes and comprehensive color
profile management remain separate unresolved requirements.

## Acceptance additions to SPEC_06

- SCAN-05: synthetic jitter, alternating nested targets, isolated misses,
  persistent target changes, invalid quads and rotation/dimension changes.
  Alternation cannot switch the selected target; stale geometry expires.
- GEO-04: common-coverage crop round trips with rotations 0/90/180/270,
  asymmetric source margins and scales 1/2, within one output pixel.
- PHOTO-06: known subpixel shifted text with two depth motions, one blurred frame
  and an occlusion. Compare local registration/residuals with global-only merge;
  record selected reference, actual contributors, border loss and stroke error.
  Single-region reference protection is allowed; globally disabling fusion is not
  a quality fix. Physical-device text charts remain required before verified SR.
- FILT-06: faint gray strokes, colored ink, uneven cream paper, dark paper,
  tiny images and BGRA/gray inputs. Confirm continuous tonal differences,
  no hard-white collapse and source immutability. Preview/export comparison and
  representative real paper are required for complete acceptance.
- UI-04: every editing surface exposes exactly Original, Magic and Paper.

Implementation and test results are recorded separately; this contract alone is
not a passed acceptance profile.

## Public research basis

[Wronski et al., Handheld Multi-Frame Super-Resolution (2019)](https://research.google/pubs/handheld-multi-frame-super-resolution/)
motivates local correspondence checks and separate sample coverage evaluation.
Their RAW algorithm and performance claims do not describe this JPEG pipeline.
[Zhang et al., Document Image Shadow Removal Guided by Color-Aware Background (2023)](https://openaccess.thecvf.com/content/CVPR2023/papers/Zhang_Document_Image_Shadow_Removal_Guided_by_Color-Aware_Background_CVPR_2023_paper.pdf)
motivates distinguishing paper background from colored foreground. No network,
weights or proprietary implementation is imported; the proposed filter is a
bounded classical illumination model with explicit limits.

## Validation-driven refinement

A known 0.65/-0.40-pixel text shift exposed an incorrect rejection assumption:
comparing raw high-pass energy after interpolation to an unresampled reference
penalizes valid subpixel phases. Validate correlation in a 0.8-to-2.0-pixel
Gaussian band instead, with a 0.68 relative-energy floor. Retain the per-pixel
residual gate and reject the deliberately blurred-text fixture. Two-dimensional
phase acceptance also requires at least 15% validated textured grid nodes in
participating frames; flat-region noise support alone is not detail evidence.
These are initial synthetic-fixture settings and need real-burst calibration.
