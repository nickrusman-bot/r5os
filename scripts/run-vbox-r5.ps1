# Boot r5-os.image in VirtualBox on the LAN (bridged Intel PRO/1000).
# Haiku has no VirtualBox Guest Additions in this tree:
# no vboxsf, no auto-resize, no VBox drag-and-drop.
# Text clipboard uses COM1 (named pipe), not Devices > Shared Clipboard.
#
# Usage:
#   powershell -File scripts/run-vbox-r5.ps1
#   powershell -File scripts/run-vbox-r5.ps1 -BridgeAdapter "Wi-Fi"
#   powershell -File scripts/run-vbox-r5.ps1 -ForceConvert
param(
	[string]$VmName = "R5OS",
	[string]$BridgeAdapter = $env:R5_VBOX_BRIDGE,
	[int]$MemoryMb = 2048,
	[int]$Cpus = 2,
	[switch]$NoClip,
	[switch]$ForceConvert
)

# VBoxManage prints progress on stderr; Stop would treat that as fatal.
$ErrorActionPreference = "Continue"

function Find-VBoxManage {
	$cmd = Get-Command VBoxManage -ErrorAction SilentlyContinue
	if ($cmd) {
		return $cmd.Source
	}
	$guess = @(
		"${env:ProgramFiles}\Oracle\VirtualBox\VBoxManage.exe",
		"${env:ProgramFiles(x86)}\Oracle\VirtualBox\VBoxManage.exe"
	)
	foreach ($path in $guess) {
		if (Test-Path $path) {
			return $path
		}
	}
	throw "VBoxManage not found. Install VirtualBox and retry."
}

function Get-RawImagePath {
	$wsl = wsl -e bash -lc "wslpath -w /home/nickr/r5os/haiku/generated.x86_64/r5-os.image 2>/dev/null"
	if ($LASTEXITCODE -eq 0 -and $wsl -and (Test-Path $wsl.Trim())) {
		return $wsl.Trim()
	}
	throw "r5-os.image not found. Jam AMD64 first (scripts/jam-r5-raw.sh)."
}

function Detach-IdeDisk([string]$vbox, [string]$name) {
	& $vbox storageattach $name --storagectl IDE --port 0 --device 0 --medium none 2>&1 | Out-Null
}

function Get-BridgeName([string]$vbox, [string]$preferred) {
	$names = & $vbox list bridgedifs | ForEach-Object {
		if ($_ -match '^Name:\s+(.+)$') { $Matches[1].Trim() }
	}
	if (-not $names) {
		throw "No bridged adapters. Enable a host NIC and retry."
	}
	if ($preferred) {
		$hit = $names | Where-Object { $_ -eq $preferred -or $_ -like "*$preferred*" } | Select-Object -First 1
		if ($hit) { return $hit }
		throw "Bridge adapter '$preferred' not in: $($names -join ', ')"
	}
	return $names[0]
}

$vbox = Find-VBoxManage
$raw = Get-RawImagePath
$outDir = Join-Path $PSScriptRoot "..\generated"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$vdi = Join-Path (Resolve-Path $outDir) "r5-os.vdi"
$localRaw = Join-Path (Resolve-Path $outDir) "r5-os.img"

Write-Host "R5OS VirtualBox"
Write-Host "  No vboxsf, no auto-resize, no VBox drag-and-drop."
Write-Host "  Text clipboard: COM1 pipe (not Guest Additions)."
Write-Host "  Disk: Intel IDE  NIC: Intel PRO/1000 (ipro1000)  Net: bridged"
Write-Host "  raw: $raw"

# Powering the VM off rewrites the VDI timestamp. Remember the old
# time first, or a freshly built raw image looks older than the disk.
$vdiEpochBefore = $null
if (Test-Path $vdi) {
	$vdiItem = Get-Item $vdi
	$vdiEpochBefore = [DateTimeOffset]::new($vdiItem.LastWriteTimeUtc).ToUnixTimeSeconds()
}

$running = & $vbox list runningvms
if ($running -match "`"$VmName`"") {
	Write-Host "Stopping $VmName ..."
	& $vbox controlvm $VmName poweroff | Out-Null
	Start-Sleep -Seconds 2
}

$all = & $vbox list vms
$exists = $all -match "`"$VmName`""
if ($exists) {
	Detach-IdeDisk $vbox $VmName
}

# \\wsl.localhost LastWriteTime is often stale. Use the Linux mtime.
$rawEpoch = wsl -e bash -lc "stat -c %Y /home/nickr/r5os/haiku/generated.x86_64/r5-os.image"
$reuse = $false
if (-not $ForceConvert -and (Test-Path $vdi) -and $rawEpoch -match '^\d+$' -and $vdiEpochBefore -ne $null) {
	$vdiItem = Get-Item $vdi
	if ($vdiItem.Length -gt 0 -and $vdiEpochBefore -ge [int64]$rawEpoch) {
		$reuse = $true
	}
}

if ($reuse) {
	Write-Host "  reusing $vdi (raw not newer; -ForceConvert to rebuild)"
} else {
	if (Test-Path $vdi) {
		& $vbox closemedium disk $vdi --delete 2>$null | Out-Null
		Remove-Item -Force $vdi -ErrorAction SilentlyContinue
	}
	# Convert from a local NTFS copy. Reading the raw image through
	# \\wsl.localhost on first power-on often hangs until the VM is reset.
	Write-Host "  copying raw image to $localRaw"
	wsl -e bash -lc "cp -f /home/nickr/r5os/haiku/generated.x86_64/r5-os.image /mnt/c/Users/nickr/Projects/R5OS/generated/r5-os.img"
	if ($LASTEXITCODE -ne 0 -or -not (Test-Path $localRaw)) {
		throw "copy raw image failed"
	}
	& $vbox convertfromraw $localRaw $vdi --format VDI --variant Fixed
	if ($LASTEXITCODE -ne 0) {
		throw "convertfromraw failed"
	}
	Start-Sleep -Seconds 2
	Write-Host "  new VDI: first boot runs package_daemon for every package."
	Write-Host "  That often takes several minutes. Wait for the desktop."
	Write-Host "  Do not Reset; a guest Restart of this VDI is fast afterwards."
}

if (-not $exists) {
	& $vbox createvm --name $VmName --ostype Other_64 --register | Out-Null
	& $vbox storagectl $VmName --name IDE --add ide --controller PIIX4 | Out-Null
}

$bridge = Get-BridgeName $vbox $BridgeAdapter
Write-Host "  bridge: $bridge"

& $vbox modifyvm $VmName `
	--memory $MemoryMb `
	--cpus $Cpus `
	--firmware bios `
	--rtcuseutc on `
	--mouse usbtablet `
	--audio-driver dsound `
	--audiocontroller hda `
	--nic1 bridged `
	--bridgeadapter1 $bridge `
	--nictype1 82540EM `
	--cableconnected1 on `
	--graphicscontroller vboxvga `
	--vram 16 `
	--ioapic on `
	--pae off `
	--clipboard-mode disabled `
	--draganddrop disabled `
	--uart1 "0x3F8" 4 `
	--uartmode1 server "\\.\pipe\R5OS-clip"

Detach-IdeDisk $vbox $VmName
& $vbox storageattach $VmName --storagectl IDE --port 0 --device 0 --type hdd --medium $vdi
if ($LASTEXITCODE -ne 0) {
	throw "storageattach failed"
}

Write-Host "Starting $VmName ..."
& $vbox startvm $VmName

if (-not $NoClip) {
	Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe'" -ErrorAction SilentlyContinue |
		Where-Object { $_.CommandLine -like "*vbox-clipboard-host.ps1*" } |
		ForEach-Object {
			Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
		}
	$hostClip = Join-Path $PSScriptRoot "vbox-clipboard-host.ps1"
	Start-Process -FilePath "powershell.exe" -WindowStyle Hidden -ArgumentList @(
		"-NoProfile", "-STA", "-ExecutionPolicy", "Bypass",
		"-File", $hostClip
	) | Out-Null
	Write-Host "  clipboard host: \\.\pipe\R5OS-clip"
}

Write-Host "LAN IPv6/mDNS can work here. QEMU usernet cannot."
