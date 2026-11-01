# OpenLibCLI — Meson subproject / wrap build file
# Installed into the consumer's subprojects/ directory so that
# dependency('OpenLibCLI') resolves automatically.

project(
    'OpenLibCLI',
    'c',
    version : files('version'),
    license : 'MIT',
    license_files : ['LICENSE'],
    default_options : ['warning_level=3', 'c_std=c99'],
)

# c99 makes glibc / musl hide POSIX: without it the default time source loses
# CLOCK_MONOTONIC and falls back to the wall clock.
if host_machine.system() == 'linux'
  add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')
endif

# -DCLI_SHARED -DBUILD_LIB_SHARED: OPENLIBCLI_API becomes __declspec(dllexport) on Windows;
# without exported symbols MSVC produces no import library (as in the top-level build).
openlibcli_lib = library(
    meson.project_name(),
    'cli/cli.c',
    'cli/utils/cli_args_parse.c',
    include_directories : include_directories('cli'),
    c_args : ['-DCLI_SHARED', '-DBUILD_LIB_SHARED'],
    install : true,
)

# ── Install headers (preserving subdirs like 'config') ─────────────────────
install_subdir('cli',
    install_dir : 'include/OpenLibCLI',
    strip_directory : true
)

openlibcli_dep = declare_dependency(
    include_directories : ['cli'],
    link_with : openlibcli_lib,
    version : meson.project_version(),
)

meson.override_dependency(meson.project_name(), openlibcli_dep)
