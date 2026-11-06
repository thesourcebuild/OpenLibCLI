#!/usr/bin/env pwsh
# package_cmake.ps1 — Create the source-only CMake package (microlog-style).
# Package name: OpenLibCLI-<version>-cmake
# Output layout:
#   install/OpenLibCLI-<version>-cmake/
#     include/OpenLibCLI/          <- public headers (config/, utils/)
#     lib/cmake/OpenLibCLI/        <- openlibcli-config.cmake, openlibcli-config-version.cmake
#     share/OpenLibCLI/src/        <- library source tree
#     LICENSE, README.md, version

# Any failed step (e.g. a Copy-Item) must end the script with an error, not be skipped.
$ErrorActionPreference = 'Stop'

pushd $PSScriptRoot/../..

try {
    $VERSION = (Get-Content ./version).Trim()
    $PKG_NAME = "OpenLibCLI-$VERSION-cmake"
    $INSTALL_DIR = "install/$PKG_NAME"
    if (Test-Path $INSTALL_DIR) { Remove-Item -Path $INSTALL_DIR -Recurse -Force }

    # Create directory structure
    New-Item -ItemType Directory -Path "$INSTALL_DIR/lib/cmake/OpenLibCLI" -Force | Out-Null

    # Headers: the public tree under include/OpenLibCLI/ (config/, utils/ kept); sources:
    # the whole library tree, so their relative includes resolve
    $cliRoot = (Resolve-Path "cli").Path
    Get-ChildItem -Path "cli" -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($cliRoot.Length + 1)
        $targets = @("$INSTALL_DIR/share/OpenLibCLI/src/$rel")
        if ($_.Extension -eq ".h") { $targets += "$INSTALL_DIR/include/OpenLibCLI/$rel" }
        foreach ($t in $targets) {
            New-Item -ItemType Directory -Path (Split-Path $t -Parent) -Force | Out-Null
            Copy-Item -Path $_.FullName -Destination $t -Force
        }
    }

    # CMake config files (lower case with hyphens: found by find_package(OpenLibCLI) and
    # find_package(openlibcli) alike, also on case-sensitive file systems)
    Copy-Item -Path "cmake/openlibcli-src-config.cmake" `
        -Destination "$INSTALL_DIR/lib/cmake/OpenLibCLI/openlibcli-config.cmake"
    (Get-Content "cmake/openlibcli-src-config-version.cmake.in") -replace '@OPENLIBCLI_VERSION@', $VERSION |
        Set-Content -Path "$INSTALL_DIR/lib/cmake/OpenLibCLI/openlibcli-config-version.cmake" -Encoding ascii

    # Copy examples as reference (shared + serial + telnet only)
    New-Item -ItemType Directory -Path "$INSTALL_DIR/examples/shared" -Force | Out-Null
    New-Item -ItemType Directory -Path "$INSTALL_DIR/examples/win_linux/serial" -Force | Out-Null
    New-Item -ItemType Directory -Path "$INSTALL_DIR/examples/win_linux/telnet" -Force | Out-Null
    Copy-Item -Path "examples/shared/*" -Destination "$INSTALL_DIR/examples/shared/" -Force
    Copy-Item -Path "examples/win_linux/serial/*" -Destination "$INSTALL_DIR/examples/win_linux/serial/" -Force
    Copy-Item -Path "examples/win_linux/telnet/*" -Destination "$INSTALL_DIR/examples/win_linux/telnet/" -Force

    # Generate examples/CMakeLists.txt (standalone build, finds the package via find_package)
    $examplesCmake = @"
cmake_minimum_required(VERSION 3.14)
project(OpenLibCLI-examples C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(OpenLibCLI REQUIRED)

add_executable(cli_server_serial
    win_linux/serial/cli_server_serial.c
    win_linux/serial/cli_transport_serial.c
    shared/cli_demo_setup.c
)
target_link_libraries(cli_server_serial PRIVATE OpenLibCLI::OpenLibCLI)

add_executable(cli_server_telnet
    win_linux/telnet/cli_server_telnet.c
    win_linux/telnet/cli_transport_telnet.c
    shared/cli_demo_setup.c
)
target_link_libraries(cli_server_telnet PRIVATE OpenLibCLI::OpenLibCLI)
"@
    Set-Content -Path "$INSTALL_DIR/examples/CMakeLists.txt" -Value $examplesCmake -Encoding ascii

    # Copy LICENSE, README, and version
    Copy-Item LICENSE $INSTALL_DIR
    Copy-Item README.md $INSTALL_DIR
    Copy-Item version $INSTALL_DIR

    Write-Host "CMake source package ready at $PWD/$INSTALL_DIR"
    Write-Host ""
    Write-Host "Consumer usage:"
    Write-Host "  cmake -B build -DCMAKE_PREFIX_PATH=/path/to/install/$PKG_NAME"
    Write-Host "  find_package(OpenLibCLI $VERSION REQUIRED)"
    Write-Host "  target_link_libraries(myapp PRIVATE OpenLibCLI::OpenLibCLI)"

    popd
    exit 0
} catch {
    Write-Host "Error: $_"
    popd
    exit 1
}
