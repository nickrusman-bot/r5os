Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot
$art = Join-Path $root "distro\artwork"
$srcPath = Join-Path $art "r5-os-logo.png"
if (-not (Test-Path $srcPath)) {
	throw "Missing $srcPath - run distill-logo.ps1 first."
}

function New-TransparentBitmap([int]$w, [int]$h) {
	$bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
	$g.Dispose()
	return $bmp
}

function Draw-Contained([System.Drawing.Bitmap]$dst, [System.Drawing.Bitmap]$src, [int]$pad) {
	$availW = $dst.Width - (2 * $pad)
	$availH = $dst.Height - (2 * $pad)
	$scale = [Math]::Min($availW / [double]$src.Width, $availH / [double]$src.Height)
	$nw = [Math]::Max(1, [int][Math]::Round($src.Width * $scale))
	$nh = [Math]::Max(1, [int][Math]::Round($src.Height * $scale))
	$x = [int](($dst.Width - $nw) / 2)
	$y = [int](($dst.Height - $nh) / 2)
	$g = [System.Drawing.Graphics]::FromImage($dst)
	$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
	$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
	$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
	$g.DrawImage($src, $x, $y, $nw, $nh)
	$g.Dispose()
}

function Set-LogoSubtitleR5OS([string]$path) {
	$bmp = [System.Drawing.Bitmap]::FromFile($path)
	$clone = New-Object System.Drawing.Bitmap $bmp.Width, $bmp.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
	$g0 = [System.Drawing.Graphics]::FromImage($clone)
	$g0.DrawImage($bmp, 0, 0, $bmp.Width, $bmp.Height)
	$g0.Dispose()
	$bmp.Dispose()

	$rowFill = New-Object int[] $clone.Height
	for ($y = 0; $y -lt $clone.Height; $y++) {
		$n = 0
		for ($x = 0; $x -lt $clone.Width; $x++) {
			$c = $clone.GetPixel($x, $y)
			if ($c.A -gt 24) { $n++ }
		}
		$rowFill[$y] = $n
	}
	$half = [int]($clone.Height * 0.55)
	$gapStart = -1
	$emptyRun = 0
	for ($y = $half; $y -lt $clone.Height; $y++) {
		if ($rowFill[$y] -lt [Math]::Max(3, $clone.Width / 40)) {
			$emptyRun++
			if ($emptyRun -ge 4 -and $gapStart -lt 0) {
				$gapStart = $y - $emptyRun + 1
			}
		} else {
			if ($gapStart -ge 0) { break }
			$emptyRun = 0
		}
	}
	if ($gapStart -lt 0) {
		$gapStart = [int]($clone.Height * 0.72)
	}

	$g = [System.Drawing.Graphics]::FromImage($clone)
	$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
	$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
	$g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
	$g.FillRectangle(
		[System.Drawing.Brushes]::Transparent,
		0, $gapStart, $clone.Width, $clone.Height - $gapStart)
	$g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver

	$fontSize = [Math]::Max(12, [int]($clone.Height * 0.13))
	$font = New-Object System.Drawing.Font "Segoe UI", $fontSize, ([System.Drawing.FontStyle]::Bold)
	$label = "R5OS"
	$sz = $g.MeasureString($label, $font)
	$x = ($clone.Width - $sz.Width) / 2
	$y = $gapStart + (($clone.Height - $gapStart - $sz.Height) / 2)
	$rect = New-Object System.Drawing.RectangleF $x, $y, $sz.Width, $sz.Height
	$c1 = [System.Drawing.Color]::FromArgb(255, 70, 130, 230)
	$c2 = [System.Drawing.Color]::FromArgb(255, 170, 130, 235)
	$brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (
		(New-Object System.Drawing.PointF $x, $y),
		(New-Object System.Drawing.PointF ($x + $sz.Width), ($y + $sz.Height)),
		$c1, $c2)
	$g.DrawString($label, $font, $brush, $rect)
	$brush.Dispose()
	$font.Dispose()
	$g.Dispose()

	$clone.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
	$clone.Dispose()
	Write-Host ("Updated logo subtitle to R5OS: {0}" -f $path)
}

Set-LogoSubtitleR5OS $srcPath

$src = [System.Drawing.Bitmap]::FromFile($srcPath)

$desk = New-TransparentBitmap 63 22
Draw-Contained $desk $src 1
$deskPath = Join-Path $art "r5-os-deskbar.png"
$desk.Save($deskPath, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host ("Wrote {0} {1}x{2}" -f $deskPath, $desk.Width, $desk.Height)
$desk.Dispose()

# Splash: 372x96, opaque black (boot loader strips alpha → RGB).
$splash = New-Object System.Drawing.Bitmap 372, 96, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$sg = [System.Drawing.Graphics]::FromImage($splash)
$sg.Clear([System.Drawing.Color]::FromArgb(255, 0, 0, 0))
$sg.Dispose()
Draw-Contained $splash $src 8
$splashPath = Join-Path $art "r5-os-splash.png"
$splash.Save($splashPath, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host ("Wrote {0} {1}x{2}" -f $splashPath, $splash.Width, $splash.Height)

# Raw BGR for generate_images_sans_tm.py (matches generate_boot_screen).
$bgrPath = Join-Path $art "r5-os-splash.bgr"
$bytes = New-Object byte[] ($splash.Width * $splash.Height * 3)
$i = 0
for ($y = 0; $y -lt $splash.Height; $y++) {
	for ($x = 0; $x -lt $splash.Width; $x++) {
		$c = $splash.GetPixel($x, $y)
		$bytes[$i] = $c.B; $i++
		$bytes[$i] = $c.G; $i++
		$bytes[$i] = $c.R; $i++
	}
}
[System.IO.File]::WriteAllBytes($bgrPath, $bytes)
Write-Host ("Wrote {0} {1} bytes" -f $bgrPath, $bytes.Length)

$splash.Dispose()
$src.Dispose()
