Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot
if (-not $root) { $root = "C:\Users\nickr\Projects\R5OS" }
$art = Join-Path $root "distro\artwork"
New-Item -ItemType Directory -Force -Path $art | Out-Null

$jpg = Join-Path $art "r5-os-logo.jpg"
$srcAsset = "C:\Users\nickr\.cursor\projects\c-Users-nickr-Projects-R5OS\assets\2JfkJ-6ab3390b-3294-4b10-9f44-fa0cbbb192d5.jpg"
Copy-Item -Force $srcAsset $jpg

$bmp = [System.Drawing.Bitmap]::FromFile($jpg)
$out = New-Object System.Drawing.Bitmap $bmp.Width, $bmp.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$minX = $bmp.Width; $minY = $bmp.Height; $maxX = 0; $maxY = 0
for ($y = 0; $y -lt $bmp.Height; $y++) {
	for ($x = 0; $x -lt $bmp.Width; $x++) {
		$c = $bmp.GetPixel($x, $y)
		if ($c.R -gt 245 -and $c.G -gt 245 -and $c.B -gt 245) {
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
$w = $maxX - $minX + 1
$h = $maxY - $minY + 1
$crop = $out.Clone((New-Object System.Drawing.Rectangle $minX, $minY, $w, $h), $out.PixelFormat)
$out.Dispose()

$maxW = 170
if ($crop.Width -gt $maxW) {
	$nh = [int]($crop.Height * ($maxW / [double]$crop.Width))
	$resized = New-Object System.Drawing.Bitmap $maxW, $nh
	$g = [System.Drawing.Graphics]::FromImage($resized)
	$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
	$g.DrawImage($crop, 0, 0, $maxW, $nh)
	$g.Dispose()
	$crop.Dispose()
	$crop = $resized
}

$png = Join-Path $art "r5-os-logo.png"
$crop.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host ("Wrote {0} {1}x{2}" -f $png, $crop.Width, $crop.Height)
$crop.Dispose()
