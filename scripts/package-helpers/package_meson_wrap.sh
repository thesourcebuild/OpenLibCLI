#!/usr/bin/env bash
# package_meson_wrap.sh — Generate the OpenLibCLI.wrap file for consumers
# Output: install/wrap/OpenLibCLI.wrap
# Usage: place OpenLibCLI.wrap in MyProject/subprojects/ then call
#        dependency('OpenLibCLI') in your meson.build.

set -euo pipefail
cd "$(dirname "$0")/../.."

if [ ! -f version ]; then
  echo "Error: version file not found"
  exit 1
fi

VERSION=$(tr -d '[:space:]' < version)
TEMPLATE="meson/meson.wrap.in"

if [ ! -f "$TEMPLATE" ]; then
  echo "Error: template not found: $TEMPLATE"
  exit 1
fi

mkdir -p install/wrap
sed "s/@CLI_VERSION@/$VERSION/g" "$TEMPLATE" > install/wrap/OpenLibCLI.wrap

echo "Generated: $(pwd)/install/wrap/OpenLibCLI.wrap"
echo ""
cat install/wrap/OpenLibCLI.wrap
