param(
    [ValidateSet("x64")]
    [string]$Arch = "x64"
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
$devCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
$tempFile = [System.IO.Path]::GetTempFileName()

try {
    $batch = "@echo off && call `"$devCmd`" -arch=$Arch -host_arch=$Arch >nul && set > `"$tempFile`""
    cmd /d /c $batch | Out-Null

    Get-Content $tempFile | ForEach-Object {
        if ($_ -match "^(.*?)=(.*)$") {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }

    Write-Host "MSVC developer environment loaded from $installPath"
    Write-Host "Try: cl"
    Write-Host "Try: cmake --version"
} finally {
    Remove-Item $tempFile -ErrorAction SilentlyContinue
}
