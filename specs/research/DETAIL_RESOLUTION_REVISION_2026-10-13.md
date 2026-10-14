# rc8 detail, achieved resolution and capture evidence

## Observations versus hypotheses

The readable 20:13 rc7 log and JPEG show 4096x3072 source, 42-pixel crop per edge,
4012x2988 output, scale=1, four base contributors, 17.7318% multi-frame coverage,
1.2894 average contributors and 0.1991% recovered highlights. Short frames really
contributed, but neither native high resolution nor 2x reconstruction occurred.
The user describes untapped capture as unclear and tapped capture as readable but
slightly soft. The later 20:27 files were not yet accessible at their supplied
host paths during initial analysis; no measurements from them are claimed.

Four endpoints form two views: 0/2/4 share main view and 3 is about 0.679x.
Grouping views must not erase differing capture-size capabilities among aliases.
The host has no authorized connected ADB device. Phone modes require test logs.

Current registration short-circuits at the first accepted global transform. The
20:13 Euclidean candidates had high-pass correlations .507/.686/.747; a valid
candidate is not necessarily the best geometric model. Local detail fractions
.005/.131/.136 failed the .15 SR eligibility gate. Do not simply lower the gate.

Short JPEG effective response gains about 30 differ strongly from metadata gain
5.65. ISP tone mapping, post-RAW sensitivity boost and AWB changes are hypotheses.
Manual requests currently omit observed post-RAW boost and replace AWB-lock
options. This needs actual timestamp-matched capture-result evidence, not an
inferred boost factor from image brightness. Focus success is presently unknown.

## Revised contract before implementation

- Keep same-view endpoint variants internally. In experimental native-high mode,
  prefer a compatible variant exposing a larger ordinary/high-resolution JPEG
  within 16..56 MP. Keep one UI view label and actual endpoint diagnostics.
  Maximum-resolution-map-only sizes require a separate pixel-mode/session path;
  never feed them into ordinary CameraX selection or pretend that path exists.
- Record ordinary, high and maximum-resolution JPEG maps, sensor modes and actual
  negotiated dimensions in test diagnostics. Show achieved capture/output MP.
  Unsupported native mode remains visible; output enlargement is not detail proof.
- Timestamp-match bounded capture-result metadata to each JPEG. Record sensor
  exposure/ISO/post-RAW boost and AF/lens state. Prefer sensor evidence only when
  consistently available for the compared burst; otherwise use EXIF consistently.
  Plan manual brackets from actual sensor ISO/time when known, preserve observed
  supported boost and AWB lock, and retain unknown values as unknown.
- Compare validated global registration candidates by measured structural
  agreement, with deterministic ties and unchanged native local/crop/SR gates.
  Check a projective text fixture, periodic text, parallax, noise and occlusion.
  The initial projective fixture exposed an additional gap: ORB cannot always
  supply a usable homography on repeated thin text. Add bounded projective ECC
  from a valid coarse transform; do not lower the local/SR validation thresholds.
- Keep independent-noise averaging and cross-exposure recovery separate. Do not
  declare soft text fixed without a same-burst reference comparison. RAW capture,
  dense motion correction, focus certification and maximum-pixel-mode capture
  remain unimplemented. Add explicit diagnostics for denied SR eligibility.

## Banner request

The user selected the native 4x result from the Banner chat, 6688x3764 (not its
3840x2160 derivative). Export a lossless WebP at those dimensions, verify decoded
RGB equality and package it at the settings bottom. Decode asynchronously at a
bounded display size; never decode its full ~96 MiB bitmap just for a small card.
Retain the previous masters and provenance. Standard edition remains empty.

## Primary references

- [Google HDR+ burst paper](https://research.google/pubs/burst-photography-for-high-dynamic-range-and-low-light-imaging-on-mobile-cameras/): constant-exposure RAW burst; not evidence that this JPEG pipeline already matches it.
- [Sensor sensitivity and post-RAW boost](https://developer.android.com/reference/android/hardware/camera2/CaptureRequest#SENSOR_SENSITIVITY)
- [Maximum-resolution stream map](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#SCALER_STREAM_CONFIGURATION_MAP_MAXIMUM_RESOLUTION)
- [Sensor pixel mode and exclusive output combinations](https://developer.android.com/reference/android/hardware/camera2/CaptureRequest#SENSOR_PIXEL_MODE)
