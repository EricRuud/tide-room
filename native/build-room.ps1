# Windows x64 preview. Run from PowerShell with Visual Studio 2022 C++ tools installed.
[CmdletBinding()]
param(
    [string]$BuildDirectory = "build-windows",
    [switch]$RunChecks,
    [int]$Jobs = 2
)
$ErrorActionPreference = "Stop"
if ($env:OS -ne "Windows_NT") { throw "This script requires Windows." }
$repo = Split-Path $PSScriptRoot -Parent
Set-Location $repo
$build = [IO.Path]::GetFullPath($BuildDirectory)
$app = Join-Path $build "app"
$checks = Join-Path $build ("checks/" + (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Force $build, $checks | Out-Null
Start-Transcript -Path (Join-Path $checks "build.log") | Out-Null
function Invoke-Native([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Invoke-Check([string]$Name, [string[]]$Arguments) {
    $source = Join-Path $build ($Name + "_artefacts/Release/" + $Name + ".exe")
    $destination = Join-Path $app ($Name + ".exe")
    Copy-Item $source $destination
    try { Invoke-Native $destination $Arguments }
    finally { Remove-Item $destination }
}
try {
    $juce = if ($env:JUCE_PATH) { $env:JUCE_PATH } else { Join-Path $repo ".cache/JUCE" }
    if (!(Test-Path (Join-Path $juce "CMakeLists.txt"))) {
        New-Item -ItemType Directory -Force (Split-Path $juce -Parent) | Out-Null
        Invoke-Native "git" @("clone", "--depth", "1", "--branch", "8.0.9", "https://github.com/juce-framework/JUCE.git", $juce)
    }
    $revision = & git -C $juce rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $revision -ne "f72bad64d29715216226685810c5196bd0d79d77") {
        throw "Expected the pinned JUCE 8.0.9 checkout in $juce"
    }
    Invoke-Native "cmake" @("-S", $repo, "-B", $build, "-G", "Visual Studio 17 2022", "-A", "x64",
        "-DJUCE_PATH=$juce", "-DTIDE_NATIVE_WOOD=ON", "-DTIDE_CLEAR_ROOM=ON", "-DTIDE_METAL=OFF",
        "-DTIDE_CPU_PREVIEW=ON", "-DTIDE_ROOM_OUTPUT_DIRECTORY=$app")
    $targets = @("TideRoom_Standalone")
    if ($RunChecks) { $targets += @("TideFourierCheck", "Tide821Check", "TideWornCheck", "TidePresetCheck", "TideRecordingCheck") }
    Invoke-Native "cmake" (@("--build", $build, "--config", "Release", "--parallel", "$Jobs", "--target") + $targets)

    $manifest = Get-Content (Join-Path $repo "native/Assets/821/manifest.json") -Raw | ConvertFrom-Json
    foreach ($entry in $manifest.sha256.PSObject.Properties) {
        $file = Join-Path $app ("Resources/821/" + $entry.Name)
        if ((Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) {
            throw "Bundled model checksum mismatch: $($entry.Name)"
        }
    }
    Write-Host "PASS all 23 bundled 821 model checksums"
    if ($RunChecks) {
        Invoke-Native (Join-Path $build "Release/TideFourierCheck.exe") @((Join-Path $checks "fourier-samples.txt"))
        Invoke-Check "Tide821Check" @((Join-Path $app "Resources/821"), (Join-Path $checks "821"), "--ui")
        Invoke-Check "TideWornCheck" @("--unit", (Join-Path $checks "worn"))
        Invoke-Check "TideWornCheck" @("--dips", (Join-Path $checks "dips"))
        Invoke-Check "TidePresetCheck" @((Join-Path $checks "presets"))
        Invoke-Check "TideRecordingCheck" @((Join-Path $checks "recording"))

        $process = Start-Process -FilePath (Join-Path $app "Tide Room.exe") -PassThru
        try {
            Start-Sleep -Seconds 8
            $process.Refresh()
            if ($process.HasExited -or $process.MainWindowHandle -eq 0) { throw "Standalone did not open a window" }
            Write-Host "PASS standalone window opened (audio hardware not tested)"
            if (!$process.CloseMainWindow() -or !$process.WaitForExit(15000)) { throw "Standalone did not close normally" }
            if ($process.ExitCode -ne 0) { throw "Standalone exited with code $($process.ExitCode)" }
        } finally { if (!$process.HasExited) { Stop-Process -Id $process.Id -Force } }
    }
    Copy-Item (Join-Path $repo "native/WINDOWS.md") (Join-Path $app "README.txt")
    $zip = Join-Path $build "Tide-Room-Windows-x64-preview.zip"
    Compress-Archive -Path (Join-Path $app "Tide Room.exe"), (Join-Path $app "Resources"), (Join-Path $app "README.txt") -DestinationPath $zip -Force
    Get-FileHash $zip -Algorithm SHA256 | Format-List
    Write-Host "Built: $zip"
} finally { Stop-Transcript | Out-Null }
