param([string]$ArmGccBin = $env:ARM_GCC_BIN)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$venvScripts = Join-Path $projectRoot '.venv\Scripts'
if (Test-Path $venvScripts) { $env:PATH = $venvScripts + ';' + $env:PATH }
if ($ArmGccBin) { $env:ARM_GCC_BIN = $ArmGccBin }
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakeExe = $cmakeCommand.Source }
else { throw 'Install CMake and Ninja and put them on PATH.' }
Push-Location -LiteralPath $projectRoot
try {
    & $cmakeExe --preset release
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    & $cmakeExe --build --preset release
    if ($LASTEXITCODE) { throw 'Firmware build failed.' }
} finally { Pop-Location }
