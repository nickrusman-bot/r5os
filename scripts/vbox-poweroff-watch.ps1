# The guest can enter ACPI S5 while VirtualBox stays in POWERING_OFF.
# On this host that happened with the NIC bridged to the MediaTek MT7921.
# The window keeps the last frame ("Shutting down..."). If the VM is still
# running 20 seconds after that log line, end its VirtualBoxVM process.
param(
	[string]$VmName = "R5OS",
	[int]$GraceSeconds = 20
)

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
	exit 1
}

function Stop-R5VmProcess([string]$name) {
	Get-CimInstance Win32_Process -Filter "Name = 'VirtualBoxVM.exe'" -ErrorAction SilentlyContinue |
		Where-Object { $_.CommandLine -and ($_.CommandLine -like "*--comment $name*" -or $_.CommandLine -like "*startvm $name*") } |
		ForEach-Object {
			Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
		}
}

$vbox = Find-VBoxManage
$log = Join-Path $env:USERPROFILE "VirtualBox VMs\$VmName\Logs\VBox.log"
$arm = $null

while ($true) {
	Start-Sleep -Seconds 2
	$info = & $vbox showvminfo $VmName --machinereadable 2>$null
	if ($LASTEXITCODE -ne 0) {
		exit 0
	}
	$stateLine = $info | Where-Object { $_ -like "VMState=*" } | Select-Object -First 1
	if ($stateLine -match '"(poweroff|aborted|saved)"') {
		exit 0
	}

	$hit = $false
	if (Test-Path $log) {
		try {
			$hit = Select-String -Path $log -Pattern "Entering S5 power state" -Quiet -ErrorAction Stop
		} catch {
			$hit = $false
		}
	}
	if ($hit -and -not $arm) {
		$arm = (Get-Date).AddSeconds($GraceSeconds)
	}
	if ($arm -and (Get-Date) -ge $arm -and $stateLine -match '"(running|stopping|poweringoff)"') {
		Stop-R5VmProcess $VmName
		exit 0
	}
}
