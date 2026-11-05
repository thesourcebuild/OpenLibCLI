#!/bin/bash

# =============================================================================
#  lint.sh — Code formatting and static analysis (Unix/macOS)
#
#  Works on the project's own sources: cli/, examples/ and tests/ (never on
#  build output or generated packages). Settings: .clang-format, .clang-tidy.
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

SOURCE_DIRS="cli examples tests"

MODE="all"
if [ "$1" == "format" ]; then MODE="format"; fi
if [ "$1" == "check" ];  then MODE="check";  fi
if [ "$1" == "fix" ];    then MODE="fix";    fi
if [ "$1" == "help" ]; then
    echo ""
    echo " Usage: ./scripts/lint/lint.sh [format|check|fix|help]"
    echo ""
    echo " Commands (on cli/, examples/ and tests/):"
    echo "   (none)    Run both format and check"
    echo "   format    Run clang-format on the .c and .h files"
    echo "   check     Run clang-tidy on the .c files"
    echo "   fix       Run clang-tidy with -fix-errors"
    echo "   help      Show this message"
    echo ""
    echo "# Using specific clang binaries:"
    echo "# You can force the scripts to use explicit clang binaries by setting"
    echo "# the CLANG_FORMAT_EXE and CLANG_TIDY_EXE environment variables."
    echo "# Example (current shell):"
    echo "#   export CLANG_FORMAT_EXE=/full/path/to/clang-format"
    echo "#   export CLANG_TIDY_EXE=/full/path/to/clang-tidy"
    echo "#   ./scripts/lint/lint.sh check"
    echo ""
    exit 0
fi

NEED_FORMAT=0
NEED_TIDY=0
if [ "$MODE" == "all" ] || [ "$MODE" == "format" ]; then NEED_FORMAT=1; fi
if [ "$MODE" != "format" ]; then NEED_TIDY=1; fi

# Check for the tools this mode uses. Allow overriding by predefining
# CLANG_FORMAT_EXE / CLANG_TIDY_EXE.
if [ "$NEED_FORMAT" == "1" ]; then
    if [ -z "$CLANG_FORMAT_EXE" ]; then
        if ! command -v clang-format &> /dev/null; then
            echo "[ERROR] clang-format not found in PATH. Set CLANG_FORMAT_EXE to the full path if installed elsewhere."
            FAIL=1
        else
            CLANG_FORMAT_EXE="$(command -v clang-format)"
        fi
    else
        echo "[lint.sh] Using CLANG_FORMAT_EXE=$CLANG_FORMAT_EXE"
    fi
fi

if [ "$NEED_TIDY" == "1" ]; then
    if [ -z "$CLANG_TIDY_EXE" ]; then
        if ! command -v clang-tidy &> /dev/null; then
            echo "[ERROR] clang-tidy not found in PATH. Set CLANG_TIDY_EXE to the full path if installed elsewhere."
            FAIL=1
        else
            CLANG_TIDY_EXE="$(command -v clang-tidy)"
        fi
    else
        echo "[lint.sh] Using CLANG_TIDY_EXE=$CLANG_TIDY_EXE"
    fi
fi

if [ "$FAIL" == "1" ]; then exit 1; fi

do_format() {
    echo "[lint.sh] Formatting code with clang-format..."
    find $SOURCE_DIRS \( -name "*.c" -o -name "*.h" \) -print0 | xargs -0 "$CLANG_FORMAT_EXE" -i
}

do_check() {
    local extra_flags=""
    local failed=0

    if [ "$MODE" == "fix" ]; then
        echo "[lint.sh] Auto-fixing code with clang-tidy..."
        extra_flags="-fix-errors"
    else
        echo "[lint.sh] Analyzing code with clang-tidy..."
    fi

    # Compiled as the builds compile them: C99, the library headers on the include path, and
    # on Linux the feature-test macro that C99 needs for POSIX (see the Makefile).
    local tidy_flags="-std=c99 -Icli"
    if [ "$(uname -s)" == "Linux" ]; then
        tidy_flags="$tidy_flags -D_DEFAULT_SOURCE"
    fi

    while IFS= read -r -d '' f; do
        echo -e "\033[0;36mChecking: $f\033[0m"
        if ! "$CLANG_TIDY_EXE" "$f" $extra_flags -- $tidy_flags; then
            failed=1
        fi
    done < <(find $SOURCE_DIRS -name "*.c" -print0)

    return $failed
}

STATUS=0
if [ "$NEED_FORMAT" == "1" ]; then
    do_format || STATUS=1
fi
if [ "$NEED_TIDY" == "1" ]; then
    do_check || STATUS=1
fi
exit $STATUS
