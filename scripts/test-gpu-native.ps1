param([string]$OpenCvRoot='.tools/opencv-host/ucrt64',
    [string]$Compiler='C:\msys64\ucrt64\bin\g++.exe',
    [string]$Python="$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
    [string]$Sdk="$env:LOCALAPPDATA\Android\Sdk")
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $taskRoot
try {
    $taskHeaders=Join-Path $taskRoot '.tools/vulkan-headers'
    New-Item -ItemType Directory -Path $taskHeaders -Force | Out-Null
    $taskSdkInclude=Join-Path $Sdk 'ndk/30.0.16248370/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
    foreach($taskHeaderGroup in @('vulkan','vk_video')) {
        Copy-Item -LiteralPath (Join-Path $taskSdkInclude $taskHeaderGroup) -Destination $taskHeaders -Recurse -Force
    }
    $taskInclude=Join-Path $OpenCvRoot 'include/opencv5'
    $taskFlags=@('-std=c++17','-O2','-fopenmp','-DHACHICAM_VULKAN_SUPPORT=1','-DHACHICAM_GPU_TEST_CONTROL=1',
        '-Wno-deprecated-declarations','-include','tests/native/host/opencv_compat.h',
        '-I',$taskInclude,'-I','app/src/main/cpp','-I','tests/native/host','-I',$taskHeaders)
    $taskBasic=@('app/src/main/cpp/local_registration.cpp','app/src/main/cpp/gpu_local_support.cpp')
    $taskLibraries=@('-L',(Join-Path $OpenCvRoot 'lib'),'-lopencv_video','-lopencv_imgproc','-lopencv_core')
    & $Compiler @taskFlags 'tests/native/gpu_gate_test.cpp' 'tests/native/gpu_fitting_test.cpp' 'tests/native/gpu_compute_test.cpp' 'tests/native/gpu_selection_test.cpp' 'tests/native/gpu_policy_test.cpp' @taskBasic @taskLibraries '-o' '.tools/gpu_gate_test.exe'
    if($LASTEXITCODE -ne 0){throw 'GPU primitive compilation failed'}
    $env:PATH=[IO.Path]::GetFullPath((Join-Path $OpenCvRoot 'bin'))+';'+(Split-Path $Compiler)+';'+$env:PATH
    $env:OMP_NUM_THREADS='4'
    & $Python 'tests/native/host/run.py' '.tools/gpu_gate_test.exe' '.tools/gpu-gate-tests.log'
    if($LASTEXITCODE -ne 0){throw 'Actual GPU primitive validation failed; CPU fallback does not count as GPU execution'}
    & $Compiler @taskFlags 'tests/native/gpu_failure_test.cpp' @taskBasic @taskLibraries '-o' '.tools/gpu_failure_test.exe'
    if($LASTEXITCODE -ne 0){throw 'GPU failure test compilation failed'}
    foreach($taskFault in @('loader','software','pipeline','allocation','bind','map','submit','timeout')) {
        & $Python 'research/document_alignment/run_review_native.py' '.tools/gpu_failure_test.exe' ".tools/gpu-failure-$taskFault.log" $taskFault
        if($LASTEXITCODE -ne 0){throw "GPU failure policy failed: $taskFault"}
    }
    $taskTests=@('tests/native/quality_pipeline_test.cpp','tests/native/regional_reconstruction_test.cpp',
        'tests/native/document_boundary_test.cpp','tests/native/noise_pipeline_test.cpp','tests/native/highlight_pipeline_test.cpp')
    $taskNative=@('app/src/main/cpp/fusion_pipeline.cpp','app/src/main/cpp/stream_fusion.cpp',
        'app/src/main/cpp/regional_reconstruction.cpp','app/src/main/cpp/regional_refinement.cpp','app/src/main/cpp/burst_fusion.cpp',
        'app/src/main/cpp/perspective_corrector.cpp','app/src/main/cpp/edge_detector.cpp','app/src/main/cpp/document_boundary.cpp')
    & $Compiler @taskFlags @taskTests @taskBasic @taskNative @taskLibraries '-lopencv_photo' '-lopencv_features' '-lopencv_calib' '-lopencv_geometry' '-o' '.tools/gpu_quality_test.exe'
    if($LASTEXITCODE -ne 0){throw 'GPU pipeline compilation failed'}
    & $Python 'tests/native/host/run.py' '.tools/gpu_quality_test.exe' '.tools/gpu-quality-tests.log'
    if($LASTEXITCODE -ne 0){throw 'GPU end-to-end quality validation failed'}
} finally { Pop-Location }
