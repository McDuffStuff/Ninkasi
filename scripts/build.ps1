# Configure, build and test Ninkasi on Windows.
# Loads the Visual Studio developer environment (MSVC, CMake, Ninja) first,
# so this works from any PowerShell window.
#
#   .\scripts\build.ps1                 # debug build + tests
#   .\scripts\build.ps1 -Config release
#   .\scripts\build.ps1 -Run            # build, test, then launch the app
param(
    [ValidateSet('debug', 'release')] [string] $Config = 'debug',
    [switch] $Run
)
$ErrorActionPreference = 'Stop'

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'Visual Studio with the C++ workload was not found.' }
    # VsDevCmd calls vswhere itself and expects to find it on PATH.
    $env:PATH = "$(Split-Path $vswhere);$env:PATH"
    & "$vsPath\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}

Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    cmake --preset $Config;        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    cmake --build --preset $Config; if ($LASTEXITCODE) { exit $LASTEXITCODE }
    ctest --preset $Config;         if ($LASTEXITCODE) { exit $LASTEXITCODE }
    if ($Run) { & ".\build\$Config\ninkasi.exe" }
}
finally {
    Pop-Location
}
