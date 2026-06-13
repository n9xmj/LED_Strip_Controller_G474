# Run PLAY bench scenarios using scripts/bench.defaults.json
param(
    [string]$Scenario = "P0",
    [switch]$Reset
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$args = @("scripts/play_scenarios.py", "--scenario", $Scenario)
if ($Reset) { $args += "--reset" }

python @args
exit $LASTEXITCODE
