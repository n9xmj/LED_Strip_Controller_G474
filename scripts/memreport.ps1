# memreport.ps1 -- concise RAM/FLASH report from the GCC .map file
#
# Usage:
#   scripts\memreport.ps1
#   scripts\memreport.ps1 --map "Debug\LED_Strip_Controller_G474.map"
#
# Designed to be called by the /memreport skill.
# Parses the "Memory Configuration" and the main "Linker script and memory map"
# sections to produce human-readable totals + itemized major sections.

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path $PSScriptRoot -Parent
$projectName = "LED_Strip_Controller_G474"
$defaultMap = Join-Path $repoRoot "Debug\$projectName.map"

$MapPath = $defaultMap

# Simple manual arg parsing (consistent with other project scripts)
for ($i = 0; $i -lt $args.Count; $i++) {
    switch -Regex ($args[$i]) {
        '^--?map$' { 
            if ($i + 1 -lt $args.Count) {
                $MapPath = $args[++$i]
            }
            break 
        }
        default { }
    }
}

if (-not (Test-Path $MapPath)) {
    Write-Error "No .map file found at '$MapPath'. Run a build first (/build or /fullbuild)."
    exit 2
}

$mapContent = Get-Content $MapPath -Raw
$mapLines = Get-Content $MapPath

Write-Host "=== Memory Report for $projectName ===" -ForegroundColor Cyan
Write-Host "Map file: $MapPath"
Write-Host "Generated: $((Get-Item $MapPath).LastWriteTime)"
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

# 3. Categorize and report
$flashSections = @('.isr_vector', '.text', '.rodata', '.data', '.ARM.exidx', '.ARM.extab', '.ARM.attributes')
$ramSections   = @('.data', '.bss', '.heap', '.stack', '._user_heap_stack')

$flashUsed = 0
$ramStatic = 0   # .data + .bss at runtime

Write-Host "Major Sections (itemized from map):"
Write-Host ""

Write-Host "FLASH consumers (stored in Flash):" -ForegroundColor Yellow
$flashList = @()
foreach ($sec in $flashSections) {
    if ($sections.ContainsKey($sec)) {
        $sz = $sections[$sec]
        $flashUsed += $sz
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

$staticRam = ($sections['.data'] + $sections['.bss'])   # runtime .data + .bss
$totalReservedRam = $staticRam + $minHeap + $minStack

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host ("Flash used : {0,7:N1} KB / {1,6:N1} KB  ({2,5:N1}%)" -f ($flashUsed/1KB), ($flashTotal/1KB), ($flashUsed/$flashTotal*100))
Write-Host ("Static RAM : {0,7:N1} KB   (.data + .bss at runtime)" -f ($staticRam/1KB))
Write-Host ("+ Reserved heap : {0,6:N1} KB" -f ($minHeap/1KB))
Write-Host ("+ Reserved stack: {0,6:N1} KB" -f ($minStack/1KB))
Write-Host ("Total committed in RAM: {0,7:N1} KB / {1,6:N1} KB  ({2,5:N1}%)" -f ($totalReservedRam/1KB), ($ramTotal/1KB), ($totalReservedRam/$ramTotal*100))

$freeRam = $ramTotal - $totalReservedRam
Write-Host ("Rough free RAM (for RTOS tasks, dynamic alloc, USB buffers, etc.): {0:N1} KB" -f ($freeRam/1KB))

Write-Host ""
Write-Host "Note: Percentages are of the memory region defined in the linker script."
Write-Host "      'Static RAM' above does not include the reserved heap/stack areas."
Write-Host "      Run after a clean build for the most accurate picture."

exit 0