param(
    [Parameter(Position = 0)]
    [string]$Port = $env:PORT,
    [double]$SecondsToCapture = $(if ($env:SECONDS_TO_CAPTURE) { [double]$env:SECONDS_TO_CAPTURE } else { 12 }),
    [string]$SdkPath = $env:SIFLI_SDK_PATH,
    [string]$ToolsPath = $env:SIFLI_SDK_TOOLS_PATH
)

$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $Port) {
    throw "Missing serial port. Pass COM7, 7, or set PORT."
}
if ($Port -match '^\d+$') {
    $Port = "COM$Port"
}
if (-not $SdkPath) {
    $SdkPath = Join-Path (Split-Path $RootDir -Parent) "sifli-sdk"
}
if (-not $ToolsPath) {
    $ToolsPath = Join-Path $RootDir ".sifli-tools"
}

$ExportScript = Join-Path $SdkPath "export.ps1"
if (-not (Test-Path -LiteralPath $ExportScript)) {
    throw "Missing SDK export script: $ExportScript. Set SIFLI_SDK_PATH or pass -SdkPath."
}

$env:SIFLI_SDK_TOOLS_PATH = $ToolsPath
. $ExportScript

$MonitorCode = @'
import serial
import sys
import time

port = sys.argv[1]
seconds = float(sys.argv[2])

ser = serial.Serial(port, baudrate=1000000, timeout=0.05)
ser.rts = True
time.sleep(0.25)
ser.rts = False
time.sleep(0.25)
ser.timeout = 0.2

start = time.time()
total = b""
while time.time() - start < seconds:
    data = ser.read(4096)
    if data:
        total += data
        sys.stdout.buffer.write(data)
        sys.stdout.flush()
ser.close()

if not total:
    print("<no data>")
'@

python -c $MonitorCode $Port $SecondsToCapture
