# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Build standalone r5-befs.exe with PyInstaller.
# Without arguments, r5-befs.exe opens the GUI reader.
# With arguments, it runs the r5_befs command-line interface.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

Write-Host "Building r5-befs.exe in $ScriptDir ..."

# Verify PyInstaller is installed
try {
    py -m PyInstaller --version | Out-Null
} catch {
    Write-Error "PyInstaller is not installed. Install with: py -m pip install pyinstaller"
}

# Run PyInstaller
py -m PyInstaller `
    --onefile `
    --console `
    --name r5-befs `
    --distpath "$ScriptDir" `
    --workpath "$ScriptDir\build" `
    --specpath "$ScriptDir" `
    --clean `
    entry.py

if ($LASTEXITCODE -ne 0) {
    Write-Error "PyInstaller build failed with exit code $LASTEXITCODE"
}

$ExePath = Join-Path $ScriptDir "r5-befs.exe"
if (Test-Path $ExePath) {
    $Size = (Get-Item $ExePath).Length
    Write-Host "Build successful: $ExePath ($([math]::Round($Size / 1MB, 2)) MB)"
} else {
    Write-Error "Expected binary $ExePath was not found."
}
