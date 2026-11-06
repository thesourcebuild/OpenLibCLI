# OpenLibCLI — Meson build file of the source package (install/OpenLibCLI-<version>-meson/)
# The package scripts copy it into the package root; there the package is used as a
# subproject (dependency('OpenLibCLI')) or built on its own.
#
# Package layout:
#   include/OpenLibCLI/   public headers (config/, utils/)
#   src/                  library source tree
#   examples/             serial and telnet examples (-Dbuild_examples=true)

project(
    'OpenLibCLI',
    'c',
    version : files('version'),
    license : 'MIT',
    license_files : ['LICENSE'],
    default_options : ['warning_level=3', 'c_std=c99'],
    meson_version : '>=1.1.0',
)

# c99 makes glibc / musl hide POSIX: without it the default time source loses
# CLOCK_MONOTONIC and falls back to the wall clock.
if host_machine.system() == 'linux'
  add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')
endif

cc = meson.get_compiler('c')

# MSVC (and clang-cl) report the standard C functions used here (sscanf, ...) as deprecated
# in favour of their _s variants; the CMake and NMake builds turn that off the same way.
if cc.get_argument_syntax() == 'msvc'
  add_project_arguments('-D_CRT_SECURE_NO_WARNINGS', language : 'c')
endif

platform_deps = []
if host_machine.system() == 'windows'
  platform_deps += cc.find_library('ws2_32')
endif

# -DCLI_SHARED -DBUILD_LIB_SHARED: OPENLIBCLI_API becomes __declspec(dllexport) on Windows;
# without exported symbols MSVC produces no import library (as in the top-level build).
# A bare-metal target (host system 'none' in the cross file) has no shared libraries: there
# the library is always static, whatever default_library says.
lib_src = files('src/cli.c', 'src/utils/cli_args_parse.c')
lib_inc = include_directories('src')
if host_machine.system() == 'none'
  openlibcli_lib = static_library(
      meson.project_name(),
      lib_src,
      include_directories : lib_inc,
      dependencies : platform_deps,
      install : true,
  )
else
  openlibcli_lib = library(
      meson.project_name(),
      lib_src,
      include_directories : lib_inc,
      c_args : ['-DCLI_SHARED', '-DBUILD_LIB_SHARED'],
      dependencies : platform_deps,
      install : true,
  )
endif

openlibcli_dep = declare_dependency(
    include_directories : include_directories('include/OpenLibCLI'),
    link_with : openlibcli_lib,
    dependencies : platform_deps,
    version : meson.project_version(),
)

meson.override_dependency(meson.project_name(), openlibcli_dep)

# Public headers: include/OpenLibCLI/ (config/, utils/)
install_subdir('include/OpenLibCLI', install_dir : get_option('includedir'))

if get_option('build_examples')
  demo_src = files('examples/shared/cli_demo_setup.c')
  executable('cli_server_serial',
      'examples/win_linux/serial/cli_server_serial.c',
      'examples/win_linux/serial/cli_transport_serial.c',
      demo_src,
      dependencies : openlibcli_dep,
      install : false,
  )
  executable('cli_server_telnet',
      'examples/win_linux/telnet/cli_server_telnet.c',
      'examples/win_linux/telnet/cli_transport_telnet.c',
      demo_src,
      dependencies : openlibcli_dep,
      install : false,
  )
endif
