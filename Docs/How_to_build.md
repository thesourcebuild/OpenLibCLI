# How to Build

## Table of Contents

- [Compile-time tuning](#compile-time-tuning-cli_configh)
- [Build variables](#build-variables)
- [Build from Packages](#build-from-packages)
- [Build using Build-Helpers scripts](#build-using-build-helpers-scripts)

## Compile-time tuning (`cli_config.h`)

All `CLI_*` constants in [`cli/config/cli_config.h`](cli/config/cli_config.h) use `#ifndef` guards, so any build system can override them via `-D` flags. The defaults target full-OS desktop usage.

See [`cli/config/cli_config.h`](cli/config/cli_config.h) for full list of tunables with defaults.

For embedded targets, copy `cli/config/cli_embedded_config.h` to `cli_config.h` on your include path. Alternatively, define `CLI_BUILD_CONFIG_HEADER_ENABLED=1` and set `CLI_CONFIG_HEADER_PATH` for a fully custom config path.

See [`cli/config/cli_config.h`](cli/config/cli_config.h) for the complete list of ~30 options.

## Build variables

| Variable | Default | Description |
|---|---|---|
| `BUILD_EXAMPLE_TELNET=0\|1` | 1 | Telnet transport + example server |
| `BUILD_EXAMPLE_TCP=0\|1` | 0 | TCP transport + example server |
| `BUILD_EXAMPLE_SERIAL=0\|1` | 1 | Serial transport + example server |
| `BUILD_EXAMPLE_PIPE=0\|1` | 0 | Pipe transport + example server |
| `BUILD_EXAMPLE_UNIX_SOCKET=0\|1` | 0 | UNIX socket example (POSIX only) |
| `BUILD_LIB_STATIC=0\|1` | 0 | Build static library in addition to shared |
| `BUILD_LIB_SHARED=0\|1` | 1 | Build shared library |

Passing any `BUILD_EXAMPLE_*` explicitly disables all other examples not mentioned (opt-in model).

## Build from Packages

Pre-built packages are available as release artifacts. Each package ships the library sources, headers, config files, and reference examples (serial + telnet) ready to build.

### Option 1 — Source Drop-in Package

Download a `OpenLibCLI-<version>-src` package from Releases.

Copy `cli/` into your project and include the source files in your build:

- Add `cli/cli.c` to your compile targets.
- Add `cli/` to your include path.
- Optionally pick `cli/config/cli_config.h` (desktop) or `cli/config/cli_embedded_config.h` for (AVR, ARM, RISC-V etc.) then copy and rename to `cli/config/cli_config.h`.

---

### Option 2 — CMake Package (recommended CMake ≥ 3.14)

Download a `OpenLibCLI-<version>-cmake` package from Releases.

Specify the package location and build:

```bash
# Using CMAKE_PREFIX_PATH
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/OpenLibCLI-<version>-cmake
cmake --build build
```

Or set `openlibcli_DIR` to point at the package directly:

```bash
cmake -B build -Dopenlibcli_DIR=/path/to/OpenLibCLI-<version>-cmake/lib/cmake/OpenLibCLI
cmake --build build
```

#### Use in your project

```cmake
find_package(OpenLibCLI REQUIRED)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE OpenLibCLI::OpenLibCLI)
```

The `OpenLibCLI::OpenLibCLI` target carries all required platform dependencies (e.g., `ws2_32` on Windows) automatically.

#### Build the included examples

```bash
cd OpenLibCLI-<version>-cmake
cmake -S examples -B examples/build -DCMAKE_PREFIX_PATH=.
cmake --build examples/build
```

Run the examples:

```bash
# Linux / macOS
./examples/build/cli_server_serial
./examples/build/cli_server_telnet

# Windows (PowerShell)
.\examples\build\Debug\cli_server_serial.exe
.\examples\build\Debug\cli_server_telnet.exe
```

#### Custom configuration

Override compile-time limits at the consumer side:

```cmake
target_compile_definitions(OpenLibCLI PRIVATE
    CLI_MAX_HISTORY=10
)
```

We can also pre-include the config profile via the include path:

```cmake
target_include_directories(OpenLibCLI PRIVATE path/to/config/)
target_compile_definitions(OpenLibCLI PRIVATE CLI_BUILD_CONFIG_HEADER_ENABLED=1)
```

Where `path/to/config/` contains a `cli_config.h` with embedded-tuned values (e.g., reduced `CLI_MAX_HISTORY`). See `install/cli/config/cli_embedded_config.h` for a reference AVR/embedded profile.

Pool capacity (`CLI_MAX_COMMANDS`) is **not** defined by the library — it must be set by the application before including `cli.h`, or passed as a literal to `cli_init()`.

Or provide a user-defined config header at any path:

```cmake
target_compile_definitions(OpenLibCLI PRIVATE
    CLI_BUILD_CONFIG_HEADER_ENABLED=1
    CLI_CONFIG_HEADER_PATH="my_config/my_settings.h"
)
target_include_directories(OpenLibCLI PRIVATE /path/to/my_config)
```

When `CLI_BUILD_CONFIG_HEADER_ENABLED=1` is defined, the library includes the header specified by `CLI_CONFIG_HEADER_PATH` (defaults to `"cli_config.h"` in the include path) instead of the built-in config fallback logic. This is useful when the compiler lacks `__has_include` or the config header is at a non-standard location.

---

### Option 3 — Meson Subproject Package

Download a `OpenLibCLI-<version>-meson` package from Releases.

#### Direct build (library + optional examples)

```bash
cd OpenLibCLI-<version>-meson
meson setup build -Dbuild_examples=true
meson compile -C build
```

Run the examples:

```bash
# Linux / macOS
./build/cli_server_serial
./build/cli_server_telnet

# Windows (Command Prompt, PowerShell)
.\build\cli_server_serial.exe
.\build\cli_server_telnet.exe
```

Omit `-Dbuild_examples=true` to build the library only.

#### Use as a subproject in your project

Copy the package into `subprojects/`:

```bash
cp -r OpenLibCLI-<version>-meson /path/to/your-project/subprojects/OpenLibCLI
```

Or use a `.wrap` file (included in the release):

```ini
# subprojects/OpenLibCLI.wrap
[wrap-file]
directory = OpenLibCLI-<version>-meson
source_filename = OpenLibCLI-<version>-meson.tar.gz
source_url = https://github.com/.../releases/...
```

Then in your `meson.build`:

```meson
project('my_project', 'c')

openlibcli_dep = dependency('OpenLibCLI')

executable('my_app', 'main.c',
    dependencies: [openlibcli_dep],
)
```

---

## Build using Build-Helpers scripts

The `scripts/build-helpers/` directory contains build wrappers for all supported toolchains. Each builder supports common targets (`all`, `lib`, `shared`, `clean`, `clean-all`) and `BUILD_EXAMPLE_*` variables.

### Windows (native) — MSVC or MinGW

```cmd
:: Auto-detect MSVC or MinGW from PATH
scripts\build-helpers\build                                  (all targets)
scripts\build-helpers\build msvc                             (force MSVC)
scripts\build-helpers\build gcc                              (force MinGW GCC)
scripts\build-helpers\build cmake                            (CMake + Ninja/MSBuild)
scripts\build-helpers\build meson                            (Meson + Ninja)
```

Pass build variables and targets after the toolchain name:

```cmd
scripts\build-helpers\build gcc BUILD_EXAMPLE_TELNET=1 clean-all
```

Thin `.bat` → `.ps1` wrappers are also available for direct invocation:

```cmd
scripts\build-helpers\build_mingw_gcc BUILD_EXAMPLE_TELNET=1
scripts\build-helpers\build_cmake -G Ninja
scripts\build-helpers\build_meson build\meson
scripts\build-helpers\build_msvc
```

### Windows (WSL cross-compile) — Linux GCC toolchain via WSL

Cross-compile the Linux build from Windows using WSL Ubuntu:

```cmd
scripts\build-helpers\build_wsl                               (gcc, default)
scripts\build-helpers\build_wsl shared                        (gcc, DLL only)
scripts\build-helpers\build_wsl clean-all                     (gcc, clean)
scripts\build-helpers\build_wsl -Backend cmake                (cmake)
scripts\build-helpers\build_wsl -Backend meson                (meson)
scripts\build-helpers\build_wsl -WslDist Ubuntu-24.04 clean   (custom distro)
```

Build variables are set as environment variables:

```cmd
scripts\build-helpers\build_wsl BUILD_EXAMPLE_TELNET=1 BUILD_EXAMPLE_SERIAL=0
```

### Linux / macOS (native)

Use the shell scripts directly:

```bash
./scripts/build-helpers/build_mingw_gcc.sh                    # gcc, default
./scripts/build-helpers/build_mingw_gcc.sh shared             # gcc, DLL only
./scripts/build-helpers/build_cmake.sh                        # cmake
./scripts/build-helpers/build_meson.sh                        # meson

# With build variables
./scripts/build-helpers/build_mingw_gcc.sh BUILD_EXAMPLE_TELNET=1 
```

### Output directories

| Platform | Backend | Output |
|---|---|---|
| Windows | MSVC | `build/msvc/windows/{binaries,libs,obj}/` |
| Windows | MinGW GCC | `build/gcc/windows/{binaries,libs,obj}/` |
| Windows | CMake | `build/cmake/windows/{binaries,libs}/` |
| Windows | Meson | `build/meson/windows/{binaries,libs}/` |
| Linux | GCC | `build/gcc/linux/{binaries,libs,obj}/` |
| Linux | CMake | `build/cmake/linux/{binaries,libs}/` |
| Linux | Meson | `build/meson/linux/{binaries,libs}/` |
| macOS | GCC | `build/gcc/macos/{binaries,libs,obj}/` |
| macOS | CMake | `build/cmake/macos/{binaries,libs}/` |
| macOS | Meson | `build/meson/macos/{binaries,libs}/` |

---
