param(
    [string]$InputDirectory = '.tools/rc10-1325-evidence/paired-screen',
    [string]$OutputDirectory = '.tools/document-alignment/real-patches',
    [string]$OpenCvRoot = '.tools/opencv-host/ucrt64',
    [string]$Compiler = 'C:/msys64/ucrt64/bin/g++.exe',
    [string]$Python = "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe"
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $root
try {
    $include = Join-Path $OpenCvRoot 'include/opencv5'
    $libs = @('-lopencv_features','-lopencv_calib','-lopencv_geometry')
    if (!(Test-Path -LiteralPath $include)) {
        $include = Join-Path $OpenCvRoot 'include/opencv4'
        $libs = @('-lopencv_features2d','-lopencv_calib3d')
    }
    if (!(Test-Path -LiteralPath $include)) { throw 'Supply installed host OpenCV using -OpenCvRoot.' }
    $outputPath = [IO.Path]::GetFullPath($OutputDirectory)
    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
    $executable = Join-Path $outputPath 'production_probe.exe'
    $sources = @('research/document_alignment/export_production_patches.cpp',
        'app/src/main/cpp/local_registration.cpp','app/src/main/cpp/fusion_pipeline.cpp',
        'app/src/main/cpp/stream_fusion.cpp','app/src/main/cpp/regional_reconstruction.cpp',
        'app/src/main/cpp/regional_refinement.cpp','app/src/main/cpp/burst_fusion.cpp',
        'app/src/main/cpp/perspective_corrector.cpp','app/src/main/cpp/edge_detector.cpp',
        'app/src/main/cpp/document_boundary.cpp')
    $compileArgs = @('-std=c++17','-O2','-fopenmp','-Wno-deprecated-declarations',
        '-include','tests/native/host/opencv_compat.h','-I',$include,'-I','app/src/main/cpp',
        '-I','tests/native/host') + $sources + @('-L',(Join-Path $OpenCvRoot 'lib'),
        '-lopencv_video','-lopencv_photo') + $libs + @('-lopencv_imgproc','-lopencv_core','-o',$executable)
    & $Compiler @compileArgs
    if ($LASTEXITCODE -ne 0) { throw 'Probe compilation failed.' }
    $env:PATH = [IO.Path]::GetFullPath((Join-Path $OpenCvRoot 'bin')) + ';' + (Split-Path $Compiler) + ';' + $env:PATH
    $env:OMP_NUM_THREADS = '4'
    # Suppress Windows DLL-error modal dialogs; retain bounded diagnostics on disk.
    & $Python -c 'import ctypes,subprocess,sys; ctypes.windll.kernel32.SetErrorMode(0x8003); r=subprocess.run(sys.argv[1:],stdout=subprocess.PIPE,stderr=subprocess.STDOUT); print(r.stdout.decode("utf-8",errors="replace")); raise SystemExit(r.returncode)' $executable $InputDirectory $outputPath |
        Set-Content -LiteralPath (Join-Path $outputPath 'production.log') -Encoding UTF8
    if ($LASTEXITCODE -ne 0) { throw 'Production probe failed; inspect production.log.' }
    & $Python research/document_alignment/evaluate_patches.py $outputPath --opencv-path .tools/vision
    if ($LASTEXITCODE -ne 0) { throw 'Patch evaluation failed.' }
} finally { Pop-Location }
