# Creates a virtual environment (if missing) and installs arstream-server into it.
# Usage: .\install.ps1 [-Dev]
param(
    [switch]$Dev
)
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ServerDir = Split-Path -Parent $ScriptDir
$VenvDir = if ($env:ARSTREAM_VENV) { $env:ARSTREAM_VENV } else { Join-Path $ServerDir ".venv" }

if (-not (Test-Path $VenvDir)) {
    Write-Host "Creating virtual environment at $VenvDir..."
    python -m venv $VenvDir
}

$Target = $ServerDir
if ($Dev) {
    $Target = "$ServerDir[dev]"
}

$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
& $VenvPython -m pip install --upgrade pip
& $VenvPython -m pip install -e $Target

Write-Host ""
Write-Host "Done. Activate the environment with:"
Write-Host "  $VenvDir\Scripts\Activate.ps1"
Write-Host "Then run:"
Write-Host "  arstream-server --help"
