#!/usr/bin/env python3
"""
Generate cli/cli_version.h from the version file and (optionally) git describe.

Usage:
    python scripts/gen_version_header.py          # uses version file + git
    python scripts/gen_version_header.py --no-git # version file only

Reads <project-root>/version (format "X.Y.Z"), parses numeric components,
optionally tries git describe for the version string, and writes
<project-root>/cli/cli_version.h.
"""

import argparse
import os
import subprocess
import sys
import time


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
VERSION_FILE = os.path.join(PROJECT_ROOT, "version")
OUTPUT_FILE = os.path.join(PROJECT_ROOT, "cli", "cli_version.h")
LIBRARY_PROPS = os.path.join(PROJECT_ROOT, "library.properties")


def parse_version_file(path):
    with open(path, "r") as f:
        line = f.readline().strip()
    parts = line.split(".")
    if len(parts) != 3:
        print(f"error: version file '{path}' does not contain <major>.<minor>.<revision>", file=sys.stderr)
        print(f"  got: '{line}'", file=sys.stderr)
        sys.exit(1)
    major, minor, revision = parts
    if not (major.isdigit() and minor.isdigit() and revision.isdigit()):
        print(f"error: version components must be numeric, got '{line}'", file=sys.stderr)
        sys.exit(1)
    return int(major), int(minor), int(revision)


def try_git_describe():
    git_dir = os.path.join(PROJECT_ROOT, ".git")
    if not os.path.isdir(git_dir):
        return None
    try:
        result = subprocess.run(
            ["git", "describe", "--tags", "--dirty", "--always"],
            capture_output=True, text=True, cwd=PROJECT_ROOT, timeout=10,
        )
        if result.returncode == 0:
            desc = result.stdout.strip()
            return desc if desc else None
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    return None


def update_library_properties(version_str):
    if not os.path.exists(LIBRARY_PROPS):
        return
    with open(LIBRARY_PROPS, "r") as f:
        lines = f.readlines()
    changed = False
    for i, line in enumerate(lines):
        if line.startswith("version="):
            old = line.strip()
            lines[i] = f"version={version_str}\n"
            if lines[i] != old:
                changed = True
            break
    if changed:
        with open(LIBRARY_PROPS, "w", newline="\n", encoding="utf-8") as f:
            f.writelines(lines)
        print(f"  Updated  : library.properties -> version={version_str}")


def generate_header(major, minor, revision, git_desc):
    now = time.localtime()
    year = now.tm_year

    version_source = "version file"
    version_str = f"{major}.{minor}.{revision}"
    if git_desc:
        version_source += " + git describe"
        version_str = git_desc

    lines = [
        "/**",
        " * @file cli_version.h",
        " * @brief Version information for the OpenLibCLI Library.",
        " *",
        " * AUTO-GENERATED — do not edit manually.",
        f" * Source: {version_source}",
        " *",
        " * Defines the library version as individual numeric components, as a packed",
        " * 32-bit word, and as a human-readable string.",
        " *",
        f" * @copyright Copyright (c) {year} Muhammad Hassaan Shah",
        " *",
        " * SPDX-License-Identifier: MIT",
        " */",
        "",
        "#ifndef OPENLIBCLI_VERSION_H",
        "#define OPENLIBCLI_VERSION_H",
        "",
        "/* C++ detection */",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "/*=======================================================================================",
        " * Includes",
        " *=======================================================================================*/",
        "",
        "/* No public includes required. */",
        "",
        "/*=======================================================================================",
        " * Public Defines",
        " *=======================================================================================*/",
        "",
        "/** @defgroup CLI_Version_Defines Version Defines",
        " *  @{",
        " */",
        "",
        "/** @brief Major version number. Incremented on breaking API changes. */",
        f"#define CLI_VERSION_MAJOR {major}",
        "",
        "/** @brief Minor version number. Incremented on backward-compatible additions. */",
        f"#define CLI_VERSION_MINOR {minor}",
        "",
        "/** @brief Revision / patch number. Incremented on bug-fix releases. */",
        f"#define CLI_VERSION_REVISION {revision}",
        "",
        "/**",
        " * @brief Packed version word: 0x00MMmmrr (major, minor, revision).",
        " *",
        " * Useful for compile-time comparisons:",
        " * @code",
        " *   #if CLI_VERSION >= 0x00010000",
        " * @endcode",
        " */",
        "#define CLI_VERSION ((CLI_VERSION_MAJOR << 16) | (CLI_VERSION_MINOR << 8) | CLI_VERSION_REVISION)",
        "",
        "/** @} */",
        "",
        "/*=======================================================================================",
        " * Public Macros",
        " *=======================================================================================*/",
        "",
        "/** @defgroup CLI_Version_Macros Version Macros",
        " *  @{",
        " */",
        "",
        "/**",
        " * @brief Stringify helper (step 1 of 2).",
        " * @param v Token to convert to a string literal.",
        " */",
        "#define CLI_VERSION_STR(v) #v",
        "",
        "/**",
        " * @brief Expand-then-stringify helper (step 2 of 2).",
        " * @param v Token to expand before stringification.",
        " */",
        "#define CLI_VERSION_XSTR(v) CLI_VERSION_STR(v)",
        "",
        "/**",
        f" * @brief Human-readable version string, for example @c \"{version_str}\".",
        " *",
        " * Built from the individual numeric components via the stringify helpers.",
        " */",
        "#define CLI_VERSION_STRING                                                                         \\",
        "  CLI_VERSION_XSTR(CLI_VERSION_MAJOR)                                                              \\",
        '  "." CLI_VERSION_XSTR(CLI_VERSION_MINOR) "." CLI_VERSION_XSTR(CLI_VERSION_REVISION)',
        "",
        "/** @} */",
        "",
        "/*=======================================================================================",
        " * Public Types",
        " *=======================================================================================*/",
        "",
        "/** @defgroup CLI_Version_Types Version Types",
        " *  @{",
        " */",
        "",
        "/* No public types declared in this header. */",
        "",
        "/** @} */",
        "",
        "/*=======================================================================================",
        " * External Data Variables",
        " *=======================================================================================*/",
        "",
        "/** @defgroup CLI_Version_ExternVars Version External Variables",
        " *  @{",
        " */",
        "",
        "/* No public external data variables. */",
        "",
        "/** @} */",
        "",
        "/*=======================================================================================",
        " * Public Function Prototypes",
        " *=======================================================================================*/",
        "",
        "/** @defgroup CLI_Version_Functions Version Public Functions",
        " *  @{",
        " */",
        "",
        "/* No public functions declared in this header. */",
        "",
        "/** @} */",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif /* OPENLIBCLI_VERSION_H */",
        "",
        "/*################################### END OF FILE ######################################*/",
        "",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate cli/cli_version.h")
    parser.add_argument("--no-git", action="store_true", help="Skip git describe")
    args = parser.parse_args()

    if not os.path.exists(VERSION_FILE):
        print(f"error: version file not found at '{VERSION_FILE}'", file=sys.stderr)
        sys.exit(1)

    major, minor, revision = parse_version_file(VERSION_FILE)
    git_desc = None if args.no_git else try_git_describe()

    content = generate_header(major, minor, revision, git_desc)

    with open(OUTPUT_FILE, "w", newline="\n", encoding="utf-8") as f:
        f.write(content)
    print(f"[gen_version_header] Generated {OUTPUT_FILE}")

    version_str = f"{major}.{minor}.{revision}"
    update_library_properties(version_str)

    if git_desc:
        print(f"  Version string : {git_desc}")
    else:
        print(f"  Version string : {version_str}")
    if git_desc:
        print(f"  Source         : version file + git describe")
    else:
        print(f"  Source         : version file")


if __name__ == "__main__":
    main()
