#!/usr/bin/env bash
# =============================================================================
#  build_cmake.sh - Configure & compile CMake build for Linux / macOS
#
#  Output layout
#  -------------
#    build/cmake/linux/   Linux builds
#    build/cmake/macos/   macOS builds
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

info() { echo -e "${CYN}[build_cmake]${RST} $*"; }
ok()   { echo -e "${GRN}[build_cmake]${RST} $*"; }
die()  { echo -e "${RED}[build_cmake] ERROR:${RST} $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
#  Detect host platform
# ---------------------------------------------------------------------------
PLATFORM="linux"
case "$(uname -s 2>/dev/null)" in
    Darwin) PLATFORM="macos" ;;
esac

BUILD_DIR="build/cmake"
CMAKE_GENERATOR=""
BUILD_TYPE="Release"
CMAKE_OPTS=()

# ---------------------------------------------------------------------------
#  Parse arguments
# ---------------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        -G)
            CMAKE_GENERATOR="$2"
            shift 2
            ;;
        Debug|Release|RelWithDebInfo)
            BUILD_TYPE="$1"
            shift
            ;;
        -h|--help|help)
            echo ""
            echo "OpenLibCLI — CMake build wrapper"
            echo "================================="
            echo ""
            echo "Usage: ./scripts/build-helpers/build_cmake.sh [options]"
            echo ""
            echo "Options:"
            echo "  -G <generator>    CMake generator (e.g. Ninja, Unix Makefiles)"
            echo "  Debug             Debug build"
            echo "  Release           Release build (default)"
            echo "  RelWithDebInfo    Release with debug info"
            echo "  -D<var>=<val>     Pass through as CMake -D flag"
            echo ""
            echo "Output: ${BUILD_DIR}/${PLATFORM}/"
            echo ""
            exit 0
            ;;
        BUILD_ASAN=*|BUILD_EXAMPLE_*=*)
            name="${1%%=*}"
            value="${1#*=}"
            if [ "$value" = "1" ]; then
                CMAKE_OPTS+=("-D${name}=ON")
            elif [ "$value" = "0" ]; then
                CMAKE_OPTS+=("-D${name}=OFF")
            else
                CMAKE_OPTS+=("-D${name}=${value}")
            fi
            shift
            ;;
        BUILD_EXAMPLE_*)
            if [ $# -gt 1 ] && [ "$2" = "0" -o "$2" = "1" ]; then
                if [ "$2" = "1" ]; then
                    CMAKE_OPTS+=("-D${1}=ON")
                else
                    CMAKE_OPTS+=("-D${1}=OFF")
                fi
                shift 2
            else
                CMAKE_OPTS+=("-D${1}=ON")
                shift
            fi
            ;;
        *)
            CMAKE_OPTS+=("$1")
            shift
            ;;
    esac
done

# ---------------------------------------------------------------------------
#  Auto-detect Ninja if no generator was explicitly given
# ---------------------------------------------------------------------------
if [ -z "$CMAKE_GENERATOR" ]; then
    if command -v ninja &>/dev/null; then
        info "ninja found - using Ninja generator"
        CMAKE_GENERATOR="-G Ninja"
    else
        info "ninja not found - using default generator"
    fi
else
    CMAKE_GENERATOR="-G $CMAKE_GENERATOR"
fi

# ---------------------------------------------------------------------------
#  Propagate BUILD_EXAMPLE_* from environment (unless already passed as -D)
# ---------------------------------------------------------------------------
for _var in BUILD_EXAMPLE_TELNET BUILD_EXAMPLE_TCP BUILD_EXAMPLE_SERIAL BUILD_EXAMPLE_PIPE BUILD_EXAMPLE_UNIX_SOCKET BUILD_TESTING BUILD_ASAN; do
    if [ -n "${!_var+x}" ]; then
        _already=0
        for _opt in "${CMAKE_OPTS[@]}"; do
            if [[ "$_opt" == "-D$_var="* ]]; then _already=1; break; fi
        done
        if [ "$_already" -eq 0 ]; then
            if [ "${!_var}" = "1" ]; then
                CMAKE_OPTS+=("-D${_var}=ON")
            elif [ "${!_var}" = "0" ]; then
                CMAKE_OPTS+=("-D${_var}=OFF")
            fi
        fi
    fi
done

# ---------------------------------------------------------------------------
#  Configure
# ---------------------------------------------------------------------------
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    info "Removing stale CMakeCache.txt..."
    rm -f "$BUILD_DIR/CMakeCache.txt"
fi

info "Configuring (BUILD_TYPE=$BUILD_TYPE)..."
cmake -B "$BUILD_DIR" $CMAKE_GENERATOR -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "${CMAKE_OPTS[@]}"
info "Compiling..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"

ok "Done."
echo "  Output dir : $BUILD_DIR/$PLATFORM"
