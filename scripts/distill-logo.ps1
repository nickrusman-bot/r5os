Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot
if (-not $root) { $root = "C:\Users\nickr\Projects\R5OS" }
$art = Join-Path $root "distro\artwork"
New-Item -ItemType Directory -Force -Path $art | Out-Null

$jpg = Join-Path $art "r5-os-logo.jpg"
if (-not (Test-Path $jpg)) {
	throw "Missing logo source $jpg"
}

$bmp = [System.Drawing.Bitmap]::FromFile($jpg)
$out = New-Object System.Drawing.Bitmap $bmp.Width, $bmp.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$minX = $bmp.Width; $minY = $bmp.Height; $maxX = 0; $maxY = 0
for ($y = 0; $y -lt $bmp.Height; $y++) {
	for ($x = 0; $x -lt $bmp.Width; $x++) {
		$c = $bmp.GetPixel($x, $y)
		# Knock out near-white JPEG background; keep ink + soft shadow.
		$nearWhite = ($c.R -gt 236) -and ($c.G -gt 236) -and ($c.B -gt 236)
		if ($nearWhite) {
			$out.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(0, 255, 255, 255))
		} else {
			$out.SetPixel($x, $y, $c)
			if ($x -lt $minX) { $minX = $x }
			if ($y -lt $minY) { $minY = $y }
			if ($x -gt $maxX) { $maxX = $x }
			if ($y -gt $maxY) { $maxY = $y }
		}
	}
}
$bmp.Dispose()

$pad = 8
$minX = [Math]::Max(0, $minX - $pad)
$minY = [Math]::Max(0, $minY - $pad)
$maxX = [Math]::Min($out.Width - 1, $maxX + $pad)
$maxY = [Math]::Min($out.Height - 1, $maxY + $pad)
$w = $maxX - $minX + 1
$h = $maxY - $minY + 1
$crop = $out.Clone((New-Object System.Drawing.Rectangle $minX, $minY, $w, $h), $out.PixelFormat)
$out.Dispose()

$maxW = 420
if ($crop.Width -gt $maxW) {
	$nh = [int]($crop.Height * ($maxW / [double]$crop.Width))
	$resized = New-Object System.Drawing.Bitmap $maxW, $nh, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
	$g = [System.Drawing.Graphics]::FromImage($resized)
	$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
	$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
	$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
	$g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
	$g.DrawImage($crop, 0, 0, $maxW, $nh)
	$g.Dispose()
	$crop.Dispose()
	$crop = $resized
}

$png = Join-Path $art "r5-os-logo.png"
$crop.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host ("Wrote {0} {1}x{2}" -f $png, $crop.Width, $crop.Height)

# Reverse mark: dark ink becomes white with alpha, then sit on black.
$onBlack = New-Object System.Drawing.Bitmap $crop.Width, $crop.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
for ($y = 0; $y -lt $crop.Height; $y++) {
	for ($x = 0; $x -lt $crop.Width; $x++) {
		$c = $crop.GetPixel($x, $y)
		if ($c.A -lt 8) {
			$onBlack.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, 0, 0, 0))
			continue
		}
		# Navy (~15,43,83) is not black; stretch darkness so letters go to
		# solid white while the gray drop-shadow stays a soft halo.
		$ink = ((255 - $c.R) + (255 - $c.G) + (255 - $c.B)) / 3.0
		$stretched = [Math]::Min(255.0, $ink * 255.0 / 200.0)
		$a = [Math]::Min(255, [int]($c.A * $stretched / 255.0))
		$onBlack.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($a, 255, 255, 255))
	}
}
$blackPath = Join-Path $art "r5-os-logo-on-black.png"
$canvas = New-Object System.Drawing.Bitmap $onBlack.Width, $onBlack.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$cg = [System.Drawing.Graphics]::FromImage($canvas)
$cg.Clear([System.Drawing.Color]::FromArgb(255, 0, 0, 0))
$cg.DrawImage($onBlack, 0, 0)
$cg.Dispose()
$canvas.Save($blackPath, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host ("Wrote {0} {1}x{2}" -f $blackPath, $canvas.Width, $canvas.Height)

$crop.Dispose()
$onBlack.Dispose()
$canvas.Dispose()
