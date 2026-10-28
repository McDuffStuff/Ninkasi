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

# A square bitmap as an icon image: a BITMAPINFOHEADER, the 32-bit pixels
# (blue, green, red, alpha) from the bottom row up, then the transparency
# mask for programs that ignore alpha: one bit a pixel, set where it's see-
# through, from the bottom row up with rows padded to 4 bytes.
function ConvertTo-IconBitmap([System.Drawing.Bitmap] $bitmap) {
    $side = $bitmap.Width
    $rect = New-Object System.Drawing.Rectangle 0, 0, $side, $side
    $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                             [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $pixels = New-Object byte[] ($data.Stride * $side)
    [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixels, 0, $pixels.Length)
    $bitmap.UnlockBits($data)

    $maskRow = [int]([math]::Ceiling($side / 32.0) * 4)
    $stream = New-Object System.IO.MemoryStream
    $out = New-Object System.IO.BinaryWriter $stream
    $out.Write([UInt32]40)                                 # header size
    $out.Write([Int32]$side)                               # width
    $out.Write([Int32]($side * 2))                         # height: the pixels and the mask
    $out.Write([UInt16]1)                                  # planes
    $out.Write([UInt16]32)                                 # bits per pixel
    $out.Write([UInt32]0)                                  # no compression
    $out.Write([UInt32]($side * $side * 4 + $maskRow * $side))
    $out.Write([Int32]0); $out.Write([Int32]0)             # resolution
    $out.Write([UInt32]0); $out.Write([UInt32]0)           # colors
    for ($y = $side - 1; $y -ge 0; $y--) {
        $out.Write($pixels, $y * $data.Stride, $side * 4)  # Format32bppArgb is already B, G, R, A
    }
    $mask = New-Object byte[] ($maskRow * $side)
    for ($y = 0; $y -lt $side; $y++) {
        $row = ($side - 1 - $y) * $maskRow
        for ($x = 0; $x -lt $side; $x++) {
            if ($pixels[$y * $data.Stride + $x * 4 + 3] -lt 128) {
                $mask[$row + [math]::Floor($x / 8)] = $mask[$row + [math]::Floor($x / 8)] -bor (0x80 -shr ($x % 8))
            }
        }
    }
    $out.Write($mask)
    $out.Flush()
    return , $stream.ToArray()  # the comma keeps PowerShell from unrolling the bytes
}

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
        if ($size -ge 256) {
            # The largest size as a PNG, which keeps the file small.
            $stream = New-Object System.IO.MemoryStream
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            , $stream.ToArray()
        } else {
            # Smaller sizes as classic bitmaps, which everything that reads icons understands.
            , (ConvertTo-IconBitmap $bitmap)
        }
        $bitmap.Dispose()
    }
}
finally {
    $source.Dispose()
}

# An .ico file: a header, a directory entry per size, then each size's image.
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
