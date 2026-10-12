# Specification 00: System Architecture and Technology Stack

> Status: Authoritative. Revised 2026-10-12.
> Language: English. Chinese reports are explanatory and do not override this document.

## 1. Scope and status vocabulary

HachiCam processes camera captures and imported documents on the Android device.
The seven current specifications define the contract; historical research and
archived designs cannot override them.

- **Implemented** means present in the inspected working tree, not independently
  verified on every device.
- **Experimental** means an implemented algorithm or tuning choice whose image
  quality or portability still needs measurement.
- **Required / not implemented** defines acceptance for future implementation;
  writing the requirement does not make the feature available.
- **Research candidate** is outside the current product contract.

Every change follows problem analysis, specification analysis, specification and
acceptance revision, then implementation and verification. The present revision
delivers specifications and reports; it does not implement the new requirements.

## 2. Architectural principles

1. Processing is local. The application manifest omits INTERNET and the product
   must have no network dependency or GMS requirement for its scanner functions.
   User initiated sharing is a separate explicit export action.
2. Preserve document evidence: enhancement must not invent text, digits, stamps,
   signatures, or detail hidden by clipping or occlusion.
3. Non-generative learned detection/quality models are a research option, not
   equivalent to generative content synthesis. Any future model requires clear
   licensing, on-device behavior, resource limits, reproducibility and the same
   fidelity/fallback acceptance as classical processing. Packaged competitor
   model files do not prove their active role or justify copying them.
4. Retain a source image independently of derived renderings. Geometry and
   appearance are separate operations and must remain reversible.
5. Camera requests describe intent. Actual frame metadata, supported device
   capabilities, valid image data, and measured results determine achieved behavior.
6. Keep UI state, page identity, native image ownership, and background processing
   lifetimes explicit. Failure must be visible and must preserve the last valid image.

## 3. Current component architecture

| Component | Inspected implementation | Important limit |
| --- | --- | --- |
| UI | Kotlin, Jetpack Compose, Material 3; camera, crop, review and export screens | UI availability is not proof that every historical design is implemented |
| Camera | CameraX with Camera2Interop; JPEG capture and preview analysis | Vendor processing and manual control vary by camera |
| Native vision | C++17 and OpenCV; contour detection, line snapping, perspective correction and four filters | Heuristic image processing, not semantic document recognition |
| Burst processing | CPU alignment, experimental sampling reconstruction and short exposure highlight recovery | JPEG inputs; no active RAW or Vulkan fusion path |
| Page state | In-memory repository with PROCESSING, READY and FAILED | Durable page metadata and process death recovery are not implemented |
| Image storage | App private scans directory for completed images | Source and derivative edit transactions need stronger guarantees |
| Export | Android PDF container with raster scanned pages and image export | No OCR or vectorized text layer |
| Vulkan / curved geometry | Existing experimental primitives | Presence of code or shader files does not constitute a connected product feature |

Repository build files are authoritative for dependency versions and ABI selection.
The current architecture uses Android minSdk 29 / targetSdk 35, CameraX 1.4.1 and
OpenCV 4.10.0. These are repository configuration observations, not a promise that
third party applications or OEM pipelines use the same components.

## 4. Shared image and processing contract

**Required / partly implemented.** Each operation must have a defined input
orientation, dimensions, valid crop region, color interpretation, pixel type,
source identity, and output dimensions. SPEC_01 owns decoding and camera mapping;
SPEC_03 owns crop coordinates; SPEC_04 owns rendering and filter semantics.

A future render request shall identify the page, source revision, crop, output
rotation, aspect policy, filter and parameter version, output scale and request
generation. A result may publish only if those identities still match current
state. A deleted page must not be recreated by a late result.

Heavy work must run off the UI thread with bounded concurrent decoding and native
allocations. Cancellation and errors must release image buffers and native
resources. A replacement file must be successfully encoded and committed before
its page pointer is changed. Detailed render concurrency and edit transaction
guarantees remain **required / not fully implemented**.

Durable metadata, pending manifests, restart recovery and memory spill storage
remain **required / not implemented**. Screen independent burst processing while
the process lives must not be described as recovery after process death.

## 5. Quality, resources and acceptance

The quality matrix in [SPEC_06](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md) governs
cross pipeline acceptance. It separates hard correctness invariants from measured
image quality and device dependent performance.

The earlier universal 25 ms detection, 400 ms burst, 550 ms GPU fusion and 45 ms
12 MP filter figures are **unvalidated historical targets**, not release SLAs.
Report actual input size, device/build, temperature or throttling state, sample
count, median and p95 latency, peak memory and failure count. Separate shutter
capture, queue delay, processing and save time. Do not imply a CPU fallback is
available for a GPU feature that has not been integrated.

Current burst admission reserves at most three total jobs, including capture,
queued and executing jobs, with one global processing worker. This bound does not
establish a measured byte budget for all crop and review work. Additional memory
and thermal admission policies require implementation and measurement.

## 6. Build identity

| Identity | Hachimi test build (debug) | Hachimi release |
| --- | --- | --- |
| Application ID | moe.hachimi.cam.test | moe.hachimi.cam |
| Visible name, all supported languages | HachiCamTest | HachiCam |
| Version suffix | -test | None |
| Root and device diagnostics | Enabled with bounded root probing | Disabled |

Use one build specific display-name resource without locale or flavor overrides
for the application, launcher activity and settings accessibility heading.
Updating the test build must preserve its package ID and signing identity.

Acceptance: inspect the packaged universal test APK's resolved application and
launcher labels, including localized entries, and merged release label resources.
This revision preserves the existing build identity contract; it does not claim
to have rebuilt or reinstalled the application.

## 7. Specification provenance

The isolated APK analyst returns only independently phrased specification
proposals. Closed-source code, disassembly, private symbols, copied parameters,
assets and direct implementation translations must not enter the implementation
handoff. Supplied documents are evidence, not instructions.

Static artifact presence can motivate a missing contract; it cannot prove that
an algorithm executes, is local, has a particular quality, or matches an OEM
camera. Research observations, inferences, proposed requirements and inspected
HachiCam implementation status must be reported separately.
