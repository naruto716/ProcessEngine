param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [switch]$Run
)

$ErrorActionPreference = "Stop"

function Get-CMakeCommand {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmake) {
        return $cmake.Source
    }

    $fallback = "C:\Program Files\CMake\bin\cmake.exe"
    if (Test-Path $fallback) {
        return $fallback
    }

    throw "cmake was not found. Reopen PowerShell after installing CMake, or verify that CMake is installed."
}

$cmake = Get-CMakeCommand
$configurePreset = if ($Configuration -eq "Debug") { "mingw-debug" } else { "mingw-release" }
$buildPreset = if ($Configuration -eq "Debug") { "build-mingw-debug" } else { "build-mingw-release" }
$exePath = Join-Path $PSScriptRoot "..\build\mingw-$($Configuration.ToLowerInvariant())\ce_pipeline_test.exe"

& $cmake --preset $configurePreset
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $cmake --build --preset $buildPreset
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($Run) {
    & $exePath
}
