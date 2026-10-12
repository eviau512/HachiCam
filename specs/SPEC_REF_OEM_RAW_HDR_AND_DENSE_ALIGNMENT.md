# Research Reference: OEM RAW HDR and Dense Alignment

> Status: Non-authoritative research context, revised 2026-10-12.
> Language: English. Current contracts are SPEC_00 through SPEC_06.

## 1. Evidence correction

Earlier versions of this document inferred internal frame allocation, proprietary
processing stages, exact parameter values and hardware activation from metadata
labels and general architecture descriptions. Those inferences were not verified
by runtime instrumentation and must not be treated as established facts or
implementation directives.

No metadata label, packaged component or standard quality request establishes
which proprietary algorithm executed, how many frames it used, what its private
parameters were, or whether a third party application can invoke the same path.
Earlier unconditional zero-noise, zero-ghosting and complete native-detail claims
are withdrawn.

## 2. Portable requirement

Use supported public camera controls and actual returned metadata.
[Android's noise-reduction contract](https://developer.android.com/reference/android/hardware/camera2/CaptureRequest#NOISE_REDUCTION_MODE)
describes device-selected quality processing and potential capture-rate cost; it
does not promise a named temporal engine or OEM-equivalent multi-frame pipeline.

RAW capture, dense alignment and vendor-specific processing remain unimplemented
or unverified in HachiCam. Their suitability requires public capability evidence,
independent implementation, resource measurements and the quality cases in
[SPEC_06](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md).

## 3. Independent engineering implications

Repeated text and parallax can make a global registration look plausible while
individual strokes disagree. Validate local reliability and preserve the source
where evidence fails. Do not categorically discard all reliable edge samples;
that conflicts with sampling reconstruction. Parameter choices require
independent tuning, not transfer of proprietary tables.

More output pixels are not proof of added detail. Clipped or occluded content
cannot be restored without valid observation. Use measured comparisons against a
single-frame source, with separate noise, stroke, ghosting and color outcomes.

## 4. Authority and isolation

This file supplies context only. SPEC_01 governs capture, SPEC_02 reconstruction,
SPEC_03 crop, SPEC_04 filters and SPEC_06 acceptance. No code or vendor-specific
implementation recipe from the old reference is an active engineering contract.

The 2026-10-12 APK study is documented separately as sanitized specification
differences under research/. Its static observations do not validate the older
OEM claims.
