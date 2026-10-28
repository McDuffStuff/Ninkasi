# Makes Ninkasi's icon, assets\ninkasi.ico, from a square picture.
#
#   .\scripts\make-icon.ps1                       # from assets\ninkasi.png
#   .\scripts\make-icon.ps1 -Picture logo.png
#
# The picture should be square and at least 256 pixels a side, ideally with a
# transparent background. The icon holds it at every size Windows asks for,
# from 16 to 256 pixels; the next build puts it on ninkasi.exe, its windows
# and the installer.
param([string] $Picture)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
if (-not $Picture) { $Picture = "$root\assets\ninkasi.png" }
if (-not (Test-Path $Picture)) { throw "There's no picture at $Picture." }

$source = [System.Drawing.Image]::FromFile((Resolve-Path $Picture))
try {
    if ($source.Width -ne $source.Height) { Write-Warning "The picture isn't square, so the icon will be squashed." }
    $sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
    $images = foreach ($size in $sizes) {
        $bitmap = New-Object System.Drawing.Bitmap $size, $size
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.DrawImage($source, 0, 0, $size, $size)
        $graphics.Dispose()
        $stream = New-Object System.IO.MemoryStream
        $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
        , $stream.ToArray()
    }
}
finally {
    $source.Dispose()
}

# An .ico file: a header, a directory entry per size, then each size as a PNG.
$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([UInt16]0)              # reserved
$writer.Write([UInt16]1)              # 1: an icon
$writer.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $side = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }  # 0 means 256
    $writer.Write([byte]$side)            # width
    $writer.Write([byte]$side)            # height
    $writer.Write([byte]0)                # no palette
    $writer.Write([byte]0)                # reserved
    $writer.Write([UInt16]1)              # color planes
    $writer.Write([UInt16]32)             # bits per pixel
    $writer.Write([UInt32]$images[$i].Length)
    $writer.Write([UInt32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write($image) }
$writer.Flush()
New-Item -ItemType Directory -Force "$root\assets" | Out-Null
[IO.File]::WriteAllBytes("$root\assets\ninkasi.ico", $out.ToArray())
"Made $root\assets\ninkasi.ico. Build again to put it on Ninkasi."
