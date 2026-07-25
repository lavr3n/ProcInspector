Add-Type -AssemblyName System.Drawing

$sizes = 16,24,32,48,64,128,256
$streams = @()

function New-IconBitmap([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    $k = $s / 256.0
    function U([double]$v) { return [float]($v * $k) }

    $r = U 48
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = [float]($r * 2)
    $w = [float]$s
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($w - $d, 0, $d, $d, 270, 90)
    $path.AddArc($w - $d, $w - $d, $d, $d, 0, 90)
    $path.AddArc(0, $w - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $rect = New-Object System.Drawing.RectangleF 0, 0, $w, $w
    $c1 = [System.Drawing.Color]::FromArgb(255, 40, 110, 235)
    $c2 = [System.Drawing.Color]::FromArgb(255, 16, 32, 72)
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, $c1, $c2, 55.0
    $g.FillPath($brush, $path)

    $barBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(235, 255, 255, 255))
    $barDim   = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(140, 255, 255, 255))
    $bh = U 22
    $ys = @(62, 106, 150, 194)
    $ws = @(150, 118, 164, 96)
    for ($i = 0; $i -lt 4; $i++) {
        $b = $barDim
        if ($i % 2 -eq 0) { $b = $barBrush }
        $g.FillRectangle($b, [float](U 46), [float](U $ys[$i]), [float](U $ws[$i]), [float]$bh)
    }

    $penW = [float](U 20)
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 255, 255, 255)), $penW
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $shadow = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(120, 8, 20, 45)), ([float](U 30))
    $cx = U 158; $cy = U 148; $rad = U 56
    $g.DrawEllipse($shadow, [float]($cx - $rad), [float]($cy - $rad), [float]($rad * 2), [float]($rad * 2))
    $g.DrawEllipse($pen, [float]($cx - $rad), [float]($cy - $rad), [float]($rad * 2), [float]($rad * 2))
    $g.DrawLine($pen, [float]($cx + $rad * 0.72), [float]($cy + $rad * 0.72), [float](U 214), [float](U 204))

    $g.Dispose()
    return $bmp
}

function Get-DibBytes([System.Drawing.Bitmap]$bmp) {
    $w = $bmp.Width; $h = $bmp.Height
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    $maskStride = [int](([math]::Floor(($w + 31) / 32)) * 4)
    $bw.Write([UInt32]40)
    $bw.Write([Int32]$w)
    $bw.Write([Int32]($h * 2))
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]0)
    $bw.Write([UInt32]($w * $h * 4 + $maskStride * $h))
    $bw.Write([Int32]0); $bw.Write([Int32]0)
    $bw.Write([UInt32]0); $bw.Write([UInt32]0)
    for ($y = $h - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $bw.Write([Byte]$c.B); $bw.Write([Byte]$c.G); $bw.Write([Byte]$c.R); $bw.Write([Byte]$c.A)
        }
    }
    $zero = New-Object Byte[] ($maskStride * $h)
    $bw.Write($zero)
    $bw.Flush()
    $bytes = $ms.ToArray()
    $bw.Close(); $ms.Dispose()
    return ,$bytes
}

foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    if ($s -eq 256) { $bmp.Save((Join-Path $PSScriptRoot "icon_preview.png"), [System.Drawing.Imaging.ImageFormat]::Png) }
    if ($s -le 64) {
        $streams += ,@($s, (Get-DibBytes $bmp))
    } else {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $streams += ,@($s, $ms.ToArray())
        $ms.Dispose()
    }
    $bmp.Dispose()
}

$out = Join-Path $PSScriptRoot "app.ico"
$fs = [System.IO.File]::Create($out)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$streams.Count)

$offset = 6 + 16 * $streams.Count
foreach ($e in $streams) {
    $s = $e[0]; $data = [byte[]]$e[1]
    $dim = [Byte]0
    if ($s -lt 256) { $dim = [Byte]$s }
    $bw.Write($dim)
    $bw.Write($dim)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$data.Length)
    $bw.Write([UInt32]$offset)
    $offset += $data.Length
}
foreach ($e in $streams) { $bw.Write([byte[]]$e[1]) }
$bw.Flush(); $bw.Close(); $fs.Close()
Write-Host "OK: $out"
