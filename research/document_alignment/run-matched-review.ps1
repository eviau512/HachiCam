param(
    [string]$InputDirectory='.tools/rc10-1325-evidence/paired-screen',
    [string]$OutputDirectory='.tools/upscan-review/matched',
    [string]$OpenCvRoot='.tools/opencv-host/ucrt64',
    [string]$Compiler='C:\msys64\ucrt64\bin\g++.exe',
    [string]$Python="$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe"
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $root
try {
    $output=[IO.Path]::GetFullPath($OutputDirectory)
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    $include=Join-Path $OpenCvRoot 'include/opencv5'
    $libs=@('-lopencv_features','-lopencv_calib','-lopencv_geometry')
    if (!(Test-Path -LiteralPath $include)) {
        $include=Join-Path $OpenCvRoot 'include/opencv4'
        $libs=@('-lopencv_features2d','-lopencv_calib3d')
    }
    $executable=Join-Path $output 'review.exe'
    $sources=@('research/document_alignment/review_matched.cpp','app/src/main/cpp/local_registration.cpp','app/src/main/cpp/gpu_local_support.cpp',
       'app/src/main/cpp/fusion_pipeline.cpp','app/src/main/cpp/stream_fusion.cpp',
       'app/src/main/cpp/regional_reconstruction.cpp','app/src/main/cpp/regional_refinement.cpp','app/src/main/cpp/burst_fusion.cpp')
    $compile=@('-std=c++17','-O2','-fopenmp','-Wno-deprecated-declarations','-include','tests/native/host/opencv_compat.h',
       '-I',$include,'-I','app/src/main/cpp','-I','tests/native/host')+$sources+
       @('-L',(Join-Path $OpenCvRoot 'lib'),'-lopencv_video','-lopencv_photo')+$libs+@('-lopencv_imgproc','-lopencv_core','-o',$executable)
    & $Compiler @compile
    if($LASTEXITCODE -ne 0){throw 'Review compilation failed.'}
    $env:PATH=[IO.Path]::GetFullPath((Join-Path $OpenCvRoot 'bin'))+';'+(Split-Path $Compiler)+';'+$env:PATH
    $env:OMP_NUM_THREADS='4'
    # Helper suppresses native DLL-error modal dialogs; reports are local/ignored.
    & $Python research/document_alignment/run_review_native.py $executable (Join-Path $output 'export.log') $InputDirectory $output
    if($LASTEXITCODE -ne 0){throw 'Review export failed.'}
    & $Python research/document_alignment/review_matched.py $output --opencv-path .tools/vision
    if($LASTEXITCODE -ne 0){throw 'Proposal fitting failed.'}
    & $Python research/document_alignment/run_review_native.py $executable (Join-Path $output 'assessment.log') assess $output
    if($LASTEXITCODE -ne 0){throw 'Native post-gate assessment failed.'}
    & $Python research/document_alignment/review_matched.py $output --opencv-path .tools/vision --summarize
    if($LASTEXITCODE -ne 0){throw 'Review summary failed.'}
} finally { Pop-Location }
