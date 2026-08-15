# Stops the arstream-server instance started by start.ps1.
# Env overrides: ARSTREAM_PID_FILE
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ServerDir = Split-Path -Parent $ScriptDir
$PidFile = if ($env:ARSTREAM_PID_FILE) { $env:ARSTREAM_PID_FILE } else { Join-Path $ServerDir "arstream-server.pid" }

if (-not (Test-Path $PidFile)) {
    Write-Error "No PID file found ($PidFile) -- is arstream-server running?"
    exit 1
}

$targetPid = Get-Content $PidFile
$proc = Get-Process -Id $targetPid -ErrorAction SilentlyContinue
if (-not $proc) {
    Write-Warning "Process $targetPid is not running; removing stale PID file."
    Remove-Item $PidFile
    exit 1
}

Stop-Process -Id $targetPid
for ($i = 0; $i -lt 20; $i++) {
    if (-not (Get-Process -Id $targetPid -ErrorAction SilentlyContinue)) { break }
    Start-Sleep -Milliseconds 500
}

if (Get-Process -Id $targetPid -ErrorAction SilentlyContinue) {
    Write-Warning "arstream-server (PID $targetPid) did not stop in time, forcing."
    Stop-Process -Id $targetPid -Force
}

Remove-Item $PidFile
Write-Host "arstream-server stopped."
