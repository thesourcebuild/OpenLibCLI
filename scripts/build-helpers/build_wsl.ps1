param(
    [Parameter(ValueFromRemainingArguments)]
    [string[]]$args
)

# =============================================================================
#  build_wsl.ps1 - Cross-compile via WSL Ubuntu
#
#  Launches WSL Ubuntu and runs the selected build script inside it.
#  Selectable backends: gcc (default), cmake, meson
#
#  Output layouts:
#    -gcc:     build/gcc/linux/{binaries,libs,obj}/
#    -cmake:   build/cmake/linux/{binaries,libs}/
#    -meson:   build/meson/linux/{binaries,libs}/
#
#  Usage:
#    scripts\build-helpers\build_wsl                               (gcc, all targets)
#    scripts\build-helpers\build_wsl shared                        (gcc, DLL only)
#    scripts\build-helpers\build_wsl clean-all                     (gcc, clean)
#    scripts\build-helpers\build_wsl BUILD_EXAMPLE_TELNET=1        (gcc, specific examples)
#    scripts\build-helpers\build_wsl -Backend all                  (gcc + cmake + meson)
#    scripts\build-helpers\build_wsl -Backend all BUILD_EXAMPLE_TELNET=1
#    scripts\build-helpers\build_wsl -Backend cmake                (cmake build)
#    scripts\build-helpers\build_wsl -Backend cmake BUILD_EXAMPLE_TELNET=1
#    scripts\build-helpers\build_wsl -Backend meson                (meson build)
#    scripts\build-helpers\build_wsl -Backend meson BUILD_EXAMPLE_TELNET=1
#    scripts\build-helpers\build_wsl -WslDist Ubuntu clean-all     (override distro)
# =============================================================================

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
#  Help
# ---------------------------------------------------------------------------
if ($args -contains 'help' -or $args -contains '--help' -or $args -contains '-h') {
    Write-Host @"
OpenLibCLI -- build_wsl   Cross-compile via WSL Ubuntu

Usage:
    scripts\build-helpers\build_wsl                               (gcc, all targets)
    scripts\build-helpers\build_wsl shared                        (gcc, DLL only)
    scripts\build-helpers\build_wsl clean-all                     (gcc, clean)
    scripts\build-helpers\build_wsl BUILD_EXAMPLE_TELNET=1        (gcc, specific examples)
    scripts\build-helpers\build_wsl -Backend all                  (gcc + cmake + meson)
    scripts\build-helpers\build_wsl -Backend all BUILD_EXAMPLE_TELNET=1
    scripts\build-helpers\build_wsl -Backend cmake                (cmake build)
    scripts\build-helpers\build_wsl -Backend cmake BUILD_EXAMPLE_TELNET=1
    scripts\build-helpers\build_wsl -Backend meson                (meson build)
    scripts\build-helpers\build_wsl -Backend meson BUILD_EXAMPLE_TELNET=1
    scripts\build-helpers\build_wsl -WslDist Ubuntu clean-all     (override distro)

Backends:
    gcc        Default. Uses build_mingw_gcc.sh (GNU Make)
    cmake      Uses build_cmake.sh (CMake)
    meson      Uses build_meson.sh (Meson)
    all        Runs gcc, then cmake, then meson in sequence

Options:
    -Backend <name>    Select build backend (gcc, cmake, meson, all)
    -WslDist <name>    WSL distro name (default: first Ubuntu-named distro)

Output layouts:
    -gcc:     build/gcc/linux/{binaries,libs,obj}/
    -cmake:   build/cmake/linux/{binaries,libs}/
    -meson:   build/meson/linux/{binaries,libs}/
"@
    exit 0
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path "$ScriptDir\..\.."

$Backend = "gcc"
$WslDist = $null
$buildVars = @()
$targets = @()

# ---------------------------------------------------------------------------
#  Map backend to .sh script
# ---------------------------------------------------------------------------
function Get-BackendScript($name) {
    switch ($name.ToLower()) {
        'gcc'   { return 'build_mingw_gcc.sh' }
        'cmake' { return 'build_cmake.sh' }
        'meson' { return 'build_meson.sh' }
        'all'   { return $null }
        default {
            Write-Error "Unknown backend '$name'. Valid: gcc, cmake, meson"
            exit 1
        }
    }
}

# ---------------------------------------------------------------------------
#  Parse args: -Backend, -WslDist, KEY=VALUE (build vars), everything else
# ---------------------------------------------------------------------------
$i = 0
while ($i -lt $args.Count) {
    if ($args[$i] -eq '-WslDist' -and ($i + 1) -lt $args.Count) {
        $WslDist = $args[$i + 1]
        $i += 2
    } elseif ($args[$i] -eq '-Backend' -and ($i + 1) -lt $args.Count) {
        $Backend = $args[$i + 1]
        $i += 2
    } elseif ($args[$i] -match '^[A-Z_][A-Z0-9_]*=.') {
        $buildVars += $args[$i]
        $i++
    } else {
        $targets += $args[$i]
        $i++
    }
}

# ---------------------------------------------------------------------------
#  Find WSL distro  (default: first Ubuntu-named distro)
# ---------------------------------------------------------------------------
if (-not $WslDist) {
    $enc = [Console]::OutputEncoding
    [Console]::OutputEncoding = [System.Text.Encoding]::Unicode
    $distros = @(wsl --list --quiet 2>&1 | ForEach-Object { $_.Trim() })
    [Console]::OutputEncoding = $enc
    $WslDist = $distros | Where-Object { $_ -match '^Ubuntu' } | Select-Object -First 1
    if (-not $WslDist) { $WslDist = "Ubuntu" }
}

# ---------------------------------------------------------------------------
#  Convert Windows path to WSL path
# ---------------------------------------------------------------------------
$Drive = $ProjectRoot.Drive.Name.ToLower()
$RelPath = $ProjectRoot.Path.Substring(3) -replace '\\', '/'
$WslPath = "/mnt/$Drive/$RelPath"

$envStr = ($buildVars -join ' ')
$targetStr = ($targets -join ' ')

# ---------------------------------------------------------------------------
#  Build / run the bash command(s)
# ---------------------------------------------------------------------------
function Invoke-WslBuild($script, $includeTargets) {
    $cmd = @()
    if ($envStr) { $cmd += $envStr }
    $cmd += "bash scripts/build-helpers/$script"
    if ($includeTargets -and $targetStr) { $cmd += $targetStr }
    $cmd = $cmd -join ' '

    Write-Host "[build_wsl] Running  : $script"
    Write-Host "[build_wsl] bash cmd : $cmd"

    wsl -d $WslDist --cd "$WslPath" -- bash -c "$cmd"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[build_wsl] FAILED    : $script (exit $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
}

if ($Backend -eq 'all') {
    Write-Host "[build_wsl] Backend   : all (gcc, cmake, meson)"
    Write-Host "[build_wsl] Distro    : $WslDist"
    Write-Host "[build_wsl] Project   : $WslPath"
    Write-Host "[build_wsl] Env vars  : $envStr"
    Write-Host "[build_wsl] Targets   : $targetStr (gcc only)"

    Invoke-WslBuild 'build_mingw_gcc.sh' $true
    Invoke-WslBuild 'build_cmake.sh'     $false
    Invoke-WslBuild 'build_meson.sh'     $false

    Write-Host "[build_wsl] All three backends completed successfully."
} else {
    $backendScript = Get-BackendScript $Backend

    $cmd = @()
    if ($envStr) { $cmd += $envStr }
    $cmd += "bash scripts/build-helpers/$backendScript"
    if ($targetStr) { $cmd += $targetStr }
    $cmd = $cmd -join ' '

    Write-Host "[build_wsl] Backend   : $Backend ($backendScript)"
    Write-Host "[build_wsl] Distro    : $WslDist"
    Write-Host "[build_wsl] Project   : $WslPath"
    Write-Host "[build_wsl] Env vars  : $envStr"
    Write-Host "[build_wsl] Targets   : $targetStr"
    Write-Host "[build_wsl] bash cmd  : $cmd"

    wsl -d $WslDist --cd "$WslPath" -- bash -c "$cmd"
    exit $LASTEXITCODE
}
