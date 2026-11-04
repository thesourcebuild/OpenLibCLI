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
$ProjectRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path

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

# First positional arg is build directory (override default), relative to where the script
# was run; the default is relative to the project root.
if ($remaining.Count -gt 0) {
    # Combine() keeps an absolute path as given and resolves a relative one.
    $buildDir = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine((Get-Location).Path, $remaining[0]))
    $remaining = @($remaining | Select-Object -Skip 1)
} else {
    $buildDir = Join-Path $ProjectRoot $buildDir
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
    $setupArgs = @("setup", $buildDir, $ProjectRoot, "--wipe") + $(if ($backend) { @($backend) } else { @() }) + @("--prefix", "/windows", "--bindir", "binaries", "--libdir", "libs") + $mesonOpts
} else {
    if (Test-Path $buildDir) {
        # Only what a failed setup leaves (meson-logs, meson-private) is removed; any other
        # content is not a Meson build and is never deleted.
        $foreign = @(Get-ChildItem -Force -LiteralPath $buildDir | Where-Object { $_.Name -notlike "meson-*" })
        if ($foreign.Count -gt 0) {
            Write-Host "[build_meson] Error: $buildDir is not a Meson build directory (it contains $($foreign[0].Name))."
            Write-Host "[build_meson]        Pass a new or empty directory."
            exit 1
        }
        Remove-Item -Recurse -Force -LiteralPath $buildDir
    }
    New-Item -ItemType Directory -Path $buildDir | Out-Null
    $setupArgs = @("setup", $buildDir, $ProjectRoot) + $(if ($backend) { @($backend) } else { @() }) + @("--prefix", "/windows", "--bindir", "binaries", "--libdir", "libs") + $mesonOpts
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
$rootDir = $ProjectRoot
& meson install -C $buildDir --destdir "$rootDir\build\meson" 2>&1
if ($LASTEXITCODE -ne 0) { exit 1 }

# --------------------------------------------------------------------------
#  Copy DLL to libs\ as well
# --------------------------------------------------------------------------
# The DLL this build made, named by Meson itself (MSVC and MinGW name it differently): a DLL
# another toolchain left in the same tree is never copied instead.
$targets = & meson introspect $buildDir --targets | ConvertFrom-Json
$shared = @($targets | Where-Object { $_.type -eq "shared library" -and $_.name -eq "OpenLibCLI" })
if ($shared.Count -ne 1) {
    Write-Host "[build_meson] Error: the shared library target was not found in $buildDir."
    exit 1
}
$dllName = Split-Path -Leaf $shared[0].filename[0]
Copy-Item -Path "$rootDir\build\meson\windows\binaries\$dllName" -Destination "$rootDir\build\meson\windows\libs\" -Force
Write-Host "[build_meson] Copied $dllName to libs\"

Write-Host "[build_meson] Done."
Write-Host "  Binaries  : build\meson\windows\binaries"
Write-Host "  Libraries : build\meson\windows\libs"
