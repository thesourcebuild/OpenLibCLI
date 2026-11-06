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
#    ./build.sh all [BUILD_EXAMPLE_*=0|1 ...]  All available toolchains
#
#  All remaining arguments are passed through to the sub-script ('all' takes only the
#  BUILD_EXAMPLE_* settings, the arguments every toolchain understands).
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
#  Colour helpers
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    # The escape characters themselves ($'...'), so the help text (a heredoc) shows them as
    # colours too, not only echo -e.
    CYN=$'\033[0;36m'; GRN=$'\033[0;32m'; RED=$'\033[0;31m'; YLW=$'\033[1;33m'; BLD=$'\033[1m'; RST=$'\033[0m'
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
    "$SCRIPT_DIR/$script" ${REST[@]+"${REST[@]}"}
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
    # Every toolchain gets the same arguments, so only the settings all of them read the
    # same way are taken: a make target would be a CMake error and Meson's build directory.
    local a
    for a in ${REST[@]+"${REST[@]}"}; do
        case "$a" in
            BUILD_EXAMPLE_TELNET=[01]|BUILD_EXAMPLE_TCP=[01]|BUILD_EXAMPLE_SERIAL=[01]|\
            BUILD_EXAMPLE_PIPE=[01]|BUILD_EXAMPLE_UNIX_SOCKET=[01]) ;;
            *) die "'all' takes only BUILD_EXAMPLE_*=0|1 settings, which every toolchain" \
                   "understands. '$a' is for one toolchain: run that one with it" \
                   "(e.g. build.sh gcc $a)." ;;
        esac
    done

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
  scripts/build-helpers/build.sh all [BUILD_EXAMPLE_*=0|1 ...]
                                                  All available

Arguments after the toolchain name are passed through. 'all' takes only the
BUILD_EXAMPLE_* settings, the arguments every toolchain understands.
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
#  Parse: an optional toolchain name as the FIRST argument (as in build.bat); every
#  other argument, wherever it is, is passed on unchanged.
# ---------------------------------------------------------------------------
TOOLCHAIN="auto"
REST=("$@")

if [ $# -gt 0 ]; then
    case "$1" in
        help|--help|-h) show_help ;;
        gcc|cmake|meson|all) TOOLCHAIN="$1"; REST=("${@:2}") ;;
        mingw)               TOOLCHAIN="gcc"; REST=("${@:2}") ;;
    esac
fi

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
