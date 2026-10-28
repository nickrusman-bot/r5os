Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot
$art = Join-Path $root "distro\artwork"
$srcPath = Join-Path $art "r5-os-logo.png"
$blackPath = Join-Path $art "r5-os-logo-on-black.png"
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

$src = [System.Drawing.Bitmap]::FromFile($srcPath)
$srcBlack = $null
if (Test-Path $blackPath) {
	$srcBlack = [System.Drawing.Bitmap]::FromFile($blackPath)
} else {
	$srcBlack = $src
}

$desk = New-TransparentBitmap 63 22
Draw-Contained $desk $src 1
$deskPath = Join-Path $art "r5-os-deskbar.png"
$desk.Save($deskPath, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host ("Wrote {0} {1}x{2}" -f $deskPath, $desk.Width, $desk.Height)
$desk.Dispose()

# Splash: 372x96, opaque black. Use the reverse (light) mark.
$splash = New-Object System.Drawing.Bitmap 372, 96, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$sg = [System.Drawing.Graphics]::FromImage($splash)
$sg.Clear([System.Drawing.Color]::FromArgb(255, 0, 0, 0))
$sg.Dispose()
Draw-Contained $splash $srcBlack 10
$splashPath = Join-Path $art "r5-os-splash.png"
$splash.Save($splashPath, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host ("Wrote {0} {1}x{2}" -f $splashPath, $splash.Width, $splash.Height)

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
if ($srcBlack -ne $src) { $srcBlack.Dispose() }
$src.Dispose()
