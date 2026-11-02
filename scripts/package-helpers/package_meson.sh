#!/usr/bin/env bash
# package_meson.sh — Create the Meson subproject package (microlog-style).
# Package name: OpenLibCLI-<version>-meson
# Output layout:
#   install/OpenLibCLI-<version>-meson/
#     meson.build, meson_options.txt <- build files (from meson/)
#     include/OpenLibCLI/            <- public headers (config/, utils/)
#     src/                           <- library source tree
#     examples/                      <- serial + telnet examples
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
mkdir -p "$INSTALL_DIR"

# Headers: the public tree under include/OpenLibCLI/ (config/, utils/ kept)
(cd cli && find . -name '*.h') | while read -r f; do
  mkdir -p "$INSTALL_DIR/include/OpenLibCLI/$(dirname "$f")"
  cp "cli/$f" "$INSTALL_DIR/include/OpenLibCLI/$f"
done

# Sources: the whole library tree, so their relative includes resolve
(cd cli && find . -name '*.c' -o -name '*.h') | while read -r f; do
  mkdir -p "$INSTALL_DIR/src/$(dirname "$f")"
  cp "cli/$f" "$INSTALL_DIR/src/$f"
done

# Build files of the package (maintained in meson/)
cp meson/meson.build meson/meson_options.txt "$INSTALL_DIR/"

# Examples built by -Dbuild_examples=true
mkdir -p "$INSTALL_DIR/examples/shared"
mkdir -p "$INSTALL_DIR/examples/win_linux/serial"
mkdir -p "$INSTALL_DIR/examples/win_linux/telnet"
cp examples/shared/* "$INSTALL_DIR/examples/shared/"
cp examples/win_linux/serial/* "$INSTALL_DIR/examples/win_linux/serial/"
cp examples/win_linux/telnet/* "$INSTALL_DIR/examples/win_linux/telnet/"

# Copy version, LICENSE, README
cp version LICENSE README.md "$INSTALL_DIR/"

echo "Meson subproject package ready at $(pwd)/$INSTALL_DIR"
echo ""
echo "Consumer usage (subproject copy):"
echo "  Copy install/$PKG_NAME/ to MyProject/subprojects/OpenLibCLI/"
echo "  dep = dependency('OpenLibCLI')"
echo ""
echo "Or use the wrap file:"
echo "  Copy install/wrap/OpenLibCLI.wrap to MyProject/subprojects/"
