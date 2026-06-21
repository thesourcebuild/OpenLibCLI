#!/usr/bin/env pwsh
# package_meson.ps1 — Create the Meson subproject package (microlog-style).
# Package name: OpenLibCLI-<version>-meson
# Output layout:
#   install/OpenLibCLI-<version>-meson/
#     meson.build                    <- subproject-ready build file
#     include/OpenLibCLI/            <- public headers
#     src/                           <- source files
#     LICENSE, README.md, version

pushd $PSScriptRoot/../..

try {
    $VERSION = (Get-Content ./version).Trim()
    $PKG_NAME = "OpenLibCLI-$VERSION-meson"
    $INSTALL_DIR = "install/$PKG_NAME"
    if (Test-Path $INSTALL_DIR) { Remove-Item -Path $INSTALL_DIR -Recurse -Force }

    # Create directory structure
    New-Item -ItemType Directory -Path "$INSTALL_DIR/include/OpenLibCLI/config" -Force | Out-Null
    New-Item -ItemType Directory -Path "$INSTALL_DIR/src" -Force | Out-Null

    # Copy headers (preserving config/ subdir)
    Copy-Item -Path "cli/cli.h", "cli/cli_version.h", "cli/cli_env_detect.h" `
        -Destination "$INSTALL_DIR/include/OpenLibCLI/"
    Copy-Item -Path "cli/config/cli_config.h", "cli/config/cli_embedded_config.h" `
        -Destination "$INSTALL_DIR/include/OpenLibCLI/config/"

    # Copy sources
    Copy-Item -Path "cli/cli.c" `
        -Destination "$INSTALL_DIR/src/"

    # Generate meson.build
    $mesonContent = @"
# OpenLibCLI - subproject-ready build file
# Package name: $PKG_NAME

project(
    'OpenLibCLI',
    'c',
    version : '$VERSION',
    license : 'MIT',
    default_options : ['warning_level=3', 'c_std=c99'],
)

# -- Platform dependencies ---------------------------------------------------
cc = meson.get_compiler('c')
platform_deps = []
if host_machine.system() == 'windows'
    platform_deps += cc.find_library('ws2_32')
endif

# -- Sources & include dirs --------------------------------------------------
src = files(
    'src/cli.c',
)
inc = include_directories('include/OpenLibCLI')

# -- Library -----------------------------------------------------------------
openlibcli_lib = library(
    meson.project_name(),
    src,
    include_directories : inc,
    dependencies : platform_deps,
    install : true,
)

# -- Dependency object (for subproject / wrap consumers) ---------------------
openlibcli_dep = declare_dependency(
    include_directories : inc,
    link_with           : openlibcli_lib,
    dependencies        : platform_deps,
    version             : meson.project_version(),
)

meson.override_dependency(meson.project_name(), openlibcli_dep)

# -- Examples (opt-in via -Dbuild_examples=true) -----------------------------
if get_option('build_examples')
    executable('cli_server_serial',
        'examples/win_linux/serial/cli_server_serial.c',
        'examples/win_linux/serial/cli_transport_serial.c',
        'examples/shared/cli_demo_setup.c',
        dependencies : [openlibcli_dep],
        install : false,
    )
    executable('cli_server_telnet',
        'examples/win_linux/telnet/cli_server_telnet.c',
        'examples/win_linux/telnet/cli_transport_telnet.c',
        'examples/shared/cli_demo_setup.c',
        dependencies : [openlibcli_dep],
        install : false,
    )
endif

"@
    Set-Content -Path "$INSTALL_DIR/meson.build" -Value $mesonContent -Encoding ascii

    # Generate meson_options.txt for the build_examples option
    $optionsContent = @"
option('build_examples', type : 'boolean', value : false,
    description : 'Build example programs (serial + telnet)')
"@
    Set-Content -Path "$INSTALL_DIR/meson_options.txt" -Value $optionsContent -Encoding ascii

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

    # Copy version, LICENSE, README
    Copy-Item version   $INSTALL_DIR
    Copy-Item LICENSE   $INSTALL_DIR
    Copy-Item README.md $INSTALL_DIR

    Write-Host "Meson subproject package ready at $PWD/$INSTALL_DIR"
    Write-Host ""
    Write-Host "Consumer usage (subproject copy):"
    Write-Host "  Copy install/$PKG_NAME/ to MyProject/subprojects/OpenLibCLI/"
    Write-Host "  dep = dependency('OpenLibCLI')"
    Write-Host ""
    Write-Host "Or use the wrap file:"
    Write-Host "  Copy install/wrap/OpenLibCLI.wrap to MyProject/subprojects/"

    popd
    exit 0
} catch {
    Write-Host "Error: $_"
    popd
    exit 1
}
