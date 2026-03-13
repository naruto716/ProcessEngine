param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [switch]$Run
)

$ErrorActionPreference = "Stop"

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $json = & $vswhere -all -prerelease -format json | ConvertFrom-Json
        if ($json) {
            $instance = $json | Select-Object -First 1
            if ($instance.installationPath) {
                return $instance.installationPath.TrimEnd('\')
            }
        }
    }

    $instances = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*", "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*" -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -like "Visual Studio *" -and $_.InstallLocation }

    $preferred = $instances | Where-Object { $_.DisplayName -like "*2026*" } | Select-Object -First 1
    if ($preferred) {
        return $preferred.InstallLocation.TrimEnd('\')
    }

    $fallback = $instances | Select-Object -First 1
    if ($fallback) {
        return $fallback.InstallLocation.TrimEnd('\')
    }

    throw "Visual Studio was not found."
}

$installPath = Get-VsInstallPath
$cmake = Join-Path $installPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$configurePreset = if ($Configuration -eq "Debug") { "msvc-debug" } else { "msvc-release" }
$buildPreset = if ($Configuration -eq "Debug") { "build-msvc-debug" } else { "build-msvc-release" }
$exePath = Join-Path $PSScriptRoot "..\build\msvc-$($Configuration.ToLowerInvariant())\$Configuration\ce_pipeline_test.exe"

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
