# OpenLibCLI — top-level Meson build
# Supports both direct build and subproject / wrap use.

project(
    'OpenLibCLI',
    'c',
    version : files('version'),
    license : 'MIT',
    license_files : ['LICENSE'],
    default_options : ['warning_level=3', 'c_std=c99'],
    meson_version : '>=1.1.0',
)

# ── Version header generation ────────────────────────────────────────────
py = import('python')
python_inst = py.find_installation('python3', required : false)
if python_inst.found()
  gen_result = run_command(python_inst, files('scripts/gen_version_header.py'), check : false)
  if gen_result.returncode() != 0
    message('gen_version_header: ' + gen_result.stderr().strip())
  else
    message('gen_version_header: regenerated cli/cli_version.h')
  endif
endif

# ── Sources & include dirs ────────────────────────────────────────────────
src = files(
    'cli/cli.c',
    'cli/utils/cli_args_parse.c',
)
inc = include_directories('cli')

# ── Platform helpers ──────────────────────────────────────────────────────
cc = meson.get_compiler('c')
is_windows = host_machine.system() == 'windows'
is_linux   = host_machine.system() == 'linux'

# ── POSIX feature test macro (nanosleep / timespec in cli_demo_setup.c) ──
if not is_windows
  add_project_arguments('-D_POSIX_C_SOURCE=199309L', language: 'c')
endif

# ── Library (both static + shared) ─────────────────────────────────────────
# The shared library needs -DCLI_SHARED -DBUILD_LIB_SHARED so that
# OPENLIBCLI_API in cli.h expands to __declspec(dllexport) on MSVC.
# Without exported symbols the MSVC linker skips IMPLIB generation.
openlibcli_lib = both_libraries(meson.project_name(), src,
    include_directories : inc,
    c_args : ['-DCLI_SHARED', '-DBUILD_LIB_SHARED'],
    install : true)

# ── Dependency object (for subproject / wrap consumers) ───────────────────
openlibcli_dep = declare_dependency(
    include_directories : inc,
    link_with           : openlibcli_lib,
    version             : meson.project_version(),
)

meson.override_dependency(meson.project_name(), openlibcli_dep)

if is_windows
  ws2_dep = cc.find_library('ws2_32', required : true)
endif

# ── Examples ──────────────────────────────────────────────────────────────
if get_option('BUILD_EXAMPLES')
  demo_src = files('examples/shared/cli_demo_setup.c')

  # Serial
  if get_option('BUILD_EXAMPLE_SERIAL')
    executable('cli_server_serial',
        'examples/win_linux/serial/cli_server_serial.c',
        'examples/win_linux/serial/cli_transport_serial.c',
        demo_src,
        include_directories : inc,
        link_with : openlibcli_lib,
        install : true,
    )
  endif

  # Telnet
  if get_option('BUILD_EXAMPLE_TELNET')
    telnet_deps = []
    if is_windows
      telnet_deps += ws2_dep
    endif
    executable('cli_server_telnet',
        'examples/win_linux/telnet/cli_server_telnet.c',
        'examples/win_linux/telnet/cli_transport_telnet.c',
        demo_src,
        include_directories : inc,
        link_with : openlibcli_lib,
        dependencies : telnet_deps,
        install : true,
    )
  endif

  # TCP
  if get_option('BUILD_EXAMPLE_TCP')
    tcp_deps = []
    if is_windows
      tcp_deps += ws2_dep
    endif
    executable('cli_server_tcp',
        'examples/win_linux/tcp/cli_server_tcp.c',
        'examples/win_linux/tcp/cli_transport_tcp.c',
        demo_src,
        include_directories : inc,
        link_with : openlibcli_lib,
        dependencies : tcp_deps,
        install : true,
    )
  endif

  # Pipe
  if get_option('BUILD_EXAMPLE_PIPE')
    pipe_deps = []
    if not is_windows
      pipe_deps += dependency('threads', required : true)
    endif
    executable('cli_server_pipe',
        'examples/win_linux/pipe/cli_server_pipe.c',
        'examples/win_linux/pipe/cli_transport_pipe.c',
        demo_src,
        include_directories : inc,
        link_with : openlibcli_lib,
        dependencies : pipe_deps,
        install : true,
    )
  endif

  # UNIX socket (Linux only)
  if is_linux and get_option('BUILD_EXAMPLE_UNIX_SOCKET')
    executable('cli_server_unix_socket',
        'examples/linux/unix_socket/cli_server_unix_socket.c',
        'examples/linux/unix_socket/cli_transport_unix_socket.c',
        demo_src,
        include_directories : inc,
        link_with : openlibcli_lib,
        dependencies : dependency('threads', required : true),
        install : true,
    )
  endif
endif
