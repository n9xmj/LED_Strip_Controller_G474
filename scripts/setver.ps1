# setver.ps1 -- set or bump FIRMWARE_VERSION in App/Inc/platform.h

param(
    [Parameter(Position=0)]
    [string]$Version
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
$platformH = Join-Path $repoRoot "App\Inc\platform.h"

if (-not (Test-Path $platformH)) {
    Write-Error "platform.h not found at $platformH"
    exit 1
}

$content = Get-Content $platformH -Raw

# Extract current
if ($content -match '(?m)^#define\s+FIRMWARE_VERSION\s+"([^"]+)"') {
    $current = $matches[1]
} else {
    Write-Error "Could not find #define FIRMWARE_VERSION in platform.h"
    exit 1
}

if ($Version) {
    # Add quotes if not present
    if (-not ($Version -match '^".*"$')) {
        $Version = '"' + $Version.Trim('"') + '"'
    }
    $new = $Version
} else {
    # Bump least significant number
    if ($current -match '^(\d+\.\d+\.)(\d+)$') {
        $prefix = $matches[1]
        $patch = [int]$matches[2] + 1
        $new = '"' + $prefix + $patch + '"'
    } else {
        Write-Error "Cannot parse current FIRMWARE_VERSION '$current' for bumping (expected X.Y.Z format)"
        exit 1
    }
}

# Replace the define line, preserving original formatting style
$newContent = $content -replace '(?m)^#define\s+FIRMWARE_VERSION\s+"[^"]+"', "#define FIRMWARE_VERSION $new"

Set-Content -Path $platformH -Value $newContent -NoNewline

Write-Host "FIRMWARE_VERSION changed from $current to $($new.Trim('"')) in App/Inc/platform.h" -ForegroundColor Green
