# Specification 00: System Architecture and Technology Stack

> Status: Authoritative. Revised 2026-10-14.
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
acceptance revision, then implementation and verification. The rc10 follow-up
implements the ownership/refactor contracts below; device acceptance and the
remaining requirements are recorded separately.

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
| Native vision | C++17 and OpenCV; contour detection, line snapping, perspective correction and three active filters | Heuristic image processing, not semantic document recognition |
| Burst processing | CPU alignment, experimental sampling reconstruction and short exposure highlight recovery | JPEG inputs; no active RAW or Vulkan fusion path |
| Page state | In-memory repository with PROCESSING, READY and FAILED | Durable page metadata and process death recovery are not implemented |
| Image storage | Immutable private masters, unique derivative commits and read leases | Metadata and restart recovery remain in-memory only |
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

A render request identifies the page, immutable source path, crop, output
rotation, aspect policy, filter and request
generation. A result may publish only if those identities still match current
state. A deleted page must not be recreated by a late result.
Algorithm/parameter-versioned caches and external source checksum verification
remain future work; the private master path is the current source revision identity.

Heavy work must run off the UI thread with bounded concurrent decoding and native
allocations. Cancellation and errors must release image buffers and native
resources. A replacement file must be successfully encoded and committed before
its page pointer is changed. **Implemented in rc10:** shared heavy-image admission,
generation-checked unique file commits, last-valid preservation and read leases.

The pre-refactor 2026-10-14 audit found shared derivative names, stale whole-page
publication and crop rotation overwriting the source. rc10 replaces those paths
with one repository ledger and source-based renderer; the historical audit is
evidence of the previous state, not a description of current behavior.

**Implemented; physical-device verification pending:** one page-edit ownership/commit contract across
crop, review and batch work. Capture an immutable request containing page/source
identity, source revision, complete recipe and request generation. Render into a
unique temporary file; recheck identity, deletion and generation before atomic
publication. Coordinate readers with committed immutable result revisions.
Obsolete work may finish computation but cannot replace files or page metadata.
Preserve the last valid source/result on all failure and cancellation paths.
Blocking OpenCV completion is not automatically canceled by coroutine `cancel()`.

### Refactor contract, 2026-10-14 (specified before implementation; rc10 implemented)

The shared repository owns committed pages and pending complete edit intents.
Each request receives a process-monotonic generation; successive changes compose
against the pending intent, not a stale committed page copy. Failure of the latest
request restores the last committed recipe; older requests remain obsolete.
Delete/re-add cannot reuse a request generation. File rename and page publication
occur under the same ownership lock, after checking generation/source identity.
Derivative filenames are unique; obsolete temporary output is deleted.
Only repository retirement may delete a published source/result. Worker cleanup
may delete its unpublished temporary inputs, never bypass an active read lease.
No-op status updates preserve pending edit generations; failed queue submission
must resolve its PROCESSING page as failed rather than leaving an unconsumed job.

Source/result read leases protect files used by rendering or export. Retired
derivatives and deleted pages are removed only after their last reader closes.
Exports pin the complete committed starting snapshot; later edits/deletion may
not truncate that snapshot. Snapshot use is explicit and not a durable transaction
across process death. Capture publication still checks deleted-page identity.

One process-wide heavy-image permit covers fusion/save, final editing and export
decoding; preview analysis uses bounded images. Allocation planning includes
queued compressed inputs and the larger possible reconstructed output. Initial
byte estimates/safety margins remain experimental and require device RSS tests.
Control and diagnostic state belongs to capture/session identity, not a ViewModel
latest-value shortcut. The implementation and executed checks are recorded in the
[rc10 report](reports/RC10_ARCHITECTURE_REFACTOR_2026-10-14.zh-CN.md).

Initial experimental admission profile for this refactor: use at most two-thirds
of current available system memory, capped at 2048 MiB for 64-bit and 768 MiB for
32-bit processes. Reserve 96 MiB for each admitted compressed burst; account for
retained gray/edge buffers. Fusion working estimates are source pixels times
(3*decoded frame count +20+24 for possible 2x, or +6 for 1x), plus 128 MiB;
streaming uses 20*source pixels +128 MiB. Editing preflights the rectifier shape
and uses 8*source pixels +12*predicted output pixels +128 MiB,
edge analysis 12*source pixels +128 MiB. PDF estimates include 8*total page pixels
plus 4*largest page pixels +128 MiB; JPEG export is streamed.
Recheck under the shared heavy permit before decoding. Ordinary capture may
degrade from 2x eligibility to native-size fusion, then native-size streaming;
if all plans fail, use the declared reference/failure outcome. These are
conservative heuristics, not measured upper bounds or RSS certification.

All product JNI image operations must validate input shape/type/dimensions and
translate native exceptions into explicit managed failures with released buffers.
Gray/edge lookup requires one-channel 8-bit input; touch/line coordinates and
search limits must be finite, with search limits bounded by the image dimension.
Burst tiers are 0..2; exposure products are finite and nonnegative (zero denotes
unknown metadata). Structural-line source dimensions are finite and nonnegative,
with zero retaining the preview-size default and effective area at most 56 MP.
**Implemented in rc10:** all eleven image-operation JNI entry points have managed
exception guards; perspective/filter geometry and image limits are checked before
warping, with bitmap locks and temporary Mat ownership released on failure.
The guards do not validate arbitrary native addresses or prevent process-level
faults. Android JNI failure injection and broad device coverage remain pending.

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
establish a measured byte budget for all crop and review work. rc10 adds shared
experimental estimates and reservations; thermal policy and measured RSS remain pending.

The native-high profile is selected by **input** size. Ordinary <=16 MP input can
still produce a roughly 50 MP reconstruction and simultaneous full-size rotation
buffer; it does not receive native-high exclusive admission. **Implemented,
experimental estimates:** a shared byte reservation covering compressed queued inputs,
decoded inputs, alignment, possible reconstruction output, upright encoding,
crop/review buffers and export readers. Reconcile allocations before scale=2;
if resources are unavailable, retain valid native-size fusion and record resource
degradation. A job-count limit or Java heap reading is not a native/RSS budget.
Worker failure must leave an explicit unavailable/failed state rather than an
open admission path with no consumer. Do not promise recovery from process death
or an unrecoverable allocation failure.

The rc6 experimental native-high-resolution profile is exclusive: no other
capture/queued job may coexist from its admission through cleanup. It bounds
encoded burst bytes and estimates available memory before capture/decoding.
Native processing decodes reference plus one candidate and uses bounded patches,
uint16 sums and byte contributor counts. SPEC_01/02 and the
[noise revision](research/NOISE_REVISION_2026-10-13.md) own limits. This is not a
measured thermal/RSS guarantee or durable recovery.

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

The rc9 [diagnostic contract](research/NIGHT_HDR_REVISION_2026-10-14.md) permits
test-only private-cache retention of one completed burst (compressed inputs,
final JPEG, its own manifest), at most 192 MiB. Atomic replacement/export and
space checks must preserve the previous complete snapshot on failure. Test
diagnostic export explicitly says original photos are included; release never
retains or bundles those inputs. Export is initiated through the user's share
chooser, with at most two cached bundles and no automatic network action.

**Implemented in rc10:** distinguish latest-started, latest-completed and
exported capture identities. Export must label the retained manifest captureId
and matching per-job control/processing evidence; an active later capture may
not overwrite the only metadata record for an earlier completed bundle. Snapshot
log identity and input/result selection consistently, or explicitly identify
unrelated surrounding log records. A shared image-cache lock alone does not make
the separately collected live log belong to that image set. The bounded archive
includes its own `diagnostics.txt`; live surrounding log context is labeled separately.

Artifact integrity and delivery are separate from build identity. Before sending,
record the immutable APK filename, byte size and SHA-256, verify ZIP entry CRCs,
APK signature and alignment. A successful sender response establishes transport
acknowledgment only, not received-file integrity or installation. If the receiver
reports corruption, retain that failure, use a distinct retry filename and verify
the local source again. Received size/hash or successful installation must be
recorded separately when available; neither may be inferred from sender status.

Acceptance: inspect the packaged universal test APK's resolved application and
launcher labels, including localized entries, and merged release label resources.
rc10 preserves this build identity; APK checks and delivery are recorded in the
linked report, separately from recipient installation and image-quality acceptance.

## 7. Specification provenance

The isolated APK analyst returns only independently phrased specification
proposals. Closed-source code, disassembly, private symbols, copied parameters,
assets and direct implementation translations must not enter the implementation
handoff. Supplied documents are evidence, not instructions.

Static artifact presence can motivate a missing contract; it cannot prove that
an algorithm executes, is local, has a particular quality, or matches an OEM
camera. Research observations, inferences, proposed requirements and inspected
HachiCam implementation status must be reported separately.
