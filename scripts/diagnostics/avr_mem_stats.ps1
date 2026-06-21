param(
    [string]$Mcu = "atmega328p",
    [int]$ArduinoVersion = 10819,
    [int]$SramBytes = 2048,
    [int]$FlashBytes = 32768,
    [switch]$TopAll
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param(
        [string]$Exe,
        [string[]]$Arguments
    )

    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("Command failed ({0}): {1} {2}" -f $LASTEXITCODE, $Exe, ($Arguments -join " "))
    }
}

function Find-AvrTool([string]$toolName) {
    $defaultRoot = Join-Path $env:LOCALAPPDATA "Arduino15\packages\arduino\tools\avr-gcc\7.3.0-atmel3.6.1-arduino7\bin"
    $defaultPath = Join-Path $defaultRoot $toolName
    if (Test-Path $defaultPath) {
        return $defaultPath
    }

    $packagesRoot = Join-Path $env:LOCALAPPDATA "Arduino15\packages"
    $found = Get-ChildItem -Path $packagesRoot -Recurse -Filter $toolName -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName

    if (-not $found) {
        throw "Could not find $toolName under $packagesRoot"
    }

    return $found
}

function Parse-NmLine([string]$line) {
    if ($line -match "^\s*[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)\s+([A-Za-z])\s+(\S+)\s*$") {
        [pscustomobject]@{
            Size = [Convert]::ToInt32($Matches[1], 16)
            Type = $Matches[2]
            Name = $Matches[3]
        }
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $repoRoot

$gcc = Find-AvrTool "avr-gcc.exe"
$nm = Find-AvrTool "avr-nm.exe"
$size = Find-AvrTool "avr-size.exe"

$buildDir = Join-Path $repoRoot "build"
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

# Use the embedded config (AVR profile) via -include so both the probe
# and all other translation units get the same AVR-appropriate defaults
# instead of the desktop defaults from config/cli_config.h.
$avrConfig = @(
    "-include", "config/cli_embedded_config.h",
    "-DCLI_ENABLE_MODE_NAMES=0"   # not defined in embedded config
)

$probeC = @"
#define ARDUINO $ArduinoVersion
#define __AVR__ 1
#ifndef CLI_MAX_COMMANDS
#define CLI_MAX_COMMANDS 12
#endif
#include "cli.h"

char probe_cli_t[sizeof(cli_t)];
char probe_cli_cmd_t[sizeof(cli_cmd_t)];
char probe_cmd_pool[sizeof(cli_cmd_t) * CLI_MAX_COMMANDS];
char probe_instances[sizeof(cli_t)];
"@

$probeCPath = Join-Path $buildDir "avr_size_probe.c"
$probeOPath = Join-Path $buildDir "avr_size_probe.o"
$serialOPath = Join-Path $buildDir "avr_cli_serial_app.o"
$demoOPath = Join-Path $buildDir "avr_demo_config.o"
$stubCPath = Join-Path $buildDir "avr_size_stub.c"
$stubOPath = Join-Path $buildDir "avr_size_stub.o"
$serialElfPath = Join-Path $buildDir "avr_cli_serial_app.elf"

Set-Content -Path $probeCPath -Value $probeC -Encoding ascii

Invoke-Checked -Exe $gcc -Arguments (
    @("-mmcu=$Mcu", "-Os", "-DARDUINO=$ArduinoVersion", "-D__AVR__=1", "-Icli") +
    $avrConfig +
    @("-c", $probeCPath, "-o", $probeOPath)
)
$probeNmRaw = & $nm -S --size-sort $probeOPath
$probeRows = $probeNmRaw | ForEach-Object { Parse-NmLine $_ } | Where-Object { $_ }

$probeWanted = @("probe_cli_t", "probe_cli_cmd_t", "probe_cmd_pool", "probe_instances")
$probeStats = foreach ($name in $probeWanted) {
    $row = $probeRows | Where-Object { $_.Name -eq $name } | Select-Object -First 1
    if ($row) {
        [pscustomobject]@{ Metric = $name; Bytes = $row.Size }
    }
}

Invoke-Checked -Exe $gcc -Arguments (
    @("-mmcu=$Mcu", "-Os", "-ffunction-sections", "-fdata-sections",
      "-DARDUINO=$ArduinoVersion", "-D__AVR__=1", "-Icli") +
    $avrConfig +
    @("-c", "scripts/diagnostics/cli_serial.c", "-o", $serialOPath)
)

Invoke-Checked -Exe $gcc -Arguments (
    @("-mmcu=$Mcu", "-Os", "-ffunction-sections", "-fdata-sections",
      "-DARDUINO=$ArduinoVersion", "-D__AVR__=1", "-Icli") +
    $avrConfig +
    @("-c", "examples/shared/cli_demo_setup.c", "-o", $demoOPath)
)

$stubC = @"
#include "cli_serial.h"
#include <stdint.h>

static int16_t stub_available(void *ctx) {
    (void)ctx;
    return 0;
}

static int16_t stub_read(void *ctx) {
    (void)ctx;
    return 0;
}

static int16_t stub_write(void *ctx, const uint8_t *buf, uint16_t len) {
    (void)ctx;
    (void)buf;
    return len;
}

static int16_t stub_flush(void *ctx) {
    (void)ctx;
    return 0;
}

int main(void) {
    cli_transport_t transport;
    transport.available = stub_available;
    transport.read = stub_read;
    transport.write = stub_write;
    transport.flush = stub_flush;
    transport.ctx = 0;

    cli_serial_setup(&transport, 0);
    while (1) {
        cli_serial_poll();
    }
    return 0;
}
"@

Set-Content -Path $stubCPath -Value $stubC -Encoding ascii

Invoke-Checked -Exe $gcc -Arguments (
    @("-mmcu=$Mcu", "-Os", "-ffunction-sections", "-fdata-sections",
      "-DARDUINO=$ArduinoVersion", "-D__AVR__=1",
      "-Iscripts/diagnostics", "-Icli") +
    $avrConfig +
    @("-c", $stubCPath, "-o", $stubOPath)
)

Invoke-Checked -Exe $gcc -Arguments @(
        "-mmcu=$Mcu", "-Os", "-Wl,--gc-sections",
    $serialOPath, $demoOPath, $stubOPath, "-o", $serialElfPath
)

$serialSizeRaw = & $size -B $serialElfPath
$sizeLine = $serialSizeRaw | Where-Object { $_ -match "^\s*\d+\s+\d+\s+\d+\s+\d+\s+[0-9A-Fa-f]+\s+" } |
        Select-Object -Last 1
if (-not $sizeLine) {
        throw "Could not parse avr-size output for $serialElfPath"
}

$serialSize = $null
if ($sizeLine -match "^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+[0-9A-Fa-f]+\s+(.+)$") {
        $serialSize = [pscustomobject]@{
                Text = [int]$Matches[1]
                Data = [int]$Matches[2]
                Bss  = [int]$Matches[3]
                Dec  = [int]$Matches[4]
                File = $Matches[5].Trim()
        }
}

if (-not $serialSize) {
        throw "Unexpected avr-size output: $sizeLine"
}

$serialNmRaw = & $nm -S --size-sort $serialElfPath
$serialRows = $serialNmRaw | ForEach-Object { Parse-NmLine $_ } | Where-Object { $_ }

# B/b/C/D/d are static RAM contributors in object output.
$ramRows = $serialRows | Where-Object { $_.Type -match "^[BbCcDd]$" }
$ramTotal = $serialSize.Data + $serialSize.Bss

$ramPercent = 0.0
if ($SramBytes -gt 0) {
    $ramPercent = [math]::Round((100.0 * $ramTotal) / $SramBytes, 1)
}

$romTotal = $serialSize.Text + $serialSize.Data

$romPercent = 0.0
if ($FlashBytes -gt 0) {
    $romPercent = [math]::Round((100.0 * $romTotal) / $FlashBytes, 1)
}

# T/t = code in .text / .text.init (flash)
$flashRows = $serialRows | Where-Object { $_.Type -match "^[Tt]$" }

$topCount = if ($TopAll) { 1000 } else { 20 }
$topRam = $ramRows | Sort-Object -Property Size -Descending | Select-Object -First $topCount
$topRamTotal = ($topRam | Measure-Object -Property Size -Sum).Sum
if (-not $topRamTotal) { $topRamTotal = 0 }
$topRamPercent = 0.0
if ($SramBytes -gt 0) {
    $topRamPercent = [math]::Round((100.0 * $topRamTotal) / $SramBytes, 1)
}
$ramOther = $ramTotal - $topRamTotal

Write-Host ""
Write-Host "AVR toolchain:" -ForegroundColor Cyan
Write-Host "  gcc: $gcc"
Write-Host "  nm : $nm"
Write-Host "  size: $size"
Write-Host ""
Write-Host "Type/pool sizes from probe object:" -ForegroundColor Cyan
$probeStats | Format-Table -AutoSize
Write-Host ""
Write-Host "Top RAM symbols:" -ForegroundColor Cyan
$topRam | Select-Object Name, Type, @{Name = "Bytes"; Expression = { $_.Size }} | Format-Table -AutoSize
Write-Host ("  Listed total: {0} bytes ({1}% of {2})" -f $topRamTotal, $topRamPercent, $SramBytes)
Write-Host ("  Other: {0} bytes" -f $ramOther)
Write-Host ""
Write-Host "Static RAM in linked examples/cli_serial_app.elf (gc-sections):" -ForegroundColor Cyan
Write-Host ("  Total: {0} bytes ({1}% of {2})" -f $ramTotal, $ramPercent, $SramBytes)
Write-Host ""
Write-Host "Flash (ROM) in linked examples/cli_serial_app.elf (gc-sections):" -ForegroundColor Cyan
Write-Host ("  Total: {0} bytes ({1}% of {2})" -f $romTotal, $romPercent, $FlashBytes)
Write-Host ""

$topFlash = $flashRows | Sort-Object -Property Size -Descending | Select-Object -First $topCount
$topFlashTotal = ($topFlash | Measure-Object -Property Size -Sum).Sum
if (-not $topFlashTotal) { $topFlashTotal = 0 }
$flashOther = $romTotal - $topFlashTotal

Write-Host "Top flash symbols:" -ForegroundColor Cyan
$topFlash | Select-Object Name, @{Name = "Bytes"; Expression = { $_.Size }}, @{Name = "%"; Expression = { "{0:F1}" -f (100.0 * $_.Size / $romTotal) }} | Format-Table -AutoSize
Write-Host ("  Listed total: {0} bytes ({1}% of {2})" -f $topFlashTotal, [math]::Round((100.0 * $topFlashTotal) / $romTotal, 1), $romTotal)
Write-Host ("  Other: {0} bytes" -f $flashOther)

# ./scripts/diagnostics/avr_mem_stats.ps1
# ./scripts/diagnostics/avr_mem_stats.ps1 -Mcu atmega328p -SramBytes 2048
# ./scripts/diagnostics/avr_mem_stats.ps1 -TopAll                    # list all flash + RAM symbols
