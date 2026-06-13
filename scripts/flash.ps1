# flash.ps1 -- flash LED_Strip_Controller_G474 via STM32_Programmer_CLI
#
# Usage:
#   scripts\flash.ps1                    # flashes Debug build (auto ST-Link)
#   scripts\flash.ps1 Release --stlink-sn 066AFF...
#   scripts\flash.ps1 --port COM15       # (port mainly for future / smoke)
#   scripts\flash.ps1 --list
#
# Supports auto-selection when --stlink-sn / --port omitted (simple benches).
# Use --list for discovery on multi-probe / multi-VCP setups (e.g. STLINK-V3SET).

if ($args -match '^-?-h(elp)?$') {
    Write-Host @'
Usage: scripts\flash.ps1 [CONFIG] [OPTIONS]

  CONFIG                 Debug (default), Release, or Test

Options:
  --stlink-sn SN         Use specific ST-Link serial number (auto-select if omitted)
  --port PORT            Use specific COM port for any serial needs (auto if omitted)
  --list                 List available ST-Links and COM ports with accessibility info
  --help, -h             Show this help

Uses STM32_Programmer_CLI (prefers $env:STM32_PROGRAMMER_CLI).
'@
    exit 0
}

# Robust manual argument parsing (consistent with smoke-test.ps1).
# Avoids all PowerShell advanced param() binding quirks with --long-options,
# ValidateSet, mixed config + flags, etc.
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
$projectName = "LED_Strip_Controller_G474"

$Config = "Debug"
$StlinkSn = $null
$Port = $null
$List = $false

# First positional arg (if present and not a flag) is treated as CONFIG
$argIndex = 0
if ($args.Count -gt 0 -and $args[0] -notmatch '^-') {
    $candidate = $args[0]
    if ($candidate -in @("Debug","Release","Test")) {
        $Config = $candidate
        $argIndex = 1
    }
}

for ($i = $argIndex; $i -lt $args.Count; $i++) {
    switch -Regex ($args[$i]) {
        '^--?stlink-sn$' { $StlinkSn = $args[++$i]; break }
        '^--?port$'      { $Port = $args[++$i]; break }
        '^--?list$'      { $List = $true; break }
        default { }
    }
}

$programmer = $env:STM32_PROGRAMMER_CLI
if (-not $programmer -or -not (Test-Path $programmer)) {
    $programmer = "C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
    if (-not (Test-Path $programmer)) {
        Write-Error "STM32_Programmer_CLI not found. Set $env:STM32_PROGRAMMER_CLI or install STM32CubeProgrammer."
        exit 3
    }
}

if ($List) {
    python "$PSScriptRoot\discover.py" --list
    exit 0
}

if (-not $StlinkSn) {
    $StlinkSn = (python "$PSScriptRoot\discover.py" --default-stlink 2>$null).Trim()
    if (-not $StlinkSn) {
        Write-Error @"
No ST-Link selected. Multiple probes may be connected.
  scripts\discover.py --list          # see probes + bench defaults
  scripts\flash.ps1 --stlink-sn SN    # explicit SN
  scripts\bench.defaults.json         # set stlink_sn for this bench
"@
        exit 3
    }
}

$artifact = Join-Path $repoRoot "$Config\${projectName}.elf"
if (-not (Test-Path $artifact)) {
    $artifact = Join-Path $repoRoot "${projectName}.elf"
}
if (-not (Test-Path $artifact)) {
    Write-Error "Artifact not found: $artifact (build first)"
    exit 2
}

$connect = "port=SWD"
if ($StlinkSn) {
    $connect += " sn=$StlinkSn"
}

Write-Host "=== LED_Strip_Controller_G474 flash ($Config) ===" -ForegroundColor Cyan
Write-Host "Artifact:  $artifact"
Write-Host "ST-Link:   $(if ($StlinkSn) { $StlinkSn } else { '(auto)' })"
Write-Host "Programmer: $programmer"
Write-Host ""

& $programmer -c $connect -w $artifact -v -rst
$rc = $LASTEXITCODE
if ($rc -eq 0) {
    Write-Host "Flash OK" -ForegroundColor Green
} else {
    Write-Error "Flash failed (exit $rc)"
}
exit $rc
