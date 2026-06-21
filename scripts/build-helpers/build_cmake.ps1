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

$buildDir = "build\cmake"
$generator = $null
$buildType = "Release"
$cmakeOpts = @()

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

    switch ($a) {
        "Debug"      { $buildType = "Debug"; $i++; continue }
        "Release"    { $buildType = "Release"; $i++; continue }
        "RelWithDebInfo" { $buildType = "RelWithDebInfo"; $i++; continue }
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

    $cmakeOpts += $a
    $i++
}

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
    Write-Host "  Output: $buildDir\windows\"
    Write-Host
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
$configureArgs = @("-B", $buildDir) + $genArg + @("-DCMAKE_BUILD_TYPE=$buildType") + $cmakeOpts
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
$rootDir = Get-Location
$dllPath = if (Test-Path "$rootDir\$buildDir\windows\binaries\OpenLibCLI.dll") {
    "$rootDir\$buildDir\windows\binaries\OpenLibCLI.dll"
} elseif (Test-Path "$rootDir\$buildDir\windows\binaries\libopenlibcli.dll") {
    "$rootDir\$buildDir\windows\binaries\libopenlibcli.dll"
} else { $null }
if ($dllPath) {
    $libsDir = "$rootDir\$buildDir\windows\libs"
    if (-not (Test-Path $libsDir)) { New-Item -ItemType Directory -Path $libsDir -Force | Out-Null }
    Copy-Item -Path $dllPath -Destination "$libsDir\" -Force
    Write-Host "[build_cmake] Copied $(Split-Path -Leaf $dllPath) to libs\"
}

Write-Host "[build_cmake] Done."
Write-Host "  Output dir : $buildDir\windows"
