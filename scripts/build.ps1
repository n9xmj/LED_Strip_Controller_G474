# build.ps1 -- headless STM32CubeIDE build for LED_Strip_Controller_G474
#
# Usage (agent or human friendly):
#   scripts\build.ps1                    # Debug (default, clean build)
#   scripts\build.ps1 Release
#   scripts\build.ps1 --config Test
#   scripts\build.ps1 Debug --clean
#   scripts\build.ps1 Debug --incremental
#
# Configs: Debug (default), Release, Test
# Use --clean to explicitly force a clean build (default behavior).
# Use --incremental to request an incremental build instead of clean.
# Locates stm32cubeidec.exe via $env:STM32CUBEIDE or common paths.
# Uses temp workspace + lock handling (from reference patterns).

if ($args -match '^-?-h(elp)?$') {
    @"
Usage: scripts\build.ps1 [CONFIG] [OPTIONS]

  CONFIG                 Debug (default), Release, or Test

Options:
  --clean                Force a clean build (default behavior)
  --incremental          Request an incremental build
  --bump-build-count     Increment BUILD_NUMBER in App/Inc/platform.h before the build (for cleanbuild skill)
  --help, -h             Show this help

Locates stm32cubeidec.exe via `$env:STM32CUBEIDE or common install paths.
Uses a dedicated temp workspace for headless builds.
"@
    exit 0
}

# Robust manual argument parsing (consistent with smoke-test.ps1 / flash.ps1).
# Avoids PowerShell param() binding quirks with -- options and ValidateSet.
$ErrorActionPreference = "Stop"

$projectName = "LED_Strip_Controller_G474"
$repoRoot    = Split-Path $PSScriptRoot -Parent

$Config = "Debug"
$Clean = $false
$Incremental = $false
$BumpBuildCount = $false
$Help = $false   # handled above

# First positional (non-flag) arg is CONFIG if valid
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
        '^--?clean$'           { $Clean = $true; break }
        '^--?incremental$'     { $Incremental = $true; break }
        '^--?bump-build-count$' { $BumpBuildCount = $true; break }
        '^--?config$'          { $Config = $args[++$i]; break }
        default { }
    }
}

# Normalize Config (ValidateSet equivalent)
if ($Config -notin @("Debug","Release","Test")) {
    Write-Host "Invalid config '$Config', defaulting to Debug" -ForegroundColor Yellow
    $Config = "Debug"
}

if ($BumpBuildCount) {
    $platformH = Join-Path $repoRoot "App\Inc\platform.h"
    if (Test-Path $platformH) {
        $content = Get-Content $platformH -Raw
        if ($content -match '(?m)^#define\s+BUILD_NUMBER\s+"(\d+)"') {
            $current = $matches[1]
            $new = [int]$current + 1
            $newContent = $content -replace '(?m)^#define\s+BUILD_NUMBER\s+"\d+"', "#define BUILD_NUMBER `"$new`""
            Set-Content -Path $platformH -Value $newContent -NoNewline
            Write-Host "BUILD_NUMBER bumped from $current to $new in App/Inc/platform.h (due to --bump-build-count)" -ForegroundColor Yellow
        } else {
            Write-Host "Could not find BUILD_NUMBER define to bump in $platformH" -ForegroundColor Yellow
        }
    }
}

function Find-LatestCubeIde {
    $roots = @(
        "C:\ST",
        "C:\STM32",
        "${env:ProgramFiles}\STMicroelectronics",
        "${env:ProgramFiles(x86)}\STMicroelectronics"
    )
    $hits = foreach ($root in $roots) {
        if (-not $root -or -not (Test-Path $root)) { continue }
        Get-ChildItem -Path $root -Directory -Filter "STM32CubeIDE_*" -ErrorAction SilentlyContinue
    }
    $fromVer = $hits |
        ForEach-Object {
            $exe = Join-Path $_.FullName "STM32CubeIDE\stm32cubeidec.exe"
            if (Test-Path $exe) {
                $ver = try { [version]($_.Name -replace '^STM32CubeIDE_','') } catch { [version]'0.0.0' }
                [pscustomobject]@{ Version = $ver; Path = $exe }
            }
        } |
        Sort-Object Version -Descending |
        Select-Object -First 1 -ExpandProperty Path
    if ($fromVer) { return $fromVer }

    foreach ($p in @(
            "C:\STM32\STM32CubeIDE\STM32CubeIDE\stm32cubeidec.exe",
            "C:\STM32\STM32CubeIDE\stm32cubeidec.exe"
        )) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

if ($env:STM32CUBEIDE -and (Test-Path $env:STM32CUBEIDE)) {
    $cubeide = $env:STM32CUBEIDE
} else {
    $cubeide = Find-LatestCubeIde
}
if (-not $cubeide -or -not (Test-Path $cubeide)) {
    Write-Error @"
STM32CubeIDE not found. Set the full path to the launcher:
  `$env:STM32CUBEIDE = 'C:\STM32\STM32CubeIDE\STM32CubeIDE\stm32cubeidec.exe'
"@
    exit 3
}

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$workspace = Join-Path $env:TEMP "led-g474-headless-ws-$ts"
if (-not (Test-Path $workspace)) { New-Item -ItemType Directory -Path $workspace | Out-Null }

$lockFile = Join-Path $workspace ".metadata\.lock"
if (Test-Path $lockFile) {
    for ($i = 0; $i -lt 5; $i++) {
        Remove-Item $lockFile -Force -ErrorAction SilentlyContinue
        if (-not (Test-Path $lockFile)) { break }
        Start-Sleep -Milliseconds (300 * ($i + 1))
    }
}

Write-Host "=== LED_Strip_Controller_G474 build ===" -ForegroundColor Cyan
Write-Host "Project:   $projectName"
Write-Host "Config:    $Config"
Write-Host "CubeIDE:   $cubeide"
Write-Host "Workspace: $workspace"
if ($Incremental) {
    Write-Host "Mode:      incremental (-build)" -ForegroundColor Yellow
} elseif ($Clean) {
    Write-Host "Mode:      clean (-cleanBuild)"
} else {
    Write-Host "Mode:      clean (-cleanBuild)  [default]"
}
Write-Host ""

$buildVerb = "-cleanBuild"
if ($Incremental) {
    $buildVerb = "-build"
} elseif ($Clean) {
    $buildVerb = "-cleanBuild"
}

& $cubeide `
    -nosplash `
    -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
    -data $workspace `
    -import "$repoRoot" `
    $buildVerb "$projectName/$Config" `
    -consoleLog

$rc = $LASTEXITCODE
if ($rc -eq 0) {
    Write-Host "Build OK. Artifacts in $Config\" -ForegroundColor Green

    $stdElf = Join-Path $repoRoot "$Config\${projectName}.elf"
    if (Test-Path $stdElf) {
        $elfTime = (Get-Item $stdElf).LastWriteTime
        Write-Host "ELF timestamp (filesystem): $elfTime"
    }

    # Clean up the temporary workspace to avoid clutter
    if (Test-Path $workspace) {
        Remove-Item $workspace -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Cleaned up temporary workspace: $workspace"
    }
} else {
    Write-Error "Build failed (exit $rc)."
    # On failure, leave the workspace so the user (or an agent) can inspect Eclipse logs if needed
    Write-Host "Temporary workspace left for inspection: $workspace" -ForegroundColor Yellow
}
exit $rc
