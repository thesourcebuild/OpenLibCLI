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

openlibcli_lib = library(
    meson.project_name(),
    'cli/cli.c',
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
