#!/usr/bin/env bash
# package_all.sh — Run all packaging scripts to generate all release artifacts.

set -euo pipefail
SCRIPT_DIR="$(dirname "$0")"

echo "===================================================="
echo " Building all release packages..."
echo "===================================================="
echo ""

# 1. Source Package
echo "--> [1/4] Building Source Package..."
"$SCRIPT_DIR/package_src.sh"

# 2. CMake Package
echo "--> [2/4] Building CMake Package..."
"$SCRIPT_DIR/package_cmake.sh"

# 3. Meson Package
echo "--> [3/4] Building Meson Package..."
"$SCRIPT_DIR/package_meson.sh"

# 4. Meson Wrap File
echo "--> [4/4] Generating Meson Wrap..."
"$SCRIPT_DIR/package_meson_wrap.sh"

echo ""
echo "===================================================="
echo " SUCCESS: All packages generated in install/ directory."
echo "===================================================="
