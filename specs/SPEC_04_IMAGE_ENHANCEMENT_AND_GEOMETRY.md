# Specification 04: Document Enhancement Filters and Geometry

> Status: Authoritative. Revised 2026-10-12.
> Language: English.
>
> Quality revision contract: [2026-10-12 follow-up](research/QUALITY_REVISION_2026-10-12.md). Experimental implementation; numerical tuning and physical-device acceptance remain unverified.

## 1. Scope and stage order

Geometry rectification and document appearance are separate stages:
decode and normalize -> validate crop -> rectify -> selected appearance processing
-> requested output rotation/encoding. Preview and final rendering must use the
same source and edit recipe, subject to documented resolution approximations.

**Implemented:** Original, Magic Color and Paper.
Photo, automatic book flattening, finger removal and content restoration are
not complete shipping features.

## 2. Source preservation and processing recipe

**Required / partly implemented:** derive every filter selection from the source
and current geometry, never from the previous filtered JPEG. Original means
geometry-corrected source appearance; it is not a promise of unchanged source
bytes or a lossless JPEG transform.

Keep the imported/captured master immutable. Express rotation, crop, aspect
choice and filter settings as a recipe; save derivative results independently.
Review generally re-renders from the source, but crop rotation currently rewrites
its input file. Complete source immutability remains **required / not implemented**.

The recipe shall define algorithm version, parameter units, border handling,
color space, bit depth and output scale. Native BGR storage order is not a color
profile. A future normalized SDR/sRGB pipeline must convert tagged/wide-gamut
inputs explicitly and define alpha compositing before JPEG encoding.
End-to-end profile and HDR/gain-map handling remain **required / not implemented**;
do not promise preserved wide-gamut or HDR output.

## 3. Perspective correction and aspect

**Implemented:** target size starts from opposite-edge lengths; homography warp
uses linear interpolation. A user-selected width/height ratio constrains output
shape. Free mode applies an experimental opposite-edge foreshortening heuristic.

Four uncalibrated corners do not uniquely establish a physical paper aspect ratio.
Free mode must not be described as calibrated pitch recovery. Known paper ratio
or independent calibration is required to validate physical proportions.

**Required / partly implemented:**

- Apply SPEC_03 geometry validation before solving or allocating a warp.
- Bound output dimensions and resampling memory.
- Specify edge/center coordinate conversion and out-of-source sample handling.
- Use antialiasing for reduction and avoid repeated full-image resampling.
- Retain straight lines, text topology and readable strokes; interpolation
  cannot recover detail absent from the source.

Linear interpolation describes the current implementation; cubic is a possible
quality experiment, not a claim about current behavior. Arbitrary aspect changes
are user directed transformations, not evidence of accurate physical dimensions.

## 4. Filter semantics and implementation status

| Mode | Current behavior | Quality contract |
| --- | --- | --- |
| Original | Rectification with no appearance filter | Retain source appearance subject to declared geometry/encoding |
| Magic Color | Stronger continuous Lab luminance illumination normalization | Reduce uneven lighting, preserve faint marks and colored ink |
| Paper | Gentler continuous color paper rendering | Lift paper illumination without hard-white classification or loss of faint tonal distinctions |
| Photo | No separate implemented filter mode | Future natural-tone profile |
| Strict binary | No implemented mode | Future distinct mode if requested; not a Paper claim |

B&W/Sauvola and Grayscale are removed from all editing surfaces and native filter
dispatch at the user's request. The three active choices are Original, Magic and
Paper. There are no persisted filter recipes to migrate in the current in-memory
session model.

### 4.1 Illumination normalization

**Experimental implementation (paper-v1):** estimate Lab luminance illumination
at a 512-pixel long-edge reference scale using morphological closing and Gaussian
smoothing. Sample this small field directly during bounded tile Lab conversion.
Both Paper and Magic use continuous protected luminance normalization and a soft
white shoulder. Paper has weaker normalization/contrast; color and dark-background
support reduce whitening strength. Lab chroma is retained. There is no hard bright
pixel threshold, full-size float background or double-precision integral image.
The exact initial parameters live in `paper_math.h` and the quality revision.
No unsharp mask, learned content reconstruction or binary classification is used.

Slow illumination separation is an approximation. It cannot reliably distinguish
all paper/ink colors, eliminate opaque shadows or recover clipped text. Original
remains necessary for photographic/natural-tone use. Color profile management and
an interactive configurable strength recipe remain incomplete.

### 4.2 Scale and borders

The same reference-scale estimator and replicate border policy apply to preview
and full output. Tile boundaries do not change the field. Quantization and small
preview loss can still change measured marks; scale parity on real receipts and
faint pencil is unverified. Tiny gray/BGR/BGRA inputs are normalized before this
filter, while wide-gamut/alpha policy remains the separate input contract.

### 4.3 Detail and color preservation

**Required / unverified:** compare against source crops using faint pencil,
thermal receipts, small digits, punctuation, table rules, stamps, colored ink,
highlighter, illustrations and dark paper. Whitening must not erase evidence;
sharpening must not merge strokes, create halos or change numbers.
Distinguish lower noise from lower detail.

Photographic images require natural-tone handling; automatic paper whitening
must be an explicit document profile choice, not a generic photographic default.

## 5. Preview, final rendering and edit transactions

**Required / not fully implemented:**

- Render previews from a reduced source with the same recipe and scale-aware
  parameters; final output is independently rendered from the master.
- Include source revision, geometry, filter version/parameters, dimensions and
  request generation in cache identity.
- Cancel or discard obsolete render completions. A slow earlier selection must
  not overwrite a newer filter or resurrect a deleted page.
- Preserve the last valid rendering until a replacement saves successfully.
  Use temporary output plus commit; encoding failure must not publish success.
- Isolate concurrent page edits and guarantee native resource ownership,
  including cancellation, JNI failures and page navigation.

A source-based render path and background coroutines already exist in places;
they do not prove atomic edits, request freshness or complete thumbnail parity.

## 6. Book curvature, glare and occlusion

Existing native curved-surface remap primitives are **experimental**. Automatic
text-baseline estimation, reliable page-surface inference and a connected book
flattening workflow are **not implemented**. A planar homography cannot flatten
a genuinely curved page. Future dewarping needs confidence, original comparison,
manual fallback and straight-line/stroke acceptance.

Finger removal, skin segmentation and inpainting are **not implemented**.
Any future cosmetic fill must be restricted to confirmed non-content margins and
remain reversible. Hidden text, signatures or stamps cannot be truthfully
reconstructed; generative or inpainting output must never be presented as
recovered document evidence.

## 7. Acceptance

Apply FILT-01 through FILT-05, GEO-01 through GEO-03 and LIFE-01 through LIFE-03
in [SPEC_06](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md).
Quality thresholds require calibration on independently created/licensed
fixtures. No competitor constants or copied parameter tables are adopted.

This revision corrects filter semantics and specifies missing protection and
failure contracts. It does not claim new filtering functionality or completed
real-device image-quality verification.
