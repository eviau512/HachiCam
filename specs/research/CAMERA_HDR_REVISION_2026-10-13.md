# rc7 camera endpoint and HDR correction

## Evidence and limits

User screenshots of rc6 show Main/Rear 2/Rear 3/Rear 4 plus zoom presets in one
row. System endpoints were incorrectly presented as individual cameras. The
user identifies the device as Ace 3V with two rear lenses; no model-specific
allowlist or exact hardware count is inferred by the implementation.

The 18:30:47 test log records four base exposures at 16.65 ms, two short frames
at 2.94 ms and one at 0.239 ms. Four base frames were admitted with 59.12%
multi-frame coverage and 2.277 average contributors. Short local support was
1.1%, 0%, 0%; the 5% global admission rule discarded every short. Result:
anchors=4, highlights=0. Capture acquisition worked; highlight recovery did not.
The repeated 17:01 JPEG is an earlier capture, not proof of this burst's output.

## Contract before implementation

Correct authoritative SPEC_01/02/05/06 first. Implement metadata-derived view
grouping, separate zoom controls, supported manual EV, window-oriented text,
short-exposure comparison normalization and sparse highlight admission. Keep
multi-frame noise reduction and lens switching. Record actual recovered/clipped
pixel fractions and a visible no-recovery outcome. Native high resolution stays
experimental. No live HDR preview, dense motion correction or universal camera
hardware discovery is claimed.

The 12% view grouping tolerance, 1024-pixel highlight admission, local comparison
thresholds, 140-pixel validated patch interior and 0.5% clipping warning threshold
are experimental parameters. Shifted/sparse fixtures additionally exposed the
four-neighbor interpolation restriction on short exposures; bounded nearest
validated patch sampling is required for highlights only (SPEC_02).
Synthetic results and real-device limitations must be recorded after checks.

## Primary references

- [CameraX intrinsic view ratio, available since 1.3.0](https://developer.android.com/reference/androidx/camera/core/CameraInfo#getIntrinsicZoomRatio())
- [Android logical versus physical cameras](https://developer.android.com/media/camera/camera2/multi-camera)
- [Camera characteristics](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics)
- [CameraX exposure compensation](https://developer.android.com/reference/androidx/camera/core/CameraControl#setExposureCompensationIndex(int))
