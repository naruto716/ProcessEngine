param()

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

$cmakeCommand = Get-CMakeCommand

$tools = @(
    @{ Name = "cmake"; Command = "& `"$cmakeCommand`" --version" },
    @{ Name = "g++"; Command = "g++ --version" },
    @{ Name = "mingw32-make"; Command = "mingw32-make --version" }
)

foreach ($tool in $tools) {
    Write-Host "=== $($tool.Name) ==="
    Invoke-Expression $tool.Command
    Write-Host ""
}
