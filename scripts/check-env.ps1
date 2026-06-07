# check-env.ps1 -- validate tools for LED_Strip_Controller_G474 automation

if ($args -match '^-?-h(elp)?$') {
    @"
Usage: scripts\check-env.ps1 [OPTIONS]

Options:
  --help, -h    Show this help

Validates presence of STM32CUBEIDE and STM32_PROGRAMMER_CLI (stock env vars preferred).
"@
    exit 0
}

Write-Host "=== LED_Strip_Controller_G474 env check ===" -ForegroundColor Cyan

# CubeIDE
$ide = $env:STM32CUBEIDE
if (-not $ide) {
    $ide = "C:\STM32\STM32CubeIDE\STM32CubeIDE\stm32cubeidec.exe"
}
if (Test-Path $ide) {
    Write-Host "STM32CUBEIDE: $ide" -ForegroundColor Green
} else {
    Write-Host "STM32CUBEIDE: NOT FOUND (set env or install)" -ForegroundColor Red
}

# Programmer
$prog = $env:STM32_PROGRAMMER_CLI
if (-not $prog) {
    $prog = "C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
}
if (Test-Path $prog) {
    Write-Host "STM32_PROGRAMMER_CLI: $prog" -ForegroundColor Green
    $binDir = Split-Path $prog -Parent
    Write-Host "  (derived bin dir: $binDir)"
} else {
    Write-Host "STM32_PROGRAMMER_CLI: NOT FOUND" -ForegroundColor Red
}

# Discovery
if (Test-Path "$PSScriptRoot\discover.py") {
    Write-Host "discover.py: present" -ForegroundColor Green
    python "$PSScriptRoot\discover.py" --list | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  (discovery runnable)" -ForegroundColor Green
    }
} else {
    Write-Host "discover.py: missing" -ForegroundColor Yellow
}

Write-Host "Check complete."
