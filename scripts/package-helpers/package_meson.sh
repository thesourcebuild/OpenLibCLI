#!/usr/bin/env bash
# package_meson.sh — Create the Meson subproject package (microlog-style).
# Package name: OpenLibCLI-<version>-meson
# Output layout:
#   install/OpenLibCLI-<version>-meson/
#     meson.build                    <- subproject-ready build file
#     meson_options.txt              <- build options
#     include/OpenLibCLI/            <- public headers
#     src/                           <- source files
#     examples/                      <- reference examples
#     LICENSE, README.md, version

set -euo pipefail
cd "$(dirname "$0")/../.."

if [ ! -f version ]; then
  echo "Error: version file not found"
  exit 1
fi

VERSION=$(tr -d '[:space:]' < version)
PKG_NAME="OpenLibCLI-$VERSION-meson"
INSTALL_DIR="install/$PKG_NAME"
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR/include/OpenLibCLI/config"
mkdir -p "$INSTALL_DIR/src"

# Copy headers (preserving config/ subdir)
for f in cli/cli.h cli/cli_version.h cli/cli_env_detect.h; do
  [ -f "$f" ] && cp "$f" "$INSTALL_DIR/include/OpenLibCLI/"
done
cp cli/config/cli_config.h cli/config/cli_embedded_config.h \
  "$INSTALL_DIR/include/OpenLibCLI/config/" 2>/dev/null || true

# Copy sources
cp cli/cli.c "$INSTALL_DIR/src/" 2>/dev/null || true

# Generate meson.build
cat > "$INSTALL_DIR/meson.build" << MESON
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
MESON

# Generate meson_options.txt
cat > "$INSTALL_DIR/meson_options.txt" << 'MESONOPT'
option('build_examples', type : 'boolean', value : false,
    description : 'Build example programs (serial + telnet)')
MESONOPT

# Copy examples
mkdir -p "$INSTALL_DIR/examples/shared"
mkdir -p "$INSTALL_DIR/examples/win_linux/serial"
mkdir -p "$INSTALL_DIR/examples/win_linux/telnet"
cp examples/shared/* "$INSTALL_DIR/examples/shared/" 2>/dev/null || true
cp examples/win_linux/serial/* "$INSTALL_DIR/examples/win_linux/serial/" 2>/dev/null || true
cp examples/win_linux/telnet/* "$INSTALL_DIR/examples/win_linux/telnet/" 2>/dev/null || true

# Generate examples/CMakeLists.txt (standalone build via find_package)
cat > "$INSTALL_DIR/examples/CMakeLists.txt" << 'EXCFG'
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
EXCFG

# Copy version, LICENSE, README
cp version LICENSE README.md "$INSTALL_DIR/" 2>/dev/null || true

echo "Meson subproject package ready at $(pwd)/$INSTALL_DIR"
echo ""
echo "Consumer usage (subproject copy):"
echo "  Copy install/$PKG_NAME/ to MyProject/subprojects/OpenLibCLI/"
echo "  dep = dependency('OpenLibCLI')"
echo ""
echo "Or use the wrap file:"
echo "  Copy install/wrap/OpenLibCLI.wrap to MyProject/subprojects/"
