# memreport.ps1 -- concise RAM/FLASH report from the GCC .map file
#
# Usage:
#   scripts\memreport.ps1
#   scripts\memreport.ps1 --config Release
#   scripts\memreport.ps1 --map "Debug\LED_Strip_Controller_G474.map"
#
# Called by skills: /memused, /memory, /memreport (.claude/skills/memused; .grok/skills/memreport)
# Parses "Memory Configuration" and "Linker script and memory map" for Build Analyzer-style totals.

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path $PSScriptRoot -Parent
$projectName = "LED_Strip_Controller_G474"
$buildConfigs = @("Debug", "Release", "Test")

$MapPath = $null
$explicitMap = $false
$configFilter = $null

for ($i = 0; $i -lt $args.Count; $i++) {
    switch -Regex ($args[$i]) {
        '^--?map$' {
            if ($i + 1 -lt $args.Count) {
                $MapPath = $args[++$i]
                $explicitMap = $true
            }
            break
        }
        '^--?config$' {
            if ($i + 1 -lt $args.Count) {
                $configFilter = $args[++$i]
            }
            break
        }
        default { }
    }
}

function Resolve-MapPath {
    param(
        [string]$PreferredConfig
    )
    $candidates = @()
    $configs = if ($PreferredConfig) { @($PreferredConfig) } else { $buildConfigs }
    foreach ($cfg in $configs) {
        $p = Join-Path $repoRoot "$cfg\$projectName.map"
        if (Test-Path $p) {
            $candidates += Get-Item $p
        }
    }
    if ($candidates.Count -eq 0) {
        return $null
    }
    return ($candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}

if (-not $explicitMap) {
    $MapPath = Resolve-MapPath -PreferredConfig $configFilter
} elseif (-not [System.IO.Path]::IsPathRooted($MapPath)) {
    $MapPath = Join-Path $repoRoot $MapPath
}

if (-not $MapPath -or -not (Test-Path $MapPath)) {
    Write-Error "No .map file found. Run a build first (/build or /roundtrip). Searched Debug, Release, Test for ${projectName}.map"
    exit 2
}

$mapLines = Get-Content $MapPath
$buildConfig = Split-Path (Split-Path $MapPath -Parent) -Leaf

Write-Host "=== Memory usage ($projectName) ===" -ForegroundColor Cyan
Write-Host "Map file : $MapPath"
Write-Host "Config   : $buildConfig"
Write-Host "Built    : $((Get-Item $MapPath).LastWriteTime)"
Write-Host ""

# 1. Parse Memory Configuration for total sizes
$memConfig = @{}
$inMemConfig = $false
foreach ($line in $mapLines) {
    if ($line -match 'Memory Configuration') { $inMemConfig = $true; continue }
    if ($inMemConfig -and $line -match '^\s*(\S+)\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)\s+(\S+)') {
        $name = $matches[1]
        $lengthHex = $matches[2]
        $attrs = $matches[3]
        $length = [Convert]::ToInt64($lengthHex, 16)
        $memConfig[$name] = @{ Length = $length; Attrs = $attrs }
    }
    if ($inMemConfig -and $line -match '^\s*\*default\*') { $inMemConfig = $false }
}

$flashTotal = if ($memConfig.ContainsKey('FLASH')) { $memConfig['FLASH'].Length } else { 0 }
$ramTotal   = if ($memConfig.ContainsKey('RAM'))   { $memConfig['RAM'].Length }   else { 0 }

Write-Host "Memory Regions (from map):"
if ($flashTotal -gt 0) {
    Write-Host ("  FLASH : {0,8} KB  (0x{1:X8})  {2}" -f ($flashTotal / 1KB), $flashTotal, $memConfig['FLASH'].Attrs)
}
if ($ramTotal -gt 0) {
    Write-Host ("  RAM   : {0,8} KB  (0x{1:X8})  {2}" -f ($ramTotal / 1KB), $ramTotal, $memConfig['RAM'].Attrs)
}
Write-Host ""

# 2. Parse the main Linker script and memory map for section sizes
# We look for the primary section definition lines with real VMA + size.
# Pattern:   .sectionname     0x<addr>      0x<size>
# We only take the first (defining) line for each top-level section.

$sections = @{}
$inMap = $false
$mapStartLine = -1

for ($i = 0; $i -lt $mapLines.Count; $i++) {
    $line = $mapLines[$i]

    if ($line -match 'Linker script and memory map') {
        $inMap = $true
        $mapStartLine = $i
        continue
    }
    if ($inMap -and ($line -match 'Cross Reference Table|Allocating common symbols|Discarded input sections')) {
        break
    }

    if ($inMap -and $line -match '^\s*(\.[a-zA-Z0-9_.]+)\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)') {
        $secName = $matches[1]
        $sizeHex = $matches[2]
        $size = [Convert]::ToInt64($sizeHex, 16)

        # Only record the first (authoritative) size for this top-level section
        if (-not $sections.ContainsKey($secName) -and $size -gt 0) {
            $sections[$secName] = $size
        }
    }
}

# Also capture the Min_Heap / Min_Stack symbols that are common in STM32 linker scripts
$minHeap = 0
$minStack = 0
foreach ($line in $mapLines) {
    if ($line -match '^\s*_Min_Heap_Size\s*=\s*0x([0-9a-fA-F]+)') {
        $minHeap = [Convert]::ToInt64($matches[1], 16)
    }
    if ($line -match '^\s*_Min_Stack_Size\s*=\s*0x([0-9a-fA-F]+)') {
        $minStack = [Convert]::ToInt64($matches[1], 16)
    }
}

# 3. Compute totals before summary table
$flashSections = @('.isr_vector', '.text', '.rodata', '.data', '.ARM.exidx', '.ARM.extab', '.ARM.attributes')
$ramSections   = @('.data', '.bss', '.heap', '.stack', '._user_heap_stack')

$flashUsed = 0
foreach ($sec in $flashSections) {
    if ($sections.ContainsKey($sec)) {
        $flashUsed += $sections[$sec]
    }
}

$staticRam = 0
if ($sections.ContainsKey('.data')) { $staticRam += $sections['.data'] }
if ($sections.ContainsKey('.bss')) { $staticRam += $sections['.bss'] }
$totalReservedRam = $staticRam + $minHeap + $minStack
$flashFree = [Math]::Max(0, $flashTotal - $flashUsed)
$ramFree = [Math]::Max(0, $ramTotal - $totalReservedRam)
$flashPct = if ($flashTotal -gt 0) { ($flashUsed / $flashTotal) * 100 } else { 0 }
$ramPct = if ($ramTotal -gt 0) { ($totalReservedRam / $ramTotal) * 100 } else { 0 }

Write-Host "=== Summary (Build Analyzer style) ===" -ForegroundColor Cyan
Write-Host ("{0,-8} {1,10} {2,10} {3,10} {4,8}" -f "Region", "Total KB", "Used KB", "Free KB", "Used %")
Write-Host ("{0,-8} {1,10:N1} {2,10:N1} {3,10:N1} {4,7:N1}%" -f "FLASH", ($flashTotal / 1KB), ($flashUsed / 1KB), ($flashFree / 1KB), $flashPct)
Write-Host ("{0,-8} {1,10:N1} {2,10:N1} {3,10:N1} {4,7:N1}%" -f "RAM", ($ramTotal / 1KB), ($totalReservedRam / 1KB), ($ramFree / 1KB), $ramPct)
Write-Host ""
Write-Host "RAM used = .data + .bss + _Min_Heap_Size + _Min_Stack_Size (linker reservations)."
Write-Host ""

Write-Host "=== Detail ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Major Sections (itemized from map):"
Write-Host ""

$ramStatic = 0   # .data + .bss at runtime (detail only)

Write-Host "FLASH consumers (stored in Flash):" -ForegroundColor Yellow
$flashList = @()
foreach ($sec in $flashSections) {
    if ($sections.ContainsKey($sec)) {
        $sz = $sections[$sec]
        $flashList += [PSCustomObject]@{ Section = $sec; Size = $sz }
    }
}
# Sort descending
$flashList | Sort-Object Size -Descending | ForEach-Object {
    $pct = if ($flashTotal -gt 0) { ($_.Size / $flashTotal) * 100 } else { 0 }
    Write-Host ("  {0,-18} {1,8} B  ({2,5:N1}%)" -f $_.Section, $_.Size, $pct)
}
Write-Host ("  {0,-18} {1,8} B  ({2,5:N1}%)" -f "TOTAL used in Flash", $flashUsed, ($flashUsed / $flashTotal * 100))
Write-Host ""

Write-Host "RAM consumers (at runtime in SRAM):" -ForegroundColor Yellow
$ramList = @()
foreach ($sec in $ramSections) {
    if ($sections.ContainsKey($sec)) {
        $sz = $sections[$sec]
        $ramStatic += $sz
        $ramList += [PSCustomObject]@{ Section = $sec; Size = $sz }
    }
}
$ramList | Sort-Object Size -Descending | ForEach-Object {
    $pct = if ($ramTotal -gt 0) { ($_.Size / $ramTotal) * 100 } else { 0 }
    Write-Host ("  {0,-18} {1,8} B  ({2,5:N1}%)" -f $_.Section, $_.Size, $pct)
}

# Add explicit heap/stack reservations if present (they are often symbols, not full sections)
if ($minHeap -gt 0 -or $minStack -gt 0) {
    Write-Host ""
    Write-Host "Reserved (from linker script symbols):"
    if ($minHeap -gt 0) {
        $pct = if ($ramTotal -gt 0) { ($minHeap / $ramTotal) * 100 } else { 0 }
        Write-Host ("  {0,-18} {1,8} B  ({2,5:N1}%)   (_Min_Heap_Size)" -f "Heap (reserved)", $minHeap, $pct)
    }
    if ($minStack -gt 0) {
        $pct = if ($ramTotal -gt 0) { ($minStack / $ramTotal) * 100 } else { 0 }
        Write-Host ("  {0,-18} {1,8} B  ({2,5:N1}%)   (_Min_Stack_Size)" -f "Stack (reserved)", $minStack, $pct)
    }
}

Write-Host ""
Write-Host "Run after a build; use --config Release to pin a build configuration."

exit 0