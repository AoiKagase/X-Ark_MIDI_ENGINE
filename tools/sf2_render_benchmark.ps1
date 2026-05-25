param(
    [Parameter(Mandatory = $true)]
    [string]$MidiPath,

    [Parameter(Mandatory = $true)]
    [string]$SoundFontPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [ValidateRange(1, 16)]
    [int]$SoloChannel = 2,

    [ValidateRange(1, 1000000)]
    [int]$ChunkFrames = 4096,

    [ValidateRange(1, 384000)]
    [int]$SampleRate = 44100,

    [ValidateSet(1, 2)]
    [int]$Channels = 2,

    [ValidateRange(0.0, 7200.0)]
    [double]$MaxSeconds = 0.0,

    [ValidateRange(0.1, 3600.0)]
    [double]$ProgressSeconds = 10.0,

    [ValidateRange(1, 32768)]
    [int]$DiffThreshold = 2000,

    [switch]$DryOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-File {
    param([string]$Path, [string]$Hint)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing file: $Path`nHint: $Hint"
    }
}

function Invoke-And-Capture {
    param(
        [string]$Exe,
        [string[]]$ExeArgs,
        [string]$LogPath
    )
    $output = & $Exe @ExeArgs 2>&1
    $output | Out-File -FilePath $LogPath -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $Exe $($ExeArgs -join ' ')"
    }
    return $output
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $repoRoot "build/cmake"
$testExe = Join-Path $buildDir "XArkMidiTest.exe"
$diffExe = Join-Path $buildDir "compare_wav_diff.exe"
$clipExe = Join-Path $buildDir "analyze_wav_clipping.exe"

Require-File -Path $MidiPath -Hint "Provide an existing MIDI path."
Require-File -Path $SoundFontPath -Hint "Provide an existing SF2 path."
Require-File -Path $testExe -Hint "Build tests first: cmake --build build/cmake --target XArkMidiTest compare_wav_diff analyze_wav_clipping"
Require-File -Path $diffExe -Hint "Build tests first: cmake --build build/cmake --target compare_wav_diff"
Require-File -Path $clipExe -Hint "Build tests first: cmake --build build/cmake --target analyze_wav_clipping"

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}
$outDirFull = (Resolve-Path -LiteralPath $OutputDir).Path

function Render-Case {
    param(
        [string]$CaseName,
        [switch]$DisableInternalEffects
    )

    $caseDir = Join-Path $outDirFull $CaseName
    if (-not (Test-Path -LiteralPath $caseDir)) {
        New-Item -ItemType Directory -Path $caseDir | Out-Null
    }

    $specWav = Join-Path $caseDir "spec_204.wav"
    $tunedWav = Join-Path $caseDir "render_tuned.wav"

    $commonArgs = @(
        $MidiPath,
        $SoundFontPath,
        "",
        "--solo", "$SoloChannel",
        "--chunk", "$ChunkFrames",
        "--sample-rate", "$SampleRate",
        "--channels", "$Channels",
        "--progress-seconds", "$ProgressSeconds"
    )
    if ($MaxSeconds -gt 0.0) {
        $commonArgs += @("--max-seconds", "$MaxSeconds")
    }
    if ($DisableInternalEffects) {
        $commonArgs += @("--disable-internal-effects")
    }

    $specArgs = @($commonArgs)
    $specArgs[2] = $specWav
    $specArgs += @("--compat-mode", "sf2-spec-204")

    $tunedArgs = @($commonArgs)
    $tunedArgs[2] = $tunedWav
    $tunedArgs += @("--compat-mode", "sf2-render-tuned")

    Write-Host "Rendering case: $CaseName"
    Invoke-And-Capture -Exe $testExe -ExeArgs $specArgs -LogPath (Join-Path $caseDir "render_spec.log") | Out-Null
    Invoke-And-Capture -Exe $testExe -ExeArgs $tunedArgs -LogPath (Join-Path $caseDir "render_tuned.log") | Out-Null

    $diffOut = Invoke-And-Capture -Exe $diffExe -ExeArgs @($specWav, $tunedWav, "$DiffThreshold") -LogPath (Join-Path $caseDir "compare_spec_vs_tuned.log")
    $clipSpecOut = Invoke-And-Capture -Exe $clipExe -ExeArgs @($specWav, "250", "20") -LogPath (Join-Path $caseDir "clip_spec.log")
    $clipTunedOut = Invoke-And-Capture -Exe $clipExe -ExeArgs @($tunedWav, "250", "20") -LogPath (Join-Path $caseDir "clip_tuned.log")

    $summaryLines = @(
        "Case: $CaseName",
        "Midi: $MidiPath",
        "SoundFont: $SoundFontPath",
        "SoloChannel: $SoloChannel",
        "SampleRate: $SampleRate",
        "Channels: $Channels",
        "ChunkFrames: $ChunkFrames",
        "MaxSeconds: $MaxSeconds",
        "DisableInternalEffects: $DisableInternalEffects",
        "",
        "==== compare_wav_diff (spec vs tuned) ====",
        $diffOut,
        "",
        "==== analyze_wav_clipping (spec) ====",
        $clipSpecOut,
        "",
        "==== analyze_wav_clipping (tuned) ====",
        $clipTunedOut
    )
    $summaryLines | Out-File -FilePath (Join-Path $caseDir "summary.txt") -Encoding utf8
}

Render-Case -CaseName "dry" -DisableInternalEffects
if (-not $DryOnly) {
    Render-Case -CaseName "wet"
}

Write-Host "Benchmark complete: $outDirFull"
