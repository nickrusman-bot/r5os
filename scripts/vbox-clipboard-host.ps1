# Text clipboard bridge for R5OS in VirtualBox (COM1 named pipe).
# Run STA. VirtualBox creates the pipe; this process is the client.
param(
	[string]$PipeName = "R5OS-clip"
)

if ([Threading.Thread]::CurrentThread.GetApartmentState() -ne "STA") {
	& powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File $PSCommandPath @args
	exit $LASTEXITCODE
}

Add-Type -AssemblyName System.Windows.Forms | Out-Null

$ErrorActionPreference = "Continue"
$utf8 = New-Object System.Text.UTF8Encoding $false
$maxBytes = 256 * 1024
$magic = [byte[]][char[]]@("R", "5", "C", "1")
$lastSent = $null
$lastRecv = $null

function Get-ClipText {
	try {
		if ([System.Windows.Forms.Clipboard]::ContainsText()) {
			return [System.Windows.Forms.Clipboard]::GetText()
		}
	} catch {}
	return ""
}

function Set-ClipText([string]$text) {
	try {
		if ($null -eq $text -or $text.Length -eq 0) {
			return
		}
		[System.Windows.Forms.Clipboard]::SetText($text)
	} catch {}
}

function Write-Frame([IO.Stream]$stream, [string]$text) {
	$bytes = $utf8.GetBytes($text)
	if ($bytes.Length -gt $maxBytes) {
		$bytes = $bytes[0..($maxBytes - 1)]
	}
	$len = [BitConverter]::GetBytes([uint32]$bytes.Length)
	$stream.Write($magic, 0, 4)
	$stream.Write($len, 0, 4)
	if ($bytes.Length -gt 0) {
		$stream.Write($bytes, 0, $bytes.Length)
	}
	$stream.Flush()
}

function Read-Exact([IO.Stream]$stream, [byte[]]$buf, [int]$n) {
	$off = 0
	while ($off -lt $n) {
		$got = $stream.Read($buf, $off, $n - $off)
		if ($got -le 0) {
			return $false
		}
		$off += $got
	}
	return $true
}

$pipe = $null
for ($i = 0; $i -lt 90; $i++) {
	try {
		$pipe = New-Object IO.Pipes.NamedPipeClientStream(
			".", $PipeName, [IO.Pipes.PipeDirection]::InOut)
		$pipe.Connect(1000)
		break
	} catch {
		if ($pipe) {
			$pipe.Dispose()
			$pipe = $null
		}
		Start-Sleep -Seconds 1
	}
}
if (-not $pipe -or -not $pipe.IsConnected) {
	exit 1
}

$header = New-Object byte[] 8
$payload = New-Object byte[] $maxBytes

try {
	while ($pipe.IsConnected) {
		$text = Get-ClipText
		if ($text -and $text -ne $lastSent -and $text -ne $lastRecv) {
			$lastSent = $text
			Write-Frame $pipe $text
		}

		if ($pipe.CanRead) {
			$pipe.ReadTimeout = 250
			try {
				if (Read-Exact $pipe $header 8) {
					$ok = $true
					for ($m = 0; $m -lt 4; $m++) {
						if ($header[$m] -ne $magic[$m]) { $ok = $false }
					}
					$n = [BitConverter]::ToUInt32($header, 4)
					if ($ok -and $n -le $maxBytes) {
						$gotPayload = $true
						if ($n -gt 0) {
							$gotPayload = Read-Exact $pipe $payload $n
						}
						if ($gotPayload) {
							$incoming = $utf8.GetString($payload, 0, $n)
							if ($incoming -and $incoming -ne $lastSent -and $incoming -ne $lastRecv) {
								$lastRecv = $incoming
								Set-ClipText $incoming
							}
						}
					}
				}
			} catch [TimeoutException] {
			} catch [IO.IOException] {
				break
			}
		}
		Start-Sleep -Milliseconds 50
	}
} finally {
	if ($pipe) { $pipe.Dispose() }
}
