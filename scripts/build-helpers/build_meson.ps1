param(
    [Parameter(ValueFromRemainingArguments)]
    [string[]]$args
)

# =============================================================================
#  build_meson.ps1 - Configure & compile Meson build with automatic backend
#                    fallback, installs to build\meson\ layout.
#
#  Output layout
#  -------------
#    build\meson\windows\binaries\   executables
#    build\meson\windows\libs\       libraries
# =============================================================================

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# --------------------------------------------------------------------------
#  Parse arguments: separate BUILD_EXAMPLE_* vars from positional args
# --------------------------------------------------------------------------
$buildDir = "build\meson"
$mesonOpts = @()
$remaining = @()
$i = 0
while ($i -lt $args.Count) {
    $a = $args[$i]

    if ($a -eq "help" -or $a -eq "--help" -or $a -eq "-h") {
        Write-Host
        Write-Host "  OpenLibCLI - Meson build wrapper"
        Write-Host "  =================================="
        Write-Host
        Write-Host "  Usage: scripts\build-helpers\build_meson.ps1 [build-dir]"
        Write-Host
        Write-Host "    BUILD_EXAMPLE_*=1|0    Pass through as -D option"
        Write-Host "    build-dir              Output directory (default: build\meson)"
        Write-Host
        Write-Host "  Output:"
        Write-Host "    build\meson\windows\binaries\   Executables"
        Write-Host "    build\meson\windows\libs\       Libraries"
        Write-Host
        exit 0
    }

    if ($a -match '^(BUILD_EXAMPLE_\w+)=(\d)$') {
        $value = if ($matches[2] -eq "1") { "true" } else { "false" }
        $mesonOpts += "-D$($matches[1])=$value"
        $i++
        continue
    }

    if ($a -match '^BUILD_EXAMPLE_\w+$') {
        # Bare flag without value — check next arg for 0/1
        if ($i + 1 -lt $args.Count -and $args[$i + 1] -match '^[01]$') {
            $value = if ($args[$i + 1] -eq "1") { "true" } else { "false" }
            $mesonOpts += "-D$a=$value"
            $i += 2
            continue
        }
        # No value — treat as =1
        $mesonOpts += "-D$a=true"
        $i++
        continue
    }

    $remaining += $a
    $i++
}

# First positional arg is build directory (override default)
if ($remaining.Count -gt 0) {
    $buildDir = $remaining[0]
    $remaining = $remaining[1..($remaining.Count - 1)]
}
if ($remaining.Count -gt 0) {
    Write-Host "[build_meson] Warning: unknown arguments ignored: $($remaining -join ' ')"
}

# --------------------------------------------------------------------------
#  Backend detection
# --------------------------------------------------------------------------
$backend = ""
if (-not (Get-Command "ninja" -ErrorAction SilentlyContinue)) {
    Write-Host "[build_meson] ninja not found - falling back to --backend=vs"
    $backend = "--backend=vs"
} else {
    Write-Host "[build_meson] ninja found - using default backend"
}

# --------------------------------------------------------------------------
#  Configure
# --------------------------------------------------------------------------
Write-Host "[build_meson] Configuring..."
if ($mesonOpts.Count -gt 0) {
    Write-Host "[build_meson] Options: $($mesonOpts -join ' ')"
}
$hasBuildInfo = Test-Path (Join-Path $buildDir "meson-info")
if ($hasBuildInfo) {
    $setupArgs = @("setup", $buildDir, "--wipe") + $(if ($backend) { @($backend) } else { @() }) + @("--prefix", "/windows", "--bindir", "binaries", "--libdir", "libs") + $mesonOpts
} else {
    # Delete any stale partial directory, then create fresh
    if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
    New-Item -ItemType Directory -Path $buildDir | Out-Null
    $setupArgs = @("setup", $buildDir) + $(if ($backend) { @($backend) } else { @() }) + @("--prefix", "/windows", "--bindir", "binaries", "--libdir", "libs") + $mesonOpts
}
& meson $setupArgs
if ($LASTEXITCODE -ne 0) { exit 1 }

# --------------------------------------------------------------------------
#  Compile
# --------------------------------------------------------------------------
Write-Host "[build_meson] Compiling..."
& meson compile -C $buildDir
if ($LASTEXITCODE -ne 0) { exit 1 }

# --------------------------------------------------------------------------
#  Install
# --------------------------------------------------------------------------
Write-Host "[build_meson] Installing to build\meson\windows\..."
$rootDir = Get-Location
& meson install -C $buildDir --destdir "$rootDir\build\meson" 2>&1
if ($LASTEXITCODE -ne 0) { exit 1 }

# --------------------------------------------------------------------------
#  Copy DLL to libs\ as well
# --------------------------------------------------------------------------
$dllPath = if (Test-Path "$rootDir\build\meson\windows\binaries\OpenLibCLI.dll") {
    "$rootDir\build\meson\windows\binaries\OpenLibCLI.dll"
} elseif (Test-Path "$rootDir\build\meson\windows\binaries\libopenlibcli.dll") {
    "$rootDir\build\meson\windows\binaries\libopenlibcli.dll"
} else { $null }
if ($dllPath) {
    Copy-Item -Path $dllPath -Destination "$rootDir\build\meson\windows\libs\" -Force
    Write-Host "[build_meson] Copied $(Split-Path -Leaf $dllPath) to libs\"
}

Write-Host "[build_meson] Done."
Write-Host "  Binaries  : build\meson\windows\binaries"
Write-Host "  Libraries : build\meson\windows\libs"
