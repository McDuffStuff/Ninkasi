# Builds Ninkasi for release, runs the tests, and packages it for Windows in dist\:
#   Ninkasi-<version>-windows-x64.zip   ninkasi.exe with its readme and licenses, to unzip and run
#   Ninkasi-<version>-setup.exe         an installer, made with Inno Setup 6
#
#   .\scripts\package.ps1                  # both
#   .\scripts\package.ps1 -SkipInstaller   # just the zip (no Inno Setup needed)
#
# The version is the one in CMakeLists.txt: project(Ninkasi VERSION ...).
# Inno Setup: winget install JRSoftware.InnoSetup
param([switch] $SkipInstaller)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

function Write-Utf8([string] $path, [string] $text) {
    [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding $false))
}

# The licenses of everything compiled into ninkasi.exe, read from the sources
# the build fetched, so they always match the versions shipped.
function Write-Notices([string] $deps, [string] $path) {
    $libraries = @(
        @('Dear ImGui', 'https://github.com/ocornut/imgui', 'imgui-src\LICENSE.txt'),
        @('ImPlot', 'https://github.com/epezent/implot', 'implot-src\LICENSE'),
        @('GLFW', 'https://www.glfw.org', 'glfw-src\LICENSE.md'),
        @('JSON for Modern C++', 'https://github.com/nlohmann/json', 'json-src\LICENSE.MIT'),
        @('stb_image', 'https://github.com/nothings/stb', 'stb-src\LICENSE'),
        @('Argon2 reference implementation', 'https://github.com/P-H-C/phc-winner-argon2', 'argon2-src\LICENSE')
    )
    $rule = '=' * 78
    $text = "Ninkasi is built with the following open-source software. Their licenses follow.`r`n"
    foreach ($library in $libraries) {
        $license = [IO.File]::ReadAllText((Join-Path $deps $library[2]))
        $text += "`r`n$rule`r`n$($library[0])  ($($library[1]))`r`n$rule`r`n`r`n$($license.Trim())`r`n"
    }
    $text += "`r`n$rule`r`nSQLite  (https://sqlite.org)`r`n$rule`r`n`r`n"
    $text += "SQLite is in the public domain: its authors disclaim copyright to its source code.`r`n"
    $text += "`r`n$rule`r`nRoboto (the font)  (https://fonts.google.com/specimen/Roboto)`r`n$rule`r`n`r`n"
    $text += "Copyright 2011 Google Inc. All Rights Reserved.`r`n"
    $text += "Licensed under the Apache License, Version 2.0, whose text is in the Argon2 section above.`r`n"
    Write-Utf8 $path $text
}

& "$PSScriptRoot\build.ps1" -Config release
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$cmake = [IO.File]::ReadAllText("$root\CMakeLists.txt")
$version = [regex]::Match($cmake, 'project\(Ninkasi VERSION ([0-9.]+)').Groups[1].Value
$publisher = [regex]::Match($cmake, 'set\(NINKASI_PUBLISHER "([^"]+)"').Groups[1].Value
if (-not $version) { throw "Couldn't find the version in CMakeLists.txt." }

$build = "$root\build\release"
$dist = "$root\dist"
$stage = "$dist\Ninkasi-$version"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item "$build\ninkasi.exe" $stage
Write-Utf8 "$stage\README.txt" ([IO.File]::ReadAllText("$root\packaging\README.txt").Replace('{version}', $version))
Write-Notices "$build\_deps" "$stage\THIRD-PARTY-NOTICES.txt"

# The zip holds a Ninkasi-<version> folder, so unzipping doesn't scatter files.
$zip = "$dist\Ninkasi-$version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip }
# Each entry named with "/" as zip files should be: Windows PowerShell's
# Compress-Archive (and .NET under it) write "\", which other unzip tools misread.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem $stage -File -Recurse) {
        $name = "Ninkasi-$version/" + $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $name,
                                                                    [IO.Compression.CompressionLevel]::Optimal)
    }
}
finally {
    $archive.Dispose()
}
"Made $zip"

if (-not $SkipInstaller) {
    $iscc = @(
        (Get-Command iscc -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $iscc) {
        throw 'Inno Setup 6 is needed for the installer: winget install JRSoftware.InnoSetup (or use -SkipInstaller).'
    }
    $arguments = @("/Q", "/DAppVersion=$version", "/DPublisher=$publisher", "/DStageDir=$stage", "/DOutputDir=$dist")
    if (Test-Path "$root\assets\ninkasi.ico") { $arguments += "/DIconFile=$root\assets\ninkasi.ico" }
    & $iscc @arguments "$root\packaging\ninkasi.iss"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    "Made $dist\Ninkasi-$version-setup.exe"
}
