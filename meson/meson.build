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

# c99 hides POSIX: without it the default time source loses CLOCK_MONOTONIC and falls
# back to the wall clock.
if host_machine.system() != 'windows'
  add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')
endif

openlibcli_lib = library(
    meson.project_name(),
    'cli/cli.c',
    'cli/utils/cli_args_parse.c',
    include_directories : include_directories('cli'),
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
