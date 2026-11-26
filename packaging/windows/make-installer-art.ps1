# Draws the installer's page backgrounds into dist\installer-art, in light and
# dark, at the sizes Inno Setup picks from by the screen's DPI. Each is a warm
# glow and a faint kitty cat in the lower right corner, out of the way of the
# page's text. kitty.iss reads them as light-*.png and dark-*.png.
param([string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path)

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

$out = Join-Path $Root 'dist\installer-art'
New-Item -ItemType Directory -Force $out | Out-Null
$cat = [System.Drawing.Image]::FromFile((Join-Path $PSScriptRoot 'art\kitty-1024.png'))

# The page area's size at 100%, 150%, 200% and 250% DPI, Inno Setup's 497:360
$sizes = @(@(596, 432), @(994, 720), @(1272, 922), @(1630, 1181))
# The base colours are Inno Setup's own Windows 11 ones, the glow the cat's fur
$looks = @{
    light = @{ Base = [System.Drawing.Color]::FromArgb(255, 0xf3, 0xf3, 0xf3); Glow = 34; CatAlpha = 0.14 }
    dark  = @{ Base = [System.Drawing.Color]::FromArgb(255, 0x20, 0x20, 0x20); Glow = 60; CatAlpha = 0.20 }
}
$fur = [System.Drawing.Color]::FromArgb(255, 0x8a, 0x4f, 0x2a)

foreach ($name in $looks.Keys) {
    $look = $looks[$name]
    foreach ($size in $sizes) {
        $w, $h = $size
        $bmp = New-Object System.Drawing.Bitmap $w, $h
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.SmoothingMode = 'AntiAlias'
        $g.InterpolationMode = 'HighQualityBicubic'
        $g.Clear($look.Base)

        # A glow of the cat's fur colour rising from the lower right corner
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $r = [int]($h * 0.95)
        $path.AddEllipse([int]($w - $r * 0.9), [int]($h - $r * 0.85), 2 * $r, 2 * $r)
        $glow = New-Object System.Drawing.Drawing2D.PathGradientBrush $path
        $glow.CenterColor = [System.Drawing.Color]::FromArgb($look.Glow, $fur)
        $glow.SurroundColors = @([System.Drawing.Color]::FromArgb(0, $fur))
        $g.FillPath($glow, $path)

        # The cat, faint, peeking in from the corner
        $side = [int]($h * 0.78)
        $matrix = New-Object System.Drawing.Imaging.ColorMatrix
        $matrix.Matrix33 = $look.CatAlpha
        $attrs = New-Object System.Drawing.Imaging.ImageAttributes
        $attrs.SetColorMatrix($matrix)
        $dest = New-Object System.Drawing.Rectangle ([int]($w - $side * 0.86)), ([int]($h - $side * 0.92)), $side, $side
        $g.DrawImage($cat, $dest, 0, 0, $cat.Width, $cat.Height, 'Pixel', $attrs)

        $g.Dispose()
        $bmp.Save((Join-Path $out "$name-${w}x$h.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
    }
}
$cat.Dispose()
Write-Host "==> installer art in $out"
