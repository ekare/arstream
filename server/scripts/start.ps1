# Starts arstream-server in the background and records its PID.
# Prefers the venv created by install.ps1, falls back to PATH.
# Usage: .\start.ps1 [extra arstream-server args...]
# Env overrides: ARSTREAM_VENV, ARSTREAM_PID_FILE, ARSTREAM_LOG_FILE
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ServerDir = Split-Path -Parent $ScriptDir
$VenvDir = if ($env:ARSTREAM_VENV) { $env:ARSTREAM_VENV } else { Join-Path $ServerDir ".venv" }
$PidFile = if ($env:ARSTREAM_PID_FILE) { $env:ARSTREAM_PID_FILE } else { Join-Path $ServerDir "arstream-server.pid" }
$LogFile = if ($env:ARSTREAM_LOG_FILE) { $env:ARSTREAM_LOG_FILE } else { Join-Path $ServerDir "arstream-server.log" }

$VenvBin = Join-Path $VenvDir "Scripts\arstream-server.exe"
$Bin = if (Test-Path $VenvBin) { $VenvBin } else { "arstream-server" }

if (Test-Path $PidFile) {
    $existingPid = Get-Content $PidFile
    if (Get-Process -Id $existingPid -ErrorAction SilentlyContinue) {
        Write-Error "arstream-server is already running (PID $existingPid)."
        exit 1
    }
}

$proc = Start-Process -FilePath $Bin -ArgumentList $args -WorkingDirectory $ServerDir `
    -RedirectStandardOutput $LogFile -RedirectStandardError "$LogFile.err" -PassThru -WindowStyle Hidden
$proc.Id | Out-File -FilePath $PidFile -Encoding ascii -NoNewline

Start-Sleep -Seconds 1
if (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue) {
    Write-Host "arstream-server started (PID $($proc.Id)). Logs: $LogFile"
} else {
    Write-Error "arstream-server failed to start -- check $LogFile"
    Remove-Item $PidFile -ErrorAction SilentlyContinue
    exit 1
}
