# Specification 05: Hachimi Design System and User Experience

> Status: Authoritative. Revised 2026-10-14.
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

The [rc9 revision](research/NIGHT_HDR_REVISION_2026-10-14.md) supersedes rc8
viewfinder captions: remove capture/output MP, native-high inactive text and the
SDR/HDR explanation from the viewfinder. Keep actual outcomes in diagnostics and
experimental native-high limits in settings; Full HDR alone promises no 50 MP.

rc7 groups metadata-derived views rather than numbering every system endpoint.
Use circular real-view selectors containing 1x/0.7x/etc from intrinsic ratios;
do not assert a hardware count or show an additional digital zoom preset row.
The preview remains SDR without a permanent explanatory caption. The user's rc12
request supersedes the rc7 manual exposure aid: remove the EV button and menu,
including advice to adjust EV. Retain only the circular real-view choices in that
row. Supported compensation resets to zero on bind; target AF/AE and bounded
exposure settling follow SPEC_01. This does not remove HDR brackets or add
technical metering captions to the viewfinder.
Text capsules, menus and progress panels follow the window orientation; rotating
long pills with a graphics transform must not make them overlap controls. Only
bounded square icons may rotate to follow physical device orientation.

The rc6 noise revision adds available back-camera choice and supported zoom
ratios. Do not claim an optical lens change for a logical/digital zoom request.
Switching is disabled during capture, errors restore usable preview with visible
feedback, and a single-base fusion outcome has explicit fallback guidance.
Single-base guidance describes lack of base-frame temporal denoising; it must
not imply that an independently supported short-exposure recovery also failed.
Full HDR is the default general capture setting; native high resolution is the
experimental opt-in. Explain supported native output, longer time and resource
limits without promising exact megapixels or restored text. Show achieved high
resolution only after successful negotiation; unsupported/resource-limited
requests have visible fallback and keep the normal camera usable.

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
**Implemented in rc10:** immutable source edits, stale-render rejection and a
last-valid-image failure state. Full appearance-preview/thumbnail parity remains
required / incomplete; the original master and committed export are distinct images.
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

### 4.1 Diagnostic export and page recovery feedback (rc13)

Test diagnostic export waits off the UI thread for admitted capture/processing
and completed final-image snapshots, with a bounded/cancellable wait. Settings
shows separate waiting-for-photos and preparing-archive states; no partial
current result is presented as ready. Export at most four completed matching
groups under the SPEC_00 combined byte bound, keeping the legacy latest group's
`capture/` layout. Earlier groups have separate identities/manifests/contexts and
final images. A failed newest attempt produces an explicit missing-current warning.
Each export has a unique identity in its log header and filenames, including
recreated/concurrent settings callers; a second export may not overwrite the
first export's context merely because both start in the same wall-clock second.
Release remains metadata-only and never retains these raw diagnostic photo groups.

Page-checkpoint persistence/recovery failures give a concise visible warning
while retaining the previous valid state; do not promise the latest unsaved edit
will survive restart. Completed-page recovery does not resume an interrupted burst.

### 4.2 Settings character banner

**Implemented; artwork inspected and both debug flavors compiled/resources
checked. Physical-device layout acceptance pending:** the Hachimi edition settings list includes
one decorative, locally bundled landscape banner at the bottom of the settings
list, after the entire About card (including log export). The user's latest
placement instruction supersedes the earlier before-language placement.
The standard edition has no character banner or reserved empty space. Existing
settings, navigation and the About identity remain usable and unchanged.

The artwork uses the community LLM-girl identities explicitly selected by the
user: DeepSeek is a whale girl, Gemini / Hachimi is a cat girl, and GPT is a
white dragon girl. The banner contains only Gemini and GPT. Gemini stands in
front toward the left, visibly surprised with a warm red flush at the center
of her forehead / between the brows. GPT stands behind toward the right,
blushing shyly and gently embracing Gemini around the waist. Follow the user's
2026-10-13 pose sketch. Preserve the cat ears, blue-violet hair and star ornament
for Gemini, and the white hair, dragon horns and pale dragon details for GPT.
The mood is affectionate, playful moe anime; there is no embedded text.

Use a landscape master targeting 16:9 (allowing integer-pixel rounding), within
the user-permitted 16:9 through 4:3 range. Keep faces,
horns, ears and the embracing hands inside the composition; the UI displays the
entire image at its native aspect ratio, without stretching or face cropping.
Use the settings card's 20 dp rounded corners and existing 16 dp list inset.
The decorative image has no accessibility announcement, click action, overlay
control, network dependency or new permission. Asset decoding must be independent
of Android density scaling (`drawable-nodpi`).

Keep the source research, selected references, generation prompt and validation
record under `artwork/settings-banner/`, with a dated research summary under
`specs/research/`. Third-party reference images are research inputs, not app
resources. Record creators, source URLs and unknown licensing facts separately
from project-produced artwork in `ARTWORK_NOTICE.md`; do not imply official
endorsement or that code/specification licensing grants rights to artwork.
Exact colors, background decorations and illustration rendering are artistic
choices. Physical-device layout acceptance remains pending until observed.

**Resolution follow-up (2026-10-13), implemented; dimensions and visual
comparison checked:** produce a separate enhanced
master targeting 3840 x 2160 (4K), using the selected 1672 x 941 scene as the
edit target. Preserve character identity, composition, expressions, the rear
embrace, hand ownership and complete ears/horns. Preserve the existing master
as a sibling version. Record actual output dimensions and method; do not call
a lower-resolution output 4K or claim that generated detail was present in the
original. That earlier 4K follow-up retained the 1672 x 941 runtime resource;
the latest request below replaces it with native 4x WebP and bounded decoding.
The built-in image edit attempt returned 1672 x 941 again and did not satisfy
the requested output size. The requested super-resolution follow-up therefore
uses the original selected scene with a dedicated local anime SR model (without
generative scene replacement), followed by a documented high-quality reduction
to the target export dimensions. Retain its native 4x result when practical.
The delivered master is `gpt-gemini-banner-v2-4k.png`, 3840 x 2160, produced
with Real-ESRGAN's anime illustration model. The native 6688 x 3764 intermediate
is retained locally. The original master checksum remains unchanged. The mobile
runtime now uses its 6688 x 3764 native derivative as WebP; the previous PNG is
not packaged alongside the replacement.

## 5. Acceptance

**Latest banner runtime request:** supersede the unchanged-runtime allowance above.
Use the selected native 6688 x 3764 4x result as a lossless WebP, with identical
decoded RGB and uncropped aspect ratio. Keep settings-bottom placement. Load on
a worker and decode with power-of-two sampling to the actual display width;
the standard flavor keeps no image or blank item. Original and 4K masters remain
archival siblings. Encoding changes do not claim new source detail.

On 2026-10-14 the user reconfirmed the largest approximately 25.9 MB native 4x
PNG as the selected source. The runtime remains its 6688 x 3764 lossless WebP;
the 3840 x 2160 sibling is archival, not the selected runtime replacement.

Apply UI-01 through UI-06 and LIFE-01 through LIFE-03 in
[SPEC_06](./SPEC_06_IMAGE_QUALITY_AND_ACCEPTANCE.md).
Test screen navigation during processing, late completion after deletion,
rapid filter switching, source comparison, export failure and current-language
feedback. Actual filter labels and achieved capture outcomes must agree.
