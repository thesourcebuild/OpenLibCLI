#!/usr/bin/env bash
# package_cmake.sh — Create the source-only CMake package (microlog-style).
# Package name: OpenLibCLI-<version>-cmake
# Output layout:
#   install/OpenLibCLI-<version>-cmake/
#     include/OpenLibCLI/          <- public headers (config/, utils/)
#     lib/cmake/OpenLibCLI/        <- openlibcli-config.cmake, openlibcli-config-version.cmake
#     share/OpenLibCLI/src/        <- library source tree
#     examples/                    <- reference examples
#     LICENSE, README.md, version

set -euo pipefail
cd "$(dirname "$0")/../.."

if [ ! -f version ]; then
  echo "Error: version file not found"
  exit 1
fi

VERSION=$(tr -d '[:space:]' < version)
PKG_NAME="OpenLibCLI-$VERSION-cmake"
INSTALL_DIR="install/$PKG_NAME"
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR/lib/cmake/OpenLibCLI"

# Headers: the public tree under include/OpenLibCLI/ (config/, utils/ kept)
(cd cli && find . -name '*.h') | while read -r f; do
  mkdir -p "$INSTALL_DIR/include/OpenLibCLI/$(dirname "$f")"
  cp "cli/$f" "$INSTALL_DIR/include/OpenLibCLI/$f"
done

# Sources: the whole library tree, so their relative includes resolve
(cd cli && find . -name '*.c' -o -name '*.h') | while read -r f; do
  mkdir -p "$INSTALL_DIR/share/OpenLibCLI/src/$(dirname "$f")"
  cp "cli/$f" "$INSTALL_DIR/share/OpenLibCLI/src/$f"
done

# CMake config files (lower case with hyphens: found by find_package(OpenLibCLI) and
# find_package(openlibcli) alike, also on case-sensitive file systems)
cp cmake/openlibcli-src-config.cmake "$INSTALL_DIR/lib/cmake/OpenLibCLI/openlibcli-config.cmake"
sed "s/@OPENLIBCLI_VERSION@/$VERSION/" cmake/openlibcli-src-config-version.cmake.in \
  > "$INSTALL_DIR/lib/cmake/OpenLibCLI/openlibcli-config-version.cmake"

# Copy examples
mkdir -p "$INSTALL_DIR/examples/shared"
mkdir -p "$INSTALL_DIR/examples/win_linux/serial"
mkdir -p "$INSTALL_DIR/examples/win_linux/telnet"
cp examples/shared/* "$INSTALL_DIR/examples/shared/" 2>/dev/null || true
cp examples/win_linux/serial/* "$INSTALL_DIR/examples/win_linux/serial/" 2>/dev/null || true
cp examples/win_linux/telnet/* "$INSTALL_DIR/examples/win_linux/telnet/" 2>/dev/null || true

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

cp LICENSE README.md version "$INSTALL_DIR/" 2>/dev/null || true

echo "CMake source package ready at $(pwd)/$INSTALL_DIR"
echo ""
echo "Consumer usage:"
echo "  cmake -B build -DCMAKE_PREFIX_PATH=$(pwd)/$INSTALL_DIR"
echo "  find_package(OpenLibCLI $VERSION REQUIRED)"
echo "  target_link_libraries(myapp PRIVATE OpenLibCLI::OpenLibCLI)"
