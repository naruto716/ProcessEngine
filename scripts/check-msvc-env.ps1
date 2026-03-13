param()

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
$vsCmake = Join-Path $installPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$devCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
$cmd = "@echo off && call `"$devCmd`" -arch=x64 -host_arch=x64 >nul && cl && `"$vsCmake`" --version"
cmd /d /c $cmd
