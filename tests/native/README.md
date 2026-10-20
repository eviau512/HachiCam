# Fusion math regression

This test includes the production `fusion_math.h`. It verifies highlight response,
color recovery, clipping guards, pixel centers and a synthetic multi-phase sampling
case. It does not exercise CameraX, registration or the complete JNI pipeline.

Run from the repository root with a C++17 compiler:

```powershell
g++ -std=c++17 -O2 -I app/src/main/cpp tests/native/fusion_math_test.cpp -o .tools/fusion_math_test.exe
.\.tools\fusion_math_test.exe
```

The current synthetic squared-error ratio is approximately 0.226 relative to
single-frame bilinear interpolation. Camera quality still requires real samples.

## Continuous paper and phase coverage math

Compile `paper_math_test.cpp` with the same include path. This checks the actual
production luminance function for bounded monotonic output, pale mark contrast,
dark/color protection and independent two-dimensional sampling phases.

## Production image pipeline regression

Run `scripts/test-quality-native.ps1` from PowerShell. It compiles the production
registration, fusion, paper and edge-detection C++ sources, substituting only
Android logging with a console stub. It generates owned synthetic text/page
fixtures in memory: two depth motions, occlusion, a blurred first arrival,
independent/identical phases, excessive source border loss, colored/faint ink,
blank paper and an internal table. Outputs are in `.tools/native-quality-tests.log`.
The launcher suppresses Windows modal loader/error dialogs and captures failures.

## Experimental GPU primitives (rc14)

Run `scripts/test-gpu-native.ps1` on a host with a hardware Vulkan compute device.
It builds the actual packed-byte support/ECC shaders against the production
sources, compares gates and known shifts, injects loader/software/pipeline/
allocation/bind/map/submit/completion failure policies, then runs the full image
regression. Missing hardware is an explicit failure of GPU execution, not a
CPU pass reported as GPU. Timeout injection completes real work first and then
simulates the unresolved policy state; it tests bounded quarantine without
deliberately hanging a driver. Real Android driver failure/thermal tests remain pending.

The rc14 ECC solver uses six cached specialized pipelines, resident byte-input
Gaussian/gradient/mask preparation and two tiled reduction dispatches per bounded
iteration. Actual GPU tests compare all five prepared float planes exactly with
CPU, check known shifts and 16 mixed non-square patches with partial chunks, and
reject flat, insufficient-mask and overflowing inputs. Numerical proposal limits
remain 0.05 source pixel and 0.002 correlation. Production workload calibration
can still select CPU; executing the test shader does not prove a phone speedup.

The quality suite also checks native-byte highlight demand coordinates, all
potential conflict/edge nodes and exhaustive admission for small supported target
regions. These checks protect the demand-bounded scheduling optimization without
changing the recovery thresholds or claiming new lamp recovery.

rc15 updates highlight demand to the revised >=247 channel-clipping contract.
The math regression checks adversarial single-channel JPEG response and exact
preservation of unclipped channels; the full pipeline retains clipped colored
text/rings and normal/streaming rejection cases. Cached/uncached node preparation
is compared with identical registration fields/gates. Medium-correlation GPU
scheduling failed frozen conformance and is not activated.

`scripts/build-compute-shaders.ps1` rebuilds the checked-in SPIR-V and C++ headers
using the installed NDK's glslc, targeting Vulkan 1.0. Android debug integration
adds startup and real-workload CPU/timing calibration. Test force/failure hooks
are host-test-only macros and are absent from the shipped APK. Release remains CPU.

The current host dependency is MSYS2 OpenCV 5.0.0-5, isolated under
`.tools/opencv-host/ucrt64`. `opencv_compat.h` includes declarations split into the
OpenCV 5 geometry module. **Android still builds/ships OpenCV 4.10.0**; a host pass
is not an Android runtime, camera, memory or device-quality pass. No host test
runtime is packaged in the APK. Compiler/Python/OpenCV paths are overridable.

The rc6 suite also compiles `stream_fusion.cpp` and `noise_pipeline_test.cpp`.
It compares known clean truth with independent noisy color frames, including
subpixel motion, faint marks, noise-only/identical cases, blurred noisy text and
sparse verified coverage below 20%. A 4912 x 3684 native fixture checks sequential
fusion and analysis-to-native mapping after reference changes. Test loaders
provide owned in-memory frames; Android's file/JPEG loading and camera mode
availability still require device verification.

The rc7 suite also compiles `highlight_pipeline_test.cpp` against both production
normal/streaming pipelines. It checks real pixel recovery and regained text
contrast, single-channel clipping, ordinary midtone preservation, translated
short exposures, sparse support below the former 5% cutoff, and rejection of
ignored exposure, occlusion and all-clipped/flat input. Grayscale match validity
excludes any clipped reference color channel. These known-truth fixtures do not
model every vendor JPEG response, flicker or rolling shutter.

The rc23 suite checks continuous highlight overlap, unchanged displacement
conflict rejection, geometric support-union equivalence, bounded CPU support and
a conservative detail-energy upper bound against independently sampled subpixel
translations. Channel recovery enters continuously at clipping; observed
channels remain protected. Full CPU/GPU image suites retain ring-hole recovery,
white-field/text, blur/occlusion/noise and streaming checks. Frozen full versus
bounded preparation replays remain private local evidence; host timing and
OpenCV 5 parity do not certify Android performance or complete lamp contours.

The rc24 suite runs actual GPU quarter-sample mapping in patch-relative
coordinates, including projective transforms, clipped sources, boundary/gate,
missing-detail and conflict cases. It checks resident CSR solving, correction
bounds and original reconstruction truth. Production calibration injects a
numeric disagreement independently into both new stages and verifies exact CPU
reuse, permanent class rejection and unrelated-stage operation. Mapping-only
isolation and fault controls are host-test-only and must be absent from APKs.

Verified official package checksums used for this workspace:

| Package | SHA256 |
| --- | --- |
| opencv 5.0.0-5 | 62ebea12f642a97d799cecf6103aa8ca2d8418f25972608e6b04ee6b0b1913a7 |
| tbb 2023.1.0-1 | 1dcd85163ef30c1ce313d72a9f8df90188da2a1b74effa5bb291250d3f7b0f67 |
| abseil-cpp 20260526.0-1 | 89803dbb88b7e1a18169fd2ecefa2b85de3d99fcd5d6899b737e4451532b691f |
| protobuf 36.2-1 | 39446eb547ad14187dc6f126ea978ec49c9648db340e14c244da033a030502eb |
| libstdc++ 16.2.0-5 | 9025eb0c238f1c6093bb7561d9be64523159a8cac212b9c221853ac88c0d341a |
| libgcc 16.2.0-5 | 5b7efca9a27ce7504a931d92bac39a8009d8347dade0d905d0588aecdf68cb4b |

Packages were obtained from the [official MSYS2 repository](https://repo.msys2.org/mingw/ucrt64/)
and compared with its package metadata. Existing compiler/threading/zlib runtimes
supply other dependencies; the project does not replace the system installation.
