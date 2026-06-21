#!/bin/bash

# =============================================================================
#  lint.sh — Code formatting and static analysis (Unix/macOS)
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

MODE="all"
if [ "$1" == "format" ]; then MODE="format"; fi
if [ "$1" == "check" ];  then MODE="check";  fi
if [ "$1" == "fix" ];    then MODE="fix";    fi
if [ "$1" == "help" ]; then
    echo ""
    echo " Usage: ./scripts/lint/lint.sh [format|check|fix|help]"
    echo ""
    echo " Commands:"
    echo "   (none)    Run both format and check"
    echo "   format    Run clang-format on all .c and .h files"
    echo "   check     Run clang-tidy on all .c files"
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

# Check for tools. Allow overriding by predefining CLANG_FORMAT_EXE/CLANG_TIDY_EXE.
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

if [ "$FAIL" == "1" ]; then exit 1; fi

do_format() {
    echo "[lint.sh] Formatting code with clang-format..."
    find . -maxdepth 3 -name "*.c" -o -name "*.h" | xargs "$CLANG_FORMAT_EXE" -i
}

do_check() {
    EXTRA_FLAGS=""
    if [ "$MODE" == "fix" ]; then
        echo "[lint.sh] Auto-fixing code with clang-tidy..."
        EXTRA_FLAGS="-fix-errors"
    else
        echo "[lint.sh] Analyzing code with clang-tidy..."
    fi

    TIDY_FLAGS="-Icli"
    
    # Run on src, transport, and examples
    FILES=$(find src transport examples -maxdepth 2 -name "*.c")
    for f in $FILES; do
        echo -e "\033[0;36mChecking: $(basename $f)\033[0m"
        "$CLANG_TIDY_EXE" "$f" $EXTRA_FLAGS -- $TIDY_FLAGS
    done
}

if [ "$MODE" == "all" ] || [ "$MODE" == "format" ]; then
    do_format
fi

if [ "$MODE" == "all" ] || [ "$MODE" == "check" ] || [ "$MODE" == "fix" ]; then
    do_check
fi
