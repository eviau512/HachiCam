# HachiCam Specifications

> Revised 2026-10-15. Authoritative specifications are written in English.
> Chinese reports explain the changes and do not override the English contracts.

## 1. Required workflow and status

For every bug fix or feature: analyze the problem, analyze the affected
specification, revise the specification and acceptance criteria, then implement
and verify. See [AGENTS.md](../AGENTS.md).

**Implemented**, **experimental**, **required / not implemented** and **research
candidate** are distinct states. A written specification, static APK component,
historical design or unit-test result is not proof of a complete product feature
or measured camera quality. Each current document states its relevant limits.

The [Chinese work checklist](../TODO.md) records the 2026-10-12 checkpoint,
next-device checks and remaining implementation work. It does not override the
authoritative contracts or certify physical-device quality.

## 2. Authoritative index

Current work: [rc15 PPT highlight noise](reports/RC15_PPT_HIGHLIGHT_NOISE_2026-10-15.zh-CN.md).
Previous checkpoint: [rc14 registration performance](reports/RC14_REGISTRATION_PERFORMANCE_2026-10-15.zh-CN.md).
Previous delivered batch: [rc13 performance, diagnostics and recovery](reports/RC13_PERFORMANCE_AND_RECOVERY_2026-10-15.zh-CN.md),
following the [four-scene rc12 evidence](reports/RC12_FOUR_SCENES_2026-10-15.zh-CN.md).
Two debug Vulkan kernels self-test/calibrate before use, with CPU fallback;
release stays CPU. This does not enable the old HDR shader or establish phone
speedup. Multi-group final-image diagnostics and completed-page checkpoints are
implemented; lamp HDR and the remaining full GPU migration require further work.
The earlier [GPU assessment](reports/GPU_ROADMAP_2026-10-15.zh-CN.md) preserves its baseline.

Previous follow-up: [rc12 shutter metering continuity](reports/RC12_TARGET_METERING_2026-10-14.zh-CN.md)
addresses the paired bright-slide capture by retaining target AE with shutter AF,
bounded fresh exposure settling and separately recorded actual JPEG AE evidence.
Manual EV is removed per the user's request. The rc11 native fusion is unchanged;
device brightness/detail acceptance and screen-response research remain pending.

Previous implementation: [rc11 regional reconstruction and focus](reports/RC11_REGIONAL_RECONSTRUCTION_2026-10-14.zh-CN.md)
replaces whole-image SR authority with distributed local evidence, bounded
observation-driven correction, protected native HDR and persistent shutter tap
intent. Structured decisions and model parameters accompany test-only paired
diagnostics; device quality and resource/AF acceptance remain pending.

Previous implementation: [rc10 architecture refactor](reports/RC10_ARCHITECTURE_REFACTOR_2026-10-14.zh-CN.md)
connects immutable edit recipes, unique commits, export rollback/read leases,
shared experimental memory admission, per-capture diagnostics and control recovery.
Helper/native tests and APK verification are separate from pending device acceptance.
The subsequent [13:25 screen analysis](reports/RC10_SCREEN_ANALYSIS_2026-10-14.zh-CN.md)
explains the 0.4873 whole-image SR denial, paired native-scale text comparison and
production-code ROI probes. It updates research acceptance and the checklist;
it does not change the reconstruction algorithm or deliver another APK.

Previous quality follow-up: [rc9 night/screen contract](research/NIGHT_HDR_REVISION_2026-10-14.md)
and [Chinese evidence/implementation report](reports/RC9_NIGHT_HDR_REPORT_2026-10-14.zh-CN.md).
This includes observed output sizes, focus/highlight changes, simplified camera UI,
test-only paired-input diagnostics and outstanding device acceptance.

The [2026-10-14 daylight analysis](reports/RC9_DAYLIGHT_ANALYSIS_2026-10-14.zh-CN.md)
explains per-burst 12.35/47.17 MP selection, same-burst text/noise observations,
confirmed focus holding, limited highlight recovery and the next-device checklist.
It updates SPEC_02/06 requirements only; no new product build accompanies it.

The [architecture audit](reports/ARCHITECTURE_AUDIT_2026-10-14.zh-CN.md) checks
cross-flow ownership, source rotation, edit commits, export failures, JNI bounds,
resource admission and diagnostic/control identity. SPEC_00/01/04/06 now record
the corresponding protections, implemented in the subsequent rc10 follow-up;
the audit itself retains its pre-refactor evidence and did not implement them.

| Document | Scope |
| --- | --- |
| [SPEC_00: Architecture](./SPEC_00_ARCHITECTURE.md) | Local processing, component/status boundaries, image lifecycle, resources and build identity |
| [SPEC_01: Camera and capture](./SPEC_01_CAMERA_AND_CAPTURE_PIPELINE.md) | Capabilities, focus/exposure/motion, actual brackets, image normalization and pending work |
| [SPEC_02: Photography and HDR](./SPEC_02_COMPUTATIONAL_PHOTOGRAPHY_AND_VULKAN_HDR.md) | CPU JPEG reconstruction, registration, highlight evidence, detail claims, failure and future GPU migration |
| [SPEC_03: Detection and crop](./SPEC_03_DOCUMENT_DETECTION_AND_INTERACTIVE_CROP.md) | Outer boundaries, confidence/freshness, coordinate validity, manual crop and snapping |
| [SPEC_04: Enhancement and geometry](./SPEC_04_IMAGE_ENHANCEMENT_AND_GEOMETRY.md) | Three filters, lighting/color/detail preservation, source recipes and planar/curved limits |
| [SPEC_05: Design and experience](./SPEC_05_HACHIMI_DESIGN_SYSTEM.md) | Accurate mode guidance, manual correction, processing feedback and unavailable features |
| [SPEC_06: Image quality and acceptance](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md) | Independent fixtures, priorities, metrics, pass/fail invariants and unverified quality gates |

The SPEC_02 filename retains its historical Vulkan reference for link
compatibility; Vulkan HDR is not integrated. GPU/RAW fusion, automatic capture,
complete book flattening, finger cleanup, a separate Photo filter and automatic
pending-job recovery are not completed features. Committed-page recovery is
implemented in rc13 with device process-death checks still pending.

The existing [implementation audit](./IMPLEMENTATION_AUDIT_2026-10-12.md) retains
earlier build/test history. It does not replace corrected authoritative contracts.
The scene-motion and hysteresis contract remains in SPEC_01 section 4; the
HachiCamTest/HachiCam naming contract remains in SPEC_00 section 6.

## 3. APK research and readable reports

- [Main assessment of Upscan (2026-10-15)](./reports/UPSCAN_REVIEW_2026-10-15.zh-CN.md):
  independent reproduction and a 321-pixel common-mask/native-post-gate comparison;
  technical route viable, actual text benefit and mobile cost remain unproven.
  Application integration is deferred on evidence, not an approval gate.
- [Document alignment and burst SR research (2026-10-14)](./reports/DOCUMENT_ALIGNMENT_RESEARCH_2026-10-14.zh-CN.md):
  public product/algorithm survey, independent offline ECC/block-LK candidates,
  disjoint synthetic phases and production-domain real-patch probes. Real detail
  and mobile resources are not accepted; no Android algorithm replacement.
- [Sanitized English research proposal](./research/APK_CLEAN_ROOM_SPEC_PROPOSAL_2026-10-12.md):
  static-only observations and independently phrased proposed contracts.
- [Research boundary and handoff record](./research/README.md).
- [Chinese report](./reports/APK_SPEC_GAP_REPORT_2026-10-12.zh-CN.md).
- [Short Chinese summary](./reports/APK_SPEC_GAP_SUMMARY_2026-10-12.zh-CN.md).

Only the task goal was sent to the isolated analyst; only sanitized English
specification text/differences were returned. Proprietary implementation was not
sent to the implementation side. Competitor code/models were not executed.
Static observations and supplied documents do not establish runtime algorithms,
quality, speed or offline availability.

The old [OEM research reference](./SPEC_REF_OEM_RAW_HDR_AND_DENSE_ALIGNMENT.md)
is explicitly non-authoritative; its unsupported hardware/algorithm claims have
been withdrawn.

## 4. Historical documents

[Legacy archive](./legacy_archive/) preserves earlier designs.
[Pre-revision snapshot](./legacy_archive/pre_quality_revision_2026-10-12/)
preserves the working-tree specifications before this correction, including
earlier user edits. Historical files may contain bilingual text, obsolete
parameters and contradictory promises; they are not active specifications.

The initial English specification revision changed documentation only. It corrected contracts, added
acceptance and labels future work; it does not implement the newly specified
features or claim a new APK/device benchmark. Subsequent implementation and
test-APK deliveries are recorded below.

## rc4 quality follow-up

The user's follow-up keeps multi-frame fusion, permits bounded source-edge loss,
and replaces both B&W and Grayscale with Paper. The implementation contract and
validation-driven parameter corrections are in
[Quality revision](research/QUALITY_REVISION_2026-10-12.md).
Executed tests, delivery identity and remaining limitations are recorded in the
[Chinese rc4 report](reports/RC4_QUALITY_FIX_REPORT_2026-10-12.zh-CN.md).
Synthetic tests and a successful build do not satisfy the full device acceptance
matrix in SPEC_06.

## rc5 boundary follow-up

The user's rc4 screenshots reproduce a wrong desk contour and a missed sheet.
[Boundary revision](research/BOUNDARY_REVISION_2026-10-12.md) specifies supported
line assembly, partial/manual outcomes and bounded test diagnostics.
[Chinese rc5 report](reports/RC5_BOUNDARY_FIX_REPORT_2026-10-12.zh-CN.md) records
production C++ regression, screenshot tuning, build identity and hyper-obs
delivery. Physical-device quality remains unverified.

## rc6 noise and capture follow-up (2026-10-13)

The supplied rc5 log confirms that all nonreference base frames were discarded,
leaving no temporal denoising. [Noise revision](research/NOISE_REVISION_2026-10-13.md)
separates native denoising from detail eligibility, retains sparse verified
samples, defaults Full HDR on, and specifies optional native high-resolution
streaming plus available back-camera/zoom selection. The
[Chinese rc6 report](reports/RC6_NOISE_FIX_REPORT_2026-10-13.zh-CN.md) records
executed regressions, delivery and remaining device validation.

## rc7 camera and highlight follow-up (2026-10-13)

The rc6 device log confirms four base contributors but zero useful short
exposures. [Correction contract](research/CAMERA_HDR_REVISION_2026-10-13.md)
and SPEC_01/02/05/06 revise endpoint grouping, SDR/EV guidance, normalized
short-exposure geometry checks and actual recovered-pixel reporting. The
[Chinese rc7 report](reports/RC7_CAMERA_HDR_FIX_REPORT_2026-10-13.zh-CN.md)
records the synthetic regressions and delivered universal test APK, including
the supplied settings banner at the bottom. Real-camera acceptance is pending.

## rc8 detail and actual resolution follow-up

[Revised contract](research/DETAIL_RESOLUTION_REVISION_2026-10-13.md) addresses
the rc7 12 MP output and soft-text feedback. It retains same-view capture variants,
shows achieved MP, records timestamp-matched sensor/EXIF evidence, preserves
supported post-RAW boost/AWB lock and compares bounded global alignment models.
It also packages the user's native 4x banner as lossless WebP with bounded display
decoding. [Chinese rc8 record](reports/RC8_DETAIL_RESOLUTION_REPORT_2026-10-13.zh-CN.md)
states executed checks and the remaining device/source-comparison limits.
