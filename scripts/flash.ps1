param(
    [Parameter(Position = 0)]
    [string]$Port = $env:PORT,
    [string]$Board = $env:BOARD,
    [string]$SdkPath = $env:SIFLI_SDK_PATH,
    [string]$ToolsPath = $env:SIFLI_SDK_TOOLS_PATH
)

$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $Board) {
    $Board = "sf32lb52-lchspi-ulp"
}
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

$BuildDir = Join-Path $RootDir "project\build_${Board}_hcpu"
$Bootloader = Join-Path $BuildDir "bootloader\bootloader.bin"
$Main = Join-Path $BuildDir "main.bin"
$Ftab = Join-Path $BuildDir "ftab\ftab.bin"

foreach ($Path in @($Bootloader, $Main, $Ftab)) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing build artifact: $Path. Run scripts\build.ps1 first."
    }
}

$ExportScript = Join-Path $SdkPath "export.ps1"
if (-not (Test-Path -LiteralPath $ExportScript)) {
    throw "Missing SDK export script: $ExportScript. Set SIFLI_SDK_PATH or pass -SdkPath."
}

$env:SIFLI_SDK_TOOLS_PATH = $ToolsPath
. $ExportScript

sftool -p $Port -c SF32LB52 -m nor write_flash `
    "$Bootloader@0x12010000" `
    "$Main@0x12020000" `
    "$Ftab@0x12000000"
