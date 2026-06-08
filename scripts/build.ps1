param(
    [string]$Board = $env:BOARD,
    [int]$Jobs = $(if ($env:JOBS) { [int]$env:JOBS } else { 8 }),
    [string]$SdkPath = $env:SIFLI_SDK_PATH,
    [string]$ToolsPath = $env:SIFLI_SDK_TOOLS_PATH
)

$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $Board) {
    $Board = "sf32lb52-lchspi-ulp"
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

Push-Location (Join-Path $RootDir "project")
try {
    scons "--board=$Board" "-j$Jobs"
}
finally {
    Pop-Location
}
