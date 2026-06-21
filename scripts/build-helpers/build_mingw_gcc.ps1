param(
    [Parameter(ValueFromRemainingArguments)]
    [string[]]$args
)

# =============================================================================
#  build_mingw_gcc.ps1 - OpenLibCLI GCC / GNU Make build
#
#  Output layout
#  -------------
#    build\gcc\windows\binaries\   executables
#    build\gcc\windows\libs\       static/import/shared libraries and PDBs
#    build\gcc\windows\obj\        object files
# =============================================================================

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path "$ScriptDir\..\.."
Push-Location $ProjectRoot

# --------------------------------------------------------------------------
#  Argument parsing: separate BUILD_EXAMPLE_* vars from targets
# --------------------------------------------------------------------------
$targetParts = @()
$buildVars = @()

$i = 0
while ($i -lt $args.Count) {
    $a = $args[$i]

    if ($a -eq "help" -or $a -eq "--help" -or $a -eq "-h") {
        Show-Help
        exit 0
    }

    if ($a -match '^(BUILD_EXAMPLE_TELNET|BUILD_EXAMPLE_TCP|BUILD_EXAMPLE_SERIAL|BUILD_EXAMPLE_PIPE|BUILD_EXAMPLE_UNIX_SOCKET)(?:=(.*))?$') {
        if ($matches[2]) {
            $buildVars += "$($matches[1])=$($matches[2])"
        } else {
            if ($i + 1 -lt $args.Count -and $args[$i+1] -notmatch '^(BUILD_EXAMPLE_|run-|clean|-|lib$|shared$|telnet$|tcp$|uart$)') {
                $i++
                $buildVars += "$a=$($args[$i])"
            } else {
                $buildVars += "$a=1"
            }
        }
        $i++
        continue
    }

    $targetParts += $a
    $i++
}

if ($targetParts.Count -eq 0) { $targets = "all" }
else { $targets = $targetParts -join " " }

# --------------------------------------------------------------------------
#  Locate GNU Make
# --------------------------------------------------------------------------
$makeCmd = $null
foreach ($candidate in @("make", "mingw32-make")) {
    if (Get-Command $candidate -ErrorAction SilentlyContinue) {
        $makeCmd = $candidate
        break
    }
}
if (-not $makeCmd) {
    Write-Host
    Write-Host "  ERROR: GNU Make not found. Install make or mingw32-make and add to PATH."
    Write-Host
    exit 1
}

function Show-Help {
    Write-Host
    Write-Host "  OpenLibCLI - GCC / GNU Make build"
    Write-Host "  =================================="
    Write-Host
    Write-Host "  Output directories:"
    Write-Host "    build\gcc\windows\binaries"
    Write-Host "    build\gcc\windows\libs"
    Write-Host "    build\gcc\windows\obj"
    Write-Host "    build\gcc\windows\*.map"
    Write-Host
    Write-Host "  Usage: scripts\build-helpers\build_mingw_gcc.ps1 [target] [BUILD_EXAMPLE_*=0|1 ...]"
    Write-Host
    Write-Host "  Targets:"
    Write-Host "    (none)        Build static + shared libraries and examples  [default]"
    Write-Host "    lib           Static library only"
    Write-Host "    shared        Shared library only"
    Write-Host "    telnet        Telnet server  (build\gcc\windows\binaries\cli_server_telnet.exe)"
    Write-Host "    tcp           TCP server   (build\gcc\windows\binaries\cli_server_tcp.exe)"
    Write-Host "    uart          Serial server demo  (build\gcc\windows\binaries\cli_server_serial.exe)"
    Write-Host "    run-telnet    Build + run telnet server on port 2323"
    Write-Host "    run-tcp       Build + run TCP server on port 2323"
    Write-Host "    run-serial      Build + run serial / stdin demo"
    Write-Host "    clean         Remove build\gcc\windows\ artefacts"
    Write-Host "    clean-all     Remove entire build\ tree"
    Write-Host "    help          Show this message"
    Write-Host
    Write-Host "  Transport switches:"
    Write-Host "    BUILD_EXAMPLE_TELNET=0|1      Enable Telnet transport  (default: 1)"
    Write-Host "    BUILD_EXAMPLE_TCP=0|1         Enable TCP transport     (default: 0)"
    Write-Host "    BUILD_EXAMPLE_SERIAL=0|1      Enable serial transport  (default: 1)"
    Write-Host "    BUILD_EXAMPLE_PIPE=0|1        Enable pipe transport    (default: 0)"
    Write-Host "    BUILD_EXAMPLE_UNIX_SOCKET=0|1 Enable UNIX socket       (default: 0)"
    Write-Host
    Write-Host "  Examples:"
    Write-Host "    scripts\build-helpers\build_mingw_gcc.ps1"
    Write-Host "    scripts\build-helpers\build_mingw_gcc.ps1 BUILD_EXAMPLE_TCP=1"
    Write-Host "    scripts\build-helpers\build_mingw_gcc.ps1 lib"
    Write-Host
}

# --------------------------------------------------------------------------
#  Version header
# --------------------------------------------------------------------------
Write-Host "[build_gcc] Generating version header..."
$python = Get-Command "python" -ErrorAction SilentlyContinue
if ($python) {
    & python "$ProjectRoot\scripts\gen_version_header.py" 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[build_gcc] Regenerated cli/cli_version.h"
    }
} else {
    Write-Host "[build_gcc] Python not found, using existing cli/cli_version.h"
}

# --------------------------------------------------------------------------
#  Build
# --------------------------------------------------------------------------
Write-Host "[build_gcc] Toolchain  : GCC"
Write-Host "[build_gcc] Build tool : $makeCmd"
Write-Host "[build_gcc] Output dir : build\gcc\windows\"
if ($buildVars.Count -gt 0) {
    Write-Host "[build_gcc] Build vars  : $($buildVars -join ' ')"
}
Write-Host "[build_gcc] Target     : $targets"
Write-Host

$makeArgs = @("CC=gcc", "PLATFORM=windows") + $buildVars + @($targets)
& $makeCmd $makeArgs
$exitCode = $LASTEXITCODE

Pop-Location
exit $exitCode
