#!/usr/bin/env bash
# =============================================================================
#  build_mingw_gcc.sh — GCC/GNU Make builder for OpenLibCLI (Linux / macOS)
#
#  Output layout
#  -------------
#    build/gcc/linux/     GCC Linux builds
#    build/gcc/macos/     Clang/GCC macOS builds
#    build/gcc/embedded/  Cross-compiled bare-metal library
#
#  Usage
#  -----
#    ./scripts/build-helpers/build_mingw_gcc.sh                       Build static + shared + telnet + serial
#    ./scripts/build-helpers/build_mingw_gcc.sh lib                   Static library only
#    ./scripts/build-helpers/build_mingw_gcc.sh shared                Shared library only
#    ./scripts/build-helpers/build_mingw_gcc.sh telnet                Telnet server example
#    ./scripts/build-helpers/build_mingw_gcc.sh tcp                   TCP server example
#    ./scripts/build-helpers/build_mingw_gcc.sh uart                  Serial server example (stdin sim)
#    ./scripts/build-helpers/build_mingw_gcc.sh embedded              Cross-compile → build/gcc/embedded/
#    ./scripts/build-helpers/build_mingw_gcc.sh run-telnet [PORT]     Build + run telnet server (default 2323)
#    ./scripts/build-helpers/build_mingw_gcc.sh run-tcp [PORT]        Build + run TCP server (default 2323)
#    ./scripts/build-helpers/build_mingw_gcc.sh run-serial            Build + run serial demo
#    ./scripts/build-helpers/build_mingw_gcc.sh clean                  Remove host-platform artefacts
#    ./scripts/build-helpers/build_mingw_gcc.sh clean-all              Remove entire build/ tree
#    ./scripts/build-helpers/build_mingw_gcc.sh help                  Show this message
#
#  Environment variables
#    CC=gcc                   Host compiler  (default: gcc)
#    CROSS_CC=arm-none-eabi-gcc  Cross-compiler for 'embedded'
#    PORT=2323                Telnet port for run-telnet
#    BUILD_EXAMPLE_TELNET=0|1     Enable Telnet transport / telnet demo (default: 1)
#    BUILD_EXAMPLE_TCP=0|1        Enable TCP transport / tcp demo (default: 0)
#    BUILD_EXAMPLE_SERIAL=0|1     Enable generic serial transport / demo (default: 1)
#    BUILD_EXAMPLE_PIPE=0|1       Enable pipe transport / demo (default: 0)
#    BUILD_EXAMPLE_UNIX_SOCKET=0|1  Enable UNIX socket transport / demo (default: 0)
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# ---------------------------------------------------------------------------
#  Colour helpers (disabled when not a terminal)
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
    CYN='\033[0;36m'; BLD='\033[1m';    RST='\033[0m'
else
    RED=''; GRN=''; YLW=''; CYN=''; BLD=''; RST=''
fi

info() { echo -e "${CYN}[build_mingw_gcc]${RST} $*"; }
ok()   { echo -e "${GRN}[build_mingw_gcc]${RST} $*"; }
warn() { echo -e "${YLW}[build_mingw_gcc]${RST} $*"; }
die()  { echo -e "${RED}[build_mingw_gcc] ERROR:${RST} $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
#  Detect host platform
# ---------------------------------------------------------------------------
PLATFORM="linux"
case "$(uname -s 2>/dev/null)" in
    Darwin)              PLATFORM="macos"   ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
esac

# ---------------------------------------------------------------------------
#  Locate GNU make
# ---------------------------------------------------------------------------
MAKE_CMD=""
for _candidate in make gmake gnumake; do
    if command -v "$_candidate" &>/dev/null; then
        MAKE_CMD="$_candidate"; break
    fi
done
if [ -z "$MAKE_CMD" ]; then
    case "$PLATFORM" in
        linux)
            die "GNU Make not found. Install it with:  sudo apt update && sudo apt install build-essential" ;;
        macos)
            die "GNU Make not found. Install it with:  brew install make" ;;
        windows)
            die "GNU Make not found. Install it via MSYS2:  pacman -S make" ;;
        *)
            die "GNU Make not found. Install 'make' (or 'gmake' on BSD)." ;;
    esac
fi

# ---------------------------------------------------------------------------
#  Arguments
# ---------------------------------------------------------------------------
TARGETS=()
BUILD_VARS=()
PORT="${PORT:-2323}"
CROSS_CC="${CROSS_CC:-arm-none-eabi-gcc}"

while [ $# -gt 0 ]; do
    case "$1" in
        help|--help|-h)
            TARGETS+=(help)
            ;;
        BUILD_EXAMPLE_TELNET=*|BUILD_EXAMPLE_TCP=*|BUILD_EXAMPLE_SERIAL=*|BUILD_EXAMPLE_PIPE=*|BUILD_EXAMPLE_UNIX_SOCKET=*)
            BUILD_VARS+=("$1")
            ;;
        PORT=*)
            PORT="${1#PORT=}"
            ;;
        *)
            TARGETS+=("$1")
            ;;
    esac
    shift
done

if [ ${#TARGETS[@]} -eq 0 ]; then
    TARGETS=(all)
fi

TARGETS_STR="${TARGETS[*]}"

# ---------------------------------------------------------------------------
#  Helper: resolve the platform output directory
# ---------------------------------------------------------------------------
outdir() {
    case "$PLATFORM" in
        windows) echo "build/gcc/windows" ;;
        macos)   echo "build/gcc/macos"   ;;
        *)       echo "build/gcc/linux"   ;;
    esac
}

# ---------------------------------------------------------------------------
#  Dispatch
# ---------------------------------------------------------------------------
case "$TARGETS_STR" in

    help)
        echo ""
        echo -e "${BLD}OpenLibCLI Library — GCC build wrapper${RST}"
        echo "============================================"
        echo ""
        echo "Usage: ./scripts/build-helpers/build_mingw_gcc.sh [target] [port]"
        echo ""
        echo "Output directories:"
        echo "  build/gcc/linux/     Linux host builds"
        echo "  build/gcc/macos/     macOS host builds"
        echo "  build/gcc/embedded/  Bare-metal cross-compile"
        echo ""
        echo "Targets:"
        echo "  (none)           Static + shared libraries + telnet + serial examples  [default]"
        echo "  lib              Static library only"
        echo "  shared           Shared library only"
        echo "  telnet           Telnet server example"
        echo "  tcp              TCP server example"
        echo "  uart             Serial server example (stdin simulation)"
        echo "  embedded         Cross-compile -> build/gcc/embedded/libs/libopenlibcli.a"
        echo "  run-telnet [P]   Build + run telnet server on port P"
        echo "  run-tcp [P]      Build + run TCP server on port P"
        echo "  run-serial       Build + run serial demo"
        echo "  clean            Remove host-platform artefacts"
        echo "  clean-all        Remove entire build/ tree"
        echo "  help             Show this message"
        echo ""
        echo "Environment:"
        echo "  CC=gcc                    Host compiler (default: gcc)"
        echo "  CROSS_CC=arm-none-eabi-gcc  Cross-compiler for 'embedded'"
        echo "  PORT=2323                 Telnet port (default: 2323)"
        echo "  BUILD_EXAMPLE_TELNET=0|1      Enable Telnet transport / telnet demo"
        echo "  BUILD_EXAMPLE_TCP=0|1         Enable TCP transport (tcp demo also requires"
        echo "                             BUILD_EXAMPLE_TELNET=1) (default: 0)"
        echo "  BUILD_EXAMPLE_SERIAL=0|1      Enable generic serial transport / demo (default: 1)"
        echo "  BUILD_EXAMPLE_PIPE=0|1        Enable pipe transport / demo (default: 0)"
        echo "  BUILD_EXAMPLE_UNIX_SOCKET=0|1 Enable UNIX socket transport / demo (default: 0)"
        echo ""
        echo "Examples:"
        echo "  ./scripts/build-helpers/build_mingw_gcc.sh"
        echo "  BUILD_EXAMPLE_TCP=1 ./scripts/build-helpers/build_mingw_gcc.sh"
        echo "  ./scripts/build-helpers/build_mingw_gcc.sh run-telnet 9999"
        echo "  CC=clang ./scripts/build-helpers/build_mingw_gcc.sh"
        echo "  CROSS_CC=arm-none-eabi-gcc ./scripts/build-helpers/build_mingw_gcc.sh embedded"
        echo "  ./scripts/build-helpers/build_mingw_gcc.sh clean-all"
        echo ""
        exit 0
        ;;

    embedded)
        info "Cross-compiling for embedded target ..."
        info "CROSS_CC  : ${CROSS_CC}"
        info "Output    : build/gcc/embedded/"
        echo ""
        "$MAKE_CMD" PLATFORM="$PLATFORM" \
                    CROSS_CC="$CROSS_CC" "${BUILD_VARS[@]}" embedded
        ok "Embedded library: build/gcc/embedded/libs/libopenlibcli.a"
        ;;

    run-telnet)
        OUT="$(outdir)"
        info "Building telnet server -> ${OUT}/ ..."
        "$MAKE_CMD" CC="${CC:-gcc}" PLATFORM="$PLATFORM" "${BUILD_VARS[@]}" cli_server_telnet
        echo ""
        exec "./${OUT}/binaries/cli_server_telnet" "$PORT"
        ;;

    run-tcp)
        OUT="$(outdir)"
        info "Building TCP server -> ${OUT}/ ..."
        "$MAKE_CMD" CC="${CC:-gcc}" PLATFORM="$PLATFORM" "${BUILD_VARS[@]}" cli_server_tcp
        echo ""
        exec "./${OUT}/binaries/cli_server_tcp" "$PORT"
        ;;

    run-serial)
        OUT="$(outdir)"
        info "Building embedded serial demo -> ${OUT}/ ..."
        "$MAKE_CMD" CC="${CC:-gcc}" PLATFORM="$PLATFORM" "${BUILD_VARS[@]}" cli_server_serial
        echo ""
        exec "./${OUT}/binaries/cli_server_serial"
        ;;

    clean|clean-all|distclean|lib|shared|all|cli_server_telnet|cli_server_tcp|telnet|tcp|cli_server_serial|uart)
        OUT="$(outdir)"
        info "Platform  : ${PLATFORM}   ->  ${OUT}/"
        info "Compiler  : ${CC:-gcc}"
        info "Make      : ${MAKE_CMD}"
        info "Target    : ${TARGETS_STR}"
        echo ""
        "$MAKE_CMD" CC="${CC:-gcc}" PLATFORM="$PLATFORM" "${BUILD_VARS[@]}" ${TARGETS_STR}
        ok "Done."
        ;;

    *)
        warn "Unknown target '${TARGETS_STR}' — passing to ${MAKE_CMD} ..."
        "$MAKE_CMD" CC="${CC:-gcc}" PLATFORM="$PLATFORM" "${BUILD_VARS[@]}" ${TARGETS_STR}
        ;;

esac
