# Specification 05: Hachimi Design System and User Experience

> Status: Authoritative. Revised 2026-10-12.
> Language: English.
>
> Quality revision contract: [2026-10-12 follow-up](research/QUALITY_REVISION_2026-10-12.md). Experimental implementation; numerical tuning and physical-device acceptance remain unverified.

## 1. Visual identity

Use Material 3 with unobtrusive camera guidance and legible dark surfaces.

| Token | Color | Role |
| --- | --- | --- |
| PrimaryTeal | #008080 | Active actions, crop handles and crosshairs |
| CherryPink | #FFB7C5 | Mode accents and capture animation |
| AmoledBackground | #121212 | Dark background |
| SurfaceDark | #1E1E1E | Cards and bottom surfaces |
| SurfaceElevated | #2A2A2A | Elevated surfaces |
| StabilityGreen | #4CAF50 | SPEC_01 scene-motion stability |
| TextPrimary | #F5F5F5 | Main text |
| TextSecondary | #9E9E9E | Secondary labels |

Color alone must not convey a processing failure or readiness decision.

The [rc5 boundary revision](research/BOUNDARY_REVISION_2026-10-12.md) requires a
dashed amber preview suggestion plus "Edges incomplete · adjust after capture"
for an occluded/out-of-frame quad. Such a suggestion never enables automatic
cropping. Missing/pending detection shows "No clear edges · crop manually";
complete acquired detection shows a green outline and "Edges found". These
messages are separate from Steady/Hold Steady. The existing disabled curvature
capsule is replaced by this useful boundary-status capsule; curvature remains
unavailable and has no enabled camera control.

## 2. Camera controls and capture feedback

The viewfinder provides torch, experimental HDR selection, settings, gallery
import, shutter and detection overlay. HDR mode indicates a requested processing
mode; it is not proof of seven accepted frames, recovered highlights or 50 MP
detail. Do not promise a universal 500 ms completion time.

Capture busy feedback covers camera acquisition, not the subsequent global
processing queue. Pending pages have clear progress; queue saturation and
fallback capture are visible. Manual capture remains available when scene
steadiness is unknown, subject to actual capture/admission availability.

### 2.1 Focus feedback

The existing tap ring is 72 x 72 dp with a 1.5 dp border and 8 dp corner radius;
entry scales from 1.2 to 1.0 over 150 ms, and fades after 1500 ms.
It acknowledges the tap. Without confirmed focus evidence it must not claim
successful focus. Metering excludes letterbox bars.

### 2.2 Steadiness feedback

Steady / Hold Steady and their colors consume the single scene-motion state in
SPEC_01 section 4. UI must not add a corner-variance test or reset motion state
because the detected contour changes. The indicator works without a document
quad and never acts as a shutter lock.

Apply SPEC_01 hysteresis and unknown-evidence expiry. HDR mode accent is
independent of steadiness. Future quality guidance must distinguish moving,
out of focus, overexposed, ambiguous crop and unknown, rather than presenting
one green state as proof of a good scan.

## 3. Crop and review behavior

**Implemented:** manual geometry controls, magnifier and three filter choices:
Original, Magic Color and Paper. Explain that Original retains
appearance with current geometry; it is not the uncropped file.

Paper is a continuous-tone color document rendering. It replaces both B&W and
Grayscale at the user's request. The crop editor, single-page review and batch
review must expose exactly the same three choices. Paper does not promise
bilevel output or restored hidden content.

Users must be able to compare with the source and undo appearance changes.
**Required / not fully implemented:** immutable source edits, stale-render
rejection, full preview/export parity and a last-valid-image failure state.
Filtering must not silently change crop or paper ratio.

Automatic book flattening and finger removal are unavailable until their
geometry and evidence-preservation criteria pass. Do not expose a control that
suggests these functions are complete.

## 4. Pages, export and future automatic capture

Page deletion and batch operations exist. Repository reordering support is not
proof that drag-and-drop reordering UI is implemented. Pre-rendered asynchronous
filter thumbnails and continuous 60 FPS remain targets pending verification.

Export uses the selected ready page recipe and current order. PROCESSING and
FAILED pages must not be silently exported as successful scans; report blocked
or failed pages. Exporting must not implicitly discard the source.

Automatic capture is **not implemented**. A future design must combine valid
fresh boundary, focus/exposure/motion evidence, dwell and hysteresis, duplicate
suppression, manual override and a visible pending trigger. Preview steadiness
alone is insufficient. Book, card and other specialized workflows are research
candidates until separately specified and verified.

## 5. Acceptance

Apply UI-01 through UI-03 and LIFE-01 through LIFE-03 in
[SPEC_06](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md).
Test screen navigation during processing, late completion after deletion,
rapid filter switching, source comparison, export failure and current-language
feedback. Actual filter labels and achieved capture outcomes must agree.
