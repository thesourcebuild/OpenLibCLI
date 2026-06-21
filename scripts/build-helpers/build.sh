#!/usr/bin/env bash
# =============================================================================
#  build.sh — Unified build dispatcher for OpenLibCLI (Linux / macOS / WSL)
#
#  Routes to the appropriate build script based on the first argument:
#
#    ./build.sh                     Auto-detect toolchain (gcc -> cmake -> meson)
#    ./build.sh gcc [args...]       GCC + GNU Make    (build_mingw_gcc.sh)
#    ./build.sh mingw [args...]     Alias for gcc
#    ./build.sh cmake [args...]     CMake             (build_cmake.sh)
#    ./build.sh meson [args...]     Meson             (build_meson.sh)
#    ./build.sh all [args...]       All available toolchains
#
#  All remaining arguments are passed through to the sub-script.
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
#  Colour helpers
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    CYN='\033[0;36m'; GRN='\033[0;32m'; RED='\033[0;31m'; YLW='\033[1;33m'; BLD='\033[1m'; RST='\033[0m'
else
    CYN=''; GRN=''; RED=''; YLW=''; BLD=''; RST=''
fi

info()  { echo -e "${CYN}[build]${RST} $*"; }
die()   { echo -e "${RED}[build] ERROR:${RST} $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
#  Helper functions
# ---------------------------------------------------------------------------
run_script() {
    local label="$1" script="$2"; shift 2
    info "Toolchain: $label"
    "$SCRIPT_DIR/$script" "${REST[@]}"
}

auto_detect() {
    if command -v gcc &>/dev/null; then
        info "Auto-detected GCC"
        run_script "GCC" "build_mingw_gcc.sh"
        exit 0
    fi
    if command -v cmake &>/dev/null; then
        info "Auto-detected CMake"
        run_script "CMake" "build_cmake.sh"
        exit 0
    fi
    if command -v meson &>/dev/null; then
        info "Auto-detected Meson"
        run_script "Meson" "build_meson.sh"
        exit 0
    fi
    echo ""
    echo -e "${RED}ERROR: No build tool found. Install one of:${RST}"
    echo "  a) GCC (gcc) — apt install build-essential / xcode-select --install"
    echo "  b) CMake + Ninja — apt install cmake ninja-build"
    echo "  c) Meson + Ninja — pip install meson ninja"
    echo ""
    exit 1
}

dispatch_all() {
    info "Running all available toolchains..."

    if command -v gcc &>/dev/null; then
        echo -e "${GRN}[build] --- GCC ---${RST}"
        run_script "GCC" "build_mingw_gcc.sh"
    else
        echo -e "${YLW}[build] GCC not available, skipping${RST}"
    fi

    if command -v cmake &>/dev/null; then
        echo -e "${GRN}[build] --- CMake ---${RST}"
        run_script "CMake" "build_cmake.sh"
    else
        echo -e "${YLW}[build] CMake not available, skipping${RST}"
    fi

    if command -v meson &>/dev/null; then
        echo -e "${GRN}[build] --- Meson ---${RST}"
        run_script "Meson" "build_meson.sh"
    else
        echo -e "${YLW}[build] Meson not available, skipping${RST}"
    fi

    info "All toolchains done."
}

show_help() {
    cat <<HELP

${BLD}OpenLibCLI — Unified build dispatcher${RST}
======================================

Routes to the appropriate build script based on toolchain.

Usage:
  scripts/build-helpers/build.sh                  Auto-detect
  scripts/build-helpers/build.sh gcc [args...]    GCC + GNU Make
  scripts/build-helpers/build.sh cmake [args...]  CMake
  scripts/build-helpers/build.sh meson [args...]  Meson
  scripts/build-helpers/build.sh all [args...]    All available

Arguments after the toolchain name are passed through.
Run the sub-script directly for per-toolchain help:
  build_mingw_gcc.sh help
  build_cmake.sh help
  build_meson.sh help

Output directories:
  build/gcc/linux/       GCC   (GNU Make)
  build/cmake/linux/     CMake
  build/meson/linux/     Meson

HELP
    exit 0
}

# ---------------------------------------------------------------------------
#  Parse: toolchain name (if first arg matches) + build REST from remaining
# ---------------------------------------------------------------------------
TOOLCHAIN=""
REST=()

for arg in "$@"; do
    if [ -z "$TOOLCHAIN" ]; then
        case "$arg" in
            help|--help|-h) show_help; exit 0 ;;
            gcc|cmake|meson|all)
                TOOLCHAIN="$arg"
                continue
                ;;
            mingw)
                TOOLCHAIN="gcc"
                continue
                ;;
        esac
    fi
    REST+=("$arg")
done

: "${TOOLCHAIN:=auto}"

# ---------------------------------------------------------------------------
#  Dispatch
# ---------------------------------------------------------------------------
case "$TOOLCHAIN" in
    gcc)   run_script "GCC"   "build_mingw_gcc.sh"  ;;
    cmake) run_script "CMake" "build_cmake.sh"      ;;
    meson) run_script "Meson" "build_meson.sh"      ;;
    all)   dispatch_all ;;
    auto)  auto_detect ;;
esac
