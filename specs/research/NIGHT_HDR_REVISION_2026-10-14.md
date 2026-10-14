# Night/screen revision, 2026-10-14

## Evidence and limits

User-owned `20261013-2342.zip`: P001/P003/P004/P005 are approximately 12 MP;
P002 (Excel) is 8136x6088, 49,531,968 pixels, scale=2. This proves reconstruction
was invoked, not native 50 MP readout or resolved-detail gain. Exposed main-view
ordinary JPEG maps top out at 4096x3072; high and maximum-mode maps are empty.
P005: no recovered highlights, 0.8451% clipping, 28.07% multi-frame coverage;
base ISO 6400, 58.325 ms, post-RAW boost 228%. The last base AF state is scanning
and short-tier focus distance differs. P003 recovers nothing; P004 recovers
0.2598% of pixels. The MotoHDR examples show useful lamp/icon/text outcomes but
different aperture, framing, ISP, dates and absent raw bursts prevent calibrated
algorithm/sensor comparisons. Two windows on one screen are not independent
focus planes; font size, clipping, ISP and fusion must also be examined.

## Revised contracts (experimental until device verification)

1. At shutter, attempt AF-only at a recent valid tap or viewfinder center, bounded
   to 1500 ms. Disable auto-cancel through capture. Fresh stationary focused lens
   metadata and AF-off support permit preserving observed focus distance in every
   tier. Unknown/failed focus stays unknown and does not block manual capture.
   Take the held distance from the first captured JPEG's exact matched result,
   not potentially stale preview metadata around the autofocus acknowledgment.
   Clear overrides and cancel metering in cancellation-safe cleanup. Fixed-focus
   cameras must not acquire fabricated focus success.
2. Deepest brackets prefer lower sensor ISO and supported 100% post-RAW boost,
   with a 16.667 ms shutter cap. Compute from the full observed exposure product,
   clamp driver limits and validate actual results. Ordinary brackets retain
   boost-preserving planning. This is not OEM RAW fusion or a calibrated ISP model.
3. Verify geometry on unclipped neighboring structure, including observed outer
   silhouettes. Do not forcibly erode all near-clipping samples: the five-pixel
   erosion proposal rejected the existing supported clipped-screen fixture by
   deleting its only visible boundary. Clipped interiors remain excluded from
   correlation. A clipped target cannot
   be rejected merely for brightness disagreement with a clipped reference.
   Unclipped targets retain residual checks and all targets need local support.
4. Observed dark shape inside clipping may recover only within 12 source pixels
   of valid nonclipped bright short-exposure signal and with verified geometry.
   Use morphological closing of that signal mask, so an enclosed small hole is
   eligible but a one-sided dark region outside the light is not simply dilated in.
   Keep the monotonic shoulder, crop, blur/occlusion protection and source fallback.
   This neighborhood cannot grant SR credit or permit unbounded dark replacement.
   Normal/streaming paths share the rule; high-resolution memory admission accounts
   for the extra byte mask using a conservative 20 bytes/pixel estimate.
5. Remove separate digital zoom presets, capture/output MP captions, inactive
   native-high caption and SDR/HDR explanation from the viewfinder. Use circular
   real-view selectors labeled 1x/0.7x/etc. Retain supported EV, capture locks,
   logged actual dimensions and experimental settings limits. Do not change SR
   eligibility or fabricate a high-resolution camera route.
6. Test builds retain at most the latest completed burst's compressed inputs,
   fused JPEG and a manifest with its own frame/exposure/focus/build identity in
   private cache. Limit a snapshot to 192 MiB, check free disk space and publish
   atomically; archive failure cannot fail the photo. Export explicitly labels
   that original photos are included and packages that snapshot plus the log in
   one ZIP. Serialize snapshot replacement/export, keep at most two exported
   bundles and no partial successful bundle. Release retains/exports no input
   photos. No automatic network transfer is introduced.

## Acceptance

CAP-08: bounded AF/cleanup, fresh stationary focused metadata only; invalid lens
values/stale sessions rejected; gain-first deepest exposure remains within limits.
PHOTO-11: independently supported clipped lamp ring retains its observed dark
hole; ordinary midtones, negative/all-clipped/occluded cases, shifted highlights,
existing SR/noise gates pass in normal and streaming production paths. UI-08:
removed captions/presets stay absent, circular view selectors and EV work, both
flavors compile. Real rc9 scenes and paired raw input comparison remain pending.
DIAG-01: previous snapshot survives failed replacement, ZIP entries/manifest match
one completed burst, bounds and release gating hold; sharing remains user initiated.

Primary references: [CameraX metering](https://developer.android.com/reference/androidx/camera/core/FocusMeteringAction.Builder),
[Camera2 focus](https://developer.android.com/reference/android/hardware/camera2/CaptureRequest#LENS_FOCUS_DISTANCE),
[Interop overrides](https://developer.android.com/reference/androidx/camera/camera2/interop/Camera2CameraControl).
