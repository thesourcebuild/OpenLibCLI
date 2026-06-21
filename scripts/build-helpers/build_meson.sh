#!/usr/bin/env bash
# =============================================================================
#  build_meson.sh - Configure & compile Meson build for Linux / macOS
#
#  Output layout
#  -------------
#    build/meson/linux/   Linux builds
#    build/meson/macos/   macOS builds
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# ---------------------------------------------------------------------------
#  Colour helpers
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    CYN='\033[0;36m'; GRN='\033[0;32m'; RED='\033[0;31m'; YLW='\033[1;33m'; RST='\033[0m'
else
    CYN=''; GRN=''; RED=''; YLW=''; RST=''
fi

info() { echo -e "${CYN}[build_meson]${RST} $*"; }
ok()   { echo -e "${GRN}[build_meson]${RST} $*"; }
die()  { echo -e "${RED}[build_meson] ERROR:${RST} $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
#  Detect host platform
# ---------------------------------------------------------------------------
PLATFORM="linux"
case "$(uname -s 2>/dev/null)" in
    Darwin) PLATFORM="macos" ;;
esac

BUILD_DIR="build/meson"
BUILD_DIR_SET=0
MESON_OPTS=()

# ---------------------------------------------------------------------------
#  Help
# ---------------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        help|--help|-h)
            echo ""
            echo "OpenLibCLI — Meson build wrapper"
            echo "================================="
            echo ""
            echo "Usage: ./scripts/build-helpers/build_meson.sh [build-dir] [options]"
            echo ""
            echo "  build-dir    Output directory (default: build/meson)"
            echo "  options      Passed through to meson setup"
            echo ""
            echo "Output: \$BUILD_DIR/\$PLATFORM/"
            echo "    binaries/   Executables"
            echo "    libs/       Libraries"
            echo ""
            exit 0
            ;;
        BUILD_EXAMPLE_*=*)
            name="${1%%=*}"
            value="${1#*=}"
            if [ "$value" = "1" ]; then
                MESON_OPTS+=("-D${name}=true")
            elif [ "$value" = "0" ]; then
                MESON_OPTS+=("-D${name}=false")
            else
                MESON_OPTS+=("-D${name}=${value}")
            fi
            ;;
        BUILD_EXAMPLE_*)
            if [ $# -gt 1 ] && { [ "$2" = "0" ] || [ "$2" = "1" ]; }; then
                if [ "$2" = "1" ]; then
                    MESON_OPTS+=("-D${1}=true")
                else
                    MESON_OPTS+=("-D${1}=false")
                fi
                shift
            else
                MESON_OPTS+=("-D${1}=true")
            fi
            ;;
        -* )
            MESON_OPTS+=("$1")
            ;;
        *)
            if [ $BUILD_DIR_SET -eq 0 ]; then
                BUILD_DIR="$1"
                BUILD_DIR_SET=1
            else
                MESON_OPTS+=("$1")
            fi
            ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
#  Backend detection
# ---------------------------------------------------------------------------
BACKEND=""
if ! command -v ninja &>/dev/null; then
    warn "ninja not found - trying default backend"
fi

# ---------------------------------------------------------------------------
#  Propagate BUILD_EXAMPLE_* from environment (unless already passed as -D)
# ---------------------------------------------------------------------------
for _var in BUILD_EXAMPLE_TELNET BUILD_EXAMPLE_TCP BUILD_EXAMPLE_SERIAL BUILD_EXAMPLE_PIPE BUILD_EXAMPLE_UNIX_SOCKET; do
    if [ -n "${!_var+x}" ]; then
        _already=0
        for _opt in "${MESON_OPTS[@]}"; do
            if [[ "$_opt" == "-D$_var="* ]]; then _already=1; break; fi
        done
        if [ "$_already" -eq 0 ]; then
            if [ "${!_var}" = "1" ]; then
                MESON_OPTS+=("-D${_var}=true")
            elif [ "${!_var}" = "0" ]; then
                MESON_OPTS+=("-D${_var}=false")
            fi
        fi
    fi
done

# ---------------------------------------------------------------------------
#  Configure
# ---------------------------------------------------------------------------
info "Configuring (build dir: $BUILD_DIR)..."
meson setup "$BUILD_DIR" --wipe $BACKEND --prefix "/$PLATFORM" --bindir binaries --libdir libs "${MESON_OPTS[@]}"
info "Compiling..."
meson compile -C "$BUILD_DIR"
info "Installing to ${BUILD_DIR}/${PLATFORM}/..."
ROOT_DIR="$PWD"
meson install -C "$BUILD_DIR" --destdir "$ROOT_DIR/$BUILD_DIR" >/dev/null

ok "Done."
echo "  Binaries  : $BUILD_DIR/$PLATFORM/binaries"
echo "  Libraries : $BUILD_DIR/$PLATFORM/libs"
