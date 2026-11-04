param(
    [Parameter(ValueFromRemainingArguments)]
    [string[]]$args
)

# =============================================================================
#  build_cmake.ps1 - Configure & compile CMake build with automatic Ninja
#                    detection.  Output layout handled by CMakeLists.txt.
#
#  Output layout
#  -------------
#    build\cmake\windows\binaries\   executables
#    build\cmake\windows\libs\       libraries
# =============================================================================

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path

# Always the project's own build tree, wherever the script is run from.
$buildDir = Join-Path $ProjectRoot "build\cmake"
$generator = $null
$buildType = "Release"
$cmakeOpts = @()

function Show-Help {
    Write-Host
    Write-Host "  OpenLibCLI - CMake build wrapper"
    Write-Host "  =================================="
    Write-Host
    Write-Host "  Usage: scripts\build-helpers\build_cmake.ps1 [options]"
    Write-Host
    Write-Host    "  Options:"
    Write-Host "    -G <generator>    CMake generator (e.g. Ninja, Visual Studio 17 2022)"
    Write-Host "    Debug             Debug build"
    Write-Host "    Release           Release build (default)"
    Write-Host "    RelWithDebInfo    Release with debug info"
    Write-Host "    BUILD_EXAMPLE_*=1|0  Pass through as -D option"
    Write-Host "    -D<var>=<val>     Pass through as CMake -D flag"
    Write-Host
    Write-Host "  Output: build\cmake\windows\"
    Write-Host
}

# --------------------------------------------------------------------------
#  Parse arguments
# --------------------------------------------------------------------------
$i = 0
while ($i -lt $args.Count) {
    $a = $args[$i]

    if ($a -eq "help" -or $a -eq "--help" -or $a -eq "-h") {
        Show-Help
        exit 0
    }

    if ($a -eq "-G") {
        if ($i + 1 -lt $args.Count) {
            $i++
            $generator = $args[$i]
        }
        $i++
        continue
    }

    # Not a switch: 'continue' inside a switch block only leaves the switch.
    if ($a -eq "Debug" -or $a -eq "Release" -or $a -eq "RelWithDebInfo") {
        $buildType = $a
        $i++
        continue
    }

    # Translate BUILD_EXAMPLE_*=1|0 to -D flags
    if ($a -match '^(BUILD_EXAMPLE_\w+)=(\d)$') {
        $cmakeOpts += "-D$($matches[1])=$(if ($matches[2] -eq '1') { 'ON' } else { 'OFF' })"
        $i++
        continue
    }
    if ($a -match '^BUILD_EXAMPLE_\w+$') {
        # Bare flag — check next arg for 0/1
        if ($i + 1 -lt $args.Count -and $args[$i + 1] -match '^[01]$') {
            $value = if ($args[$i + 1] -eq "1") { "ON" } else { "OFF" }
            $cmakeOpts += "-D$a=$value"
            $i += 2
            continue
        }
        $cmakeOpts += "-D$a=ON"
        $i++
        continue
    }

    # CMake options only: a bare word (e.g. a make target such as "clean") would be taken
    # by CMake as a source path and ignored with a warning, and a full build would follow.
    if ($a -notmatch '^-') {
        Write-Host "[build_cmake] Error: unknown argument '$a' (see 'help')."
        exit 1
    }
    $cmakeOpts += $a
    $i++
}


# --------------------------------------------------------------------------
#  Auto-detect Ninja if no generator was explicitly given
# --------------------------------------------------------------------------
if (-not $generator) {
    if (Get-Command "ninja" -ErrorAction SilentlyContinue) {
        Write-Host "[build_cmake] ninja found - using Ninja generator"
        $generator = "Ninja"
    } else {
        Write-Host "[build_cmake] ninja not found - using default generator"
    }
}

$genArg = if ($generator) { @("-G", $generator) } else { @() }

# --------------------------------------------------------------------------
#  Remove stale cache
# --------------------------------------------------------------------------
if (Test-Path "$buildDir\CMakeCache.txt") {
    Write-Host "[build_cmake] Removing stale CMakeCache.txt..."
    Remove-Item -Force "$buildDir\CMakeCache.txt" -ErrorAction SilentlyContinue
}

# --------------------------------------------------------------------------
#  Configure
# --------------------------------------------------------------------------
Write-Host "[build_cmake] Configuring (BUILD_TYPE=$buildType)..."
if ($cmakeOpts.Count -gt 0 -and ($cmakeOpts | Where-Object { $_ -match '^-DBUILD_EXAMPLE_' })) {
    Write-Host "[build_cmake] Options: $(($cmakeOpts | Where-Object { $_ -match '^-DBUILD_EXAMPLE_' }) -join ' ')"
}
$configureArgs = @("-S", $ProjectRoot, "-B", $buildDir) + $genArg + @("-DCMAKE_BUILD_TYPE=$buildType") + $cmakeOpts
& cmake $configureArgs
if ($LASTEXITCODE -ne 0) { exit 1 }

# --------------------------------------------------------------------------
#  Build
# --------------------------------------------------------------------------
Write-Host "[build_cmake] Compiling..."
& cmake --build $buildDir --config $buildType
if ($LASTEXITCODE -ne 0) { exit 1 }

# --------------------------------------------------------------------------
#  Copy DLL to libs\ as well
# --------------------------------------------------------------------------
$outDir = Join-Path $buildDir "windows"
# The DLL this build made, as CMake records it (MSVC and MinGW name it differently): a DLL
# another toolchain left in the same tree is never copied instead.
$sharedFile = Join-Path $buildDir "openlibcli_shared_$buildType.txt"
if (-not (Test-Path $sharedFile)) {
    Write-Host "[build_cmake] Error: $sharedFile was not generated."
    exit 1
}
$dllPath = (Get-Content -Raw $sharedFile).Trim()
$libsDir = "$outDir\libs"
if (-not (Test-Path $libsDir)) { New-Item -ItemType Directory -Path $libsDir -Force | Out-Null }
Copy-Item -Path $dllPath -Destination "$libsDir\" -Force
Write-Host "[build_cmake] Copied $(Split-Path -Leaf $dllPath) to libs\"

Write-Host "[build_cmake] Done."
Write-Host "  Output dir : $outDir"
