#!/usr/bin/env bash
# package_src.sh — Create the source-drop package (copy-paste install)
# Package name: OpenLibCLI-<version>-src
# Output: install/OpenLibCLI-<version>-src/{cli/, LICENSE, README.md, version}

set -euo pipefail
cd "$(dirname "$0")/../.."

if [ ! -f version ]; then
  echo "Error: version file not found"
  exit 1
fi

VERSION=$(tr -d '[:space:]' < version)
PKG_NAME="OpenLibCLI-$VERSION-src"
INSTALL_DIR="install/$PKG_NAME"
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

cp -r cli "$INSTALL_DIR/"
cp LICENSE README.md version "$INSTALL_DIR/" 2>/dev/null || true

echo "Source package ready at $(pwd)/$INSTALL_DIR"
