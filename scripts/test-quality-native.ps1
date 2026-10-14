param(
    [string]$OpenCvRoot = "$PSScriptRoot\..\.tools\opencv-host\ucrt64",
    [string]$Compiler = 'C:\msys64\ucrt64\bin\g++.exe',
    [string]$Python = "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $root
try {
    if (!(Test-Path -LiteralPath $Compiler) -or !(Test-Path -LiteralPath $Python)) {
        throw 'Provide the C++ compiler and Python paths with -Compiler / -Python.'
    }
    $include = Join-Path $OpenCvRoot 'include\opencv5'
    $libs = @('-lopencv_features', '-lopencv_calib', '-lopencv_geometry')
    if (!(Test-Path -LiteralPath $include)) {
        $include = Join-Path $OpenCvRoot 'include\opencv4'
        $libs = @('-lopencv_features2d', '-lopencv_calib3d')
    }
    if (!(Test-Path -LiteralPath $include)) { throw 'Install host OpenCV inside .tools or pass -OpenCvRoot.' }
    $args = @('-std=c++17', '-O2', '-fopenmp', '-Wno-deprecated-declarations',
        '-include', 'tests/native/host/opencv_compat.h', '-I', $include,
        '-I', 'app/src/main/cpp', '-I', 'tests/native/host',
        'tests/native/quality_pipeline_test.cpp', 'tests/native/regional_reconstruction_test.cpp', 'tests/native/document_boundary_test.cpp', 'tests/native/noise_pipeline_test.cpp', 'tests/native/highlight_pipeline_test.cpp', 'app/src/main/cpp/local_registration.cpp', 'app/src/main/cpp/gpu_local_support.cpp',
        'app/src/main/cpp/fusion_pipeline.cpp', 'app/src/main/cpp/stream_fusion.cpp', 'app/src/main/cpp/regional_reconstruction.cpp', 'app/src/main/cpp/regional_refinement.cpp', 'app/src/main/cpp/burst_fusion.cpp',
        'app/src/main/cpp/perspective_corrector.cpp', 'app/src/main/cpp/edge_detector.cpp',
        'app/src/main/cpp/document_boundary.cpp',
        '-L', (Join-Path $OpenCvRoot 'lib'), '-lopencv_video', '-lopencv_photo') + $libs +
        @('-lopencv_imgproc', '-lopencv_core', '-o', '.tools/quality_pipeline_test.exe')
    & $Compiler @args
    if ($LASTEXITCODE -ne 0) { throw 'Native quality test compilation failed.' }
    $env:PATH = (Join-Path $OpenCvRoot 'bin') + ';' + (Split-Path $Compiler) + ';' + $env:PATH
    $env:OMP_NUM_THREADS = '4'
    & $Python tests/native/host/run.py .tools/quality_pipeline_test.exe .tools/native-quality-tests.log
    if ($LASTEXITCODE -ne 0) { throw 'Native quality regression failed; see .tools/native-quality-tests.log.' }
} finally { Pop-Location }
