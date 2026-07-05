# OpenLibCLI Library

A command-line interface library written in **pure C99** with **100 % static memory allocation**. Runs on Windows, Linux/macOS, and MCUs (AVR, ARM, RISC-V) with any byte-stream transport (Telnet, TCP, serial, pipes, UNIX sockets, or custom).

## Table of Contents

- [OpenLibCLI Library](#openlibcli-library)
  - [Table of Contents](#table-of-contents)
  - [Features](#features)
  - [Use Cases](#use-cases)
  - [Coding Standards](#coding-standards)
  - [Demo](#demo)
  - [Repository Layout](#repository-layout)
  - [Quick Start](#quick-start)
    - [1 — Serial CLI (stdin/stdout)](#1--serial-cli-stdinstdout)
    - [2 — Telnet CLI server](#2--telnet-cli-server)
    - [3 — Embedded (STM32 / ESP32 / RPI PICO / AVR / ARDUINO)](#3--embedded-stm32--esp32--rpi-pico--avr--arduino)
  - [Building](#building)
    - [Windows — automatic (recommended)](#windows--automatic-recommended)
    - [Linux / macOS — automatic](#linux--macos--automatic)
    - [GNU Make (GCC / MinGW / Clang)](#gnu-make-gcc--mingw--clang)
    - [MSVC / NMake](#msvc--nmake)
    - [CMake](#cmake)
  - [Transport Layer](#transport-layer)
    - [Built-in transports](#built-in-transports)
    - [Custom transport](#custom-transport)
  - [Command Registration](#command-registration)
    - [Command handler signature](#command-handler-signature)
  - [Privilege Levels and Modes](#privilege-levels-and-modes)
    - [Privilege levels](#privilege-levels)
    - [Built-in modes](#built-in-modes)
    - [Custom modes](#custom-modes)
  - [Output and Pipe Filters](#output-and-pipe-filters)
  - [Authentication](#authentication)
  - [Non-Blocking / Embedded Usage](#non-blocking--embedded-usage)
  - [Programmatic Command Execution](#programmatic-command-execution)
    - [`cli_exec` — Execute a command string](#cli_exec--execute-a-command-string)
    - [`cli_exec_argv` — Execute pre-parsed arguments](#cli_exec_argv--execute-pre-parsed-arguments)
    - [No-op transport (silent execution)](#no-op-transport-silent-execution)
    - [Capturing output](#capturing-output)
  - [Built-in Commands](#built-in-commands)
  - [API Reference](#api-reference)
  - [Platform Notes](#platform-notes)
    - [Linux / macOS](#linux--macos)
    - [Windows (MinGW)](#windows-mingw)
    - [Windows (MSVC)](#windows-msvc)
    - [Bare-metal / RTOS embedded](#bare-metal--rtos-embedded)
  - [Contributions](#contributions)
  - [License](#license)
  - [Author](#author)

---

## Features

| Feature | Detail |
|---|---|
| **Pure C99** | Written in C99 standard. |
| **Coding Standards** | MISRA 2012 + MISRA 2023 amendments, verified via PVS-Studio static analysis. |
| **Static allocation** | Every buffer is a compile-time fixed array. No heap. |
| **Multi-platform** | Works on Windows, Linux/macOS, and Microcontrollers platforms running bare-metal or RTOS. |
| **Transport-agnostic** | Plug in Telnet, raw TCP, serial, pipe, or any byte stream. |
| **Per-session isolation** | Zero global mutable state, safe for multiple independent sessions. |
| **Hierarchical commands** | Unlimited nesting depth, privilege levels, per-mode visibility. |
| **Tab completion** | Unique match auto-completes; multiple matches lists possibilities. |
| **Inline `?` help** | Context-sensitive help at any point in a partial command. |
| **History** | Arrow key / Ctrl-P/N navigation over a configurable ring buffer. Consecutive duplicate filtering. |
| **VT100 line editing** | Ctrl-A/E/B/F/K/W/U/L/D plus Home/End/Delete escape sequences. |
| **Pipe filters** | `\| grep`, `\| exclude`, `\| begin`, `\| count` applied to any command output. If enabled. |
| **Authentication** | local user table or custom callbacks, Cisco-style `enable` password for privilege escalation, configurable auth failure policy (lockout/close). |
| **Banner / MOTD** | Displayed at session start before the login prompt. |
| **Programmatic execution** | `cli_exec()` / `cli_exec_argv()` can be used to run commands directly with or without a transport. Pair with a no-op write callback for silent or captured execution. |

---

## Use Cases

| Use Case | Description | Transport | Example |
|---|---|---|---|
| **Host server (Windows/Linux/macOS)** | Telnet, TCP, serial, pipe, or UNIX socket CLI for remote shell access, runtime config, network device management, IPC, and test automation | Telnet / TCP / Serial / Pipe / UNIX socket | `examples/win_linux/` — telnet, tcp, serial, pipe; `examples/linux/unix_socket/` |
| **Embedded / RTOS shell** | UART or Telnet CLI on MCUs using bare-metal or RTOS (FreeRTOS, Zephyr e.t.c)  for remote shell access, runtime config, diagnostics, monitoring, and control | Serial (UART) / Telnet | `OpenLibCLI-Embedded-Demos/` |

---

## Coding Standards

The library targets **MISRA 2012** with **MISRA 2023 amendments** and is verified with **PVS-Studio** static analysis. Key compliance areas:

| Area | Approach |
|------|----------|
| **No dynamic memory** | 100 % static allocation — no `malloc`/`free`/`realloc`. All buffers are compile-time fixed arrays. |
| **Single return / single break** | Functions prefer single exit points; loops use at most one `break`. |
| **No side effects in the right operand of `&&` or `\|\|`** | Move function calls and variable modifications to the left operand. |
| **Formatted I/O wrappers** | `cli_snprintf` / `cli_vsnprintf` / `cli_sscanf` abstract away direct `*printf`/`*scanf` usage. |
| **Strict compiler flags** | Builds clean with `-std=c99 -Wall -Wextra -Werror`. |
| **Static analysis** | PVS-Studio scans run regularly; all warnings reviewed and either fixed or suppressed. |

The following PVS-Studio diagnostics are suppressed with `//-V::` annottions in the source code:

| Rule | Count | Reason |
|------|-------|--------|
| `V1051` | 1 | False positive — `history_last` is compared character-by-character against the new line; PVS suggests `history_head` should be checked instead. The comparison is correct as-is. |
| `V2565` | 1 | Recursion in `cli_remove_command_recursive`. |
| `V2600` | 2 | `cli_snprintf` / `cli_sscanf` wrappers — these intentionally call `snprintf`/`sscanf`. |
| `V2600` | 3 | AVR `vsnprintf_P` calls — `P` variant is required on AVR for flash-string access, but PVS flags it the same as `snprintf`. Suppressed with `//-V:vsnprintf_P:2600`. |
| `V2604` | 19 | Variadic functions — `va_start`/`va_end` usage is required for `printf`-style formatting and is correctly paired across all paths. |

---

## Demo

```
************************************************************
*              OpenLibCLI  --  Telnet Transport            *
*         Pure-C, 100% Static Allocation CLI Engine        *
************************************************************

Username: guest
Password:

router> ?
  exit                Exit session
  quit                Quit session
  disable             Leave Privileged mode
  configure           Config mode
  help                Show commands
  history             Command history
  clear               Clear screen
  usage               Usage information
  show                Show system information
  network             Network commands
  reboot              Reload / reboot the system

router> show version
OpenLibCLI Library version: <version>
Platforms   : Linux / macOS / Windows / Embedded
Transports  : Telnet, TCP, Serial (UART, stdin/stdout), pipe

router> show interfaces
Interface GigE0/0  status up
Interface GigE0/1  status down
Interface GigE0/2  status up

router> network ping 8.8.8.8
Sending 5, 100-byte pings to 8.8.8.8:
Reply from 8.8.8.8: bytes=100 time<1ms TTL=64
Reply from 8.8.8.8: bytes=100 time<1ms TTL=64
Reply from 8.8.8.8: bytes=100 time<1ms TTL=64
Reply from 8.8.8.8: bytes=100 time<1ms TTL=64
Reply from 8.8.8.8: bytes=100 time<1ms TTL=64
Ping statistics: 5 sent, 5 received, 0% loss

router> enable
Password:
router# configure
router(config)# hostname myrouter
Hostname set to: myrouter
myrouter(config)# end
myrouter# show interfaces
Interface GigE0/0  status up
Interface GigE0/1  status down
Interface GigE0/2  status up

myrouter# exit
```

---

## Repository Layout

```
OpenLibCLI/
├── cli/
│   ├── cli.h               Public API — the only header your app needs
│   ├── cli_version.h       Version macros
│   ├── cli.c               Core CLI engine
│   ├── cli_env_detect.h    Platform detection macros
│   └── config/             Compile-time tuning (cli_config.h)
|   └── utils/             
├── Docs/
├── examples/
│   ├── win_linux/          Windows + Linux demo servers
│   ├── linux/              Linux-only demo servers
│   ├── shared/             Shared demo config and commands
├── examples-embedded/      Embedded helper sources
├── cmake/
├── meson/
├── Makefile                GNU Make  (GCC / MinGW / Clang)
├── Makefile.nmake          NMake     (MSVC cl.exe)
├── CMakeLists.txt          CMake     (all platforms)
├── scripts/
    ├── build-helpers
    ├── diagnostics
    ├── lint
    ├── package-helpers
├── tests/
```

---

## Quick Start

| Role | Username | Password |
|---|---|---|
| Admin | `admin` | `admin` |
| Guest | `guest` | `guest` |
| Enable secret | — | `cisco` |

### 1 — Serial CLI (stdin/stdout)

Uses `cli_serial_init()` to set up stdin/stdout transport, registers commands via `demo_register_commands()`, and drives the session with `cli_server_loop()`. See [`examples/win_linux/serial/cli_server_serial.c`](examples/win_linux/serial/cli_server_serial.c).

### 2 — Telnet CLI server

Uses `cli_telnet_init()` and `cli_telnet_negotiate()` to set up telnet transport, registers commands via `demo_register_commands()`, and drives the session with `cli_server_loop()`.
Listens on TCP port **2323** by default, accepts one client at a time, and serves a CLI session with telnet negotiation and authentication. See [`examples/win_linux/telnet/cli_server_telnet.c`](examples/win_linux/telnet/cli_server_telnet.c).

### 3 — Embedded (STM32 / ESP32 / RPI PICO / AVR / ARDUINO)

See the [`examples-embedded/`](examples-embedded) directory for microcontrollers demos:

- **STM32-NUCLEO-F103RB** — [`OpenLibCLI-Embedded-Demos/STM32/serial/`](https://github.com/thesourcebuild/OpenLibCLI-Embedded-Demos/tree/main/STM32/serial/) — STM32CubeIDE\Keil\IAR EW ARM project with USART2 transport
- **Arduino** — [`OpenLibCLI-Embedded-Demos/Arduino/`](https://github.com/thesourcebuild/OpenLibCLI-Embedded-Demos/tree/main/Arduino/) — Arduino IDE ready sketch.

  ```
  OpenLibCLI-Embedded-Demos/Arduino/OpenLibCLI/
  ├── examples/
  │   ├── serial/
  │   └── telnet/
  ├── src/
  ├── keywords.txt
  ├── library.json
  ├── library.properties
  ├── platformio.ini
  └── README.adoc
  ```

  Copy or symlink the root `OpenLibCLI/` folder (which contains `examples, src and library.properties e.t.c`) into your Arduino libraries directory:

  | Platform | Path |
  |---|---|
  | Windows | `C:\Users\<user>\Documents\Arduino\libraries\OpenLibCLI\` |
  | Linux   | `~/Arduino/libraries/OpenLibCLI/` |

  After installing, open the Arduino IDE.

    1. Navigate to **File → Examples → OpenLibCLI → serial** or **telnet**.
    2. Select your board and COM port.
    3. Upload.
    4. Open Serial Monitor at `115200` baud.

---

## Building

See [`Docs/how_to_build.md`](Docs/how_to_build.md) for the full build guide covering all toolchains, compile-time tuning, and output layouts.

### Windows — automatic (recommended)

```bat
:: build everything  (auto-selects MSVC or MinGW)
scripts\build-helpers\build.bat     

:: build + run telnet server on port 2323
scripts\build-helpers\build.bat run-telnet 

:: build + run TCP server on port 2323
scripts\build-helpers\build.bat run-tcp

:: build + run serial demo (stdin/stdout simulation)
scripts\build-helpers\build.bat run-serial

scripts\build-helpers\build.bat clean

scripts\build-helpers\build.bat help
```

`scripts\build-helpers\build.bat` searches for `cl.exe` first (tries to locate and source `vcvars64.bat` automatically for VS 2019/2022 in all editions). Falls back to MinGW `gcc` if MSVC is not found.

### Linux / macOS — automatic

```bash
chmod +x scripts/build-helpers/build.sh

# build everything
./scripts/build-helpers/build.sh    

# build + run Telnet on custom port
./scripts/build-helpers/build.sh run-telnet 2323  

# build + run TCP server on custom port
./scripts/build-helpers/build.sh run-tcp 2323      

# build + run Serial server
./scripts/build-helpers/build.sh run-serial

./scripts/build-helpers/build.sh clean

./scripts/build-helpers/build.sh help
```

### GNU Make (GCC / MinGW / Clang)

```bash
# Linux / macOS
make
make run-telnet PORT=2323
make run-tcp PORT=2323
make run-serial
make clean
make distclean

# Windows (MinGW — must pass CC=gcc explicitly)
mingw32-make CC=gcc PLATFORM=windows
mingw32-make CC=gcc PLATFORM=windows run-serial
mingw32-make CC=gcc PLATFORM=windows run-telnet
mingw32-make CC=gcc PLATFORM=windows run-tcp
mingw32-make CC=gcc PLATFORM=windows clean

# Cross-compile for ARM bare-metal
make CC=arm-none-eabi-gcc lib

# Cross-compile for Windows from Linux
make CC=x86_64-w64-mingw32-gcc PLATFORM=windows
```

### MSVC / NMake

Open a **Visual Studio x64 Native Tools Command Prompt** (or run `vcvars64.bat`) then:

```bat
nmake /f Makefile.nmake              :: build all
nmake /f Makefile.nmake lib          :: library only
nmake /f Makefile.nmake telnet       :: telnet server example
nmake /f Makefile.nmake tcp          :: TCP server example
nmake /f Makefile.nmake uart         :: embedded serial example
nmake /f Makefile.nmake run-telnet   :: build + run telnet server
nmake /f Makefile.nmake run-tcp      :: build + run TCP server
for TCP run
nmake /f Makefile.nmake run-serial     :: build + run serial demo
nmake /f Makefile.nmake clean
nmake /f Makefile.nmake distclean
nmake /f Makefile.nmake help
```

### CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Common targets (depend on enabled options):
#   cli_static, cli_shared, cli_server_telnet, cli_server_tcp, cli_server_serial
# Output libraries: static + shared under `build/<platform>/libs/`
```

---

## Transport Layer

The engine never touches I/O directly. Everything goes through a `cli_transport_struct_t` vtable:

```c
typedef struct {
    /**< Runtime transport kind. */
    cli_transport_kind_enum_t kind;               

    /**< Check for at least one decoded input byte. */
    cli_transport_ret_t (*available)(void *ctx); 
    
    /**< Read one decoded input byte, or <0 on error. */
    cli_transport_ret_t (*read)(void *ctx);  
    
    /**< Write @p len bytes; returns bytes written, <0 on error. */
    cli_transport_ret_t (*write)(void *ctx, const uint8_t *buf,
                             cli_transport_buflen_t len); 
    
    /**< Flush send buffer; may be NULL. */
    cli_transport_ret_t (*flush)(void *ctx);              
    
    /**< Opaque context pointer passed verbatim to every call. */
    void *ctx; 
} cli_transport_struct_t;
```

### Built-in transports

| Transport | Init function | Header requirement |
|---|---|---|
| **Telnet** (RFC 854, IAC negotiation) | `cli_telnet_init()` + `cli_telnet_negotiate()` + `cli_telnet_listen()` / `cli_telnet_accept()` / `cli_telnet_close()` | POSIX / Winsock socket |
| **Raw TCP** | `cli_tcp_init()` + `cli_tcp_listen()` / `cli_tcp_accept()` | POSIX / Winsock socket |
| **Serial** / **POSIX fd** | `cli_serial_init()` / `cli_fd_init()` | Stdin/Stdout or POSIX fds |
| **Pipe** (named / anonymous) | `cli_pipe_init()` + `cli_named_pipe_listen()` / `cli_named_pipe_connect()` / `cli_anon_pipe_create()` | POSIX or Win32 |
| **UNIX socket** | `cli_unix_socket_init()` + `cli_unix_listen()` / `cli_unix_accept()` | POSIX only |

### Custom transport

Implement your required transport functions and fill the struct:

```c
static cli_transport_ret_t my_available(void *ctx)
{
    /* return >0 if data ready, 0 if none, <0 on error */
}
static cli_transport_ret_t my_read(void *ctx)
{
    /* return one decoded byte (0..255), or <0 on error */
}
static cli_transport_ret_t my_write(void *ctx, const uint8_t *buf, cli_transport_buflen_t len)
{
    /* write len bytes; return bytes sent, <0 on error */
}

cli_transport_struct_t tp = {
    .kind      = CLI_TRANSPORT_UNKNOWN,
    .available = my_available,
    .read      = my_read,
    .write     = my_write,
    .flush     = NULL,
    .ctx       = &my_context,
};
```

---

## Command Registration

Each session carries its own command pool passed to `cli_init()`. Register commands after initialising the session.

```c
#define CLI_MAX_COMMANDS 64

static cli_struct_t     s_cli;
static cli_cmd_struct_t s_cmd_pool[CLI_MAX_COMMANDS];

void setup_commands(void)
{
    cli_init(&s_cli, "host", &transport, NULL, s_cmd_pool, CLI_MAX_COMMANDS);

    // Root command (no parent)
    cli_cmd_handle_t h_show =
        cli_register_command(&s_cli, CLI_CMD_ROOT,
            "show", NULL,
            CLI_PRIV_USER, CLI_MODE_ANY,
            "Show system information");

    // Sub-command of "show"
    cli_register_command(&s_cli, h_show,
        "version", cmd_show_version,
        CLI_PRIV_USER, CLI_MODE_ANY,
        "Show software version");

    // Privileged command only visible in config mode
    cli_register_command(&s_cli, CLI_CMD_ROOT,
        "hostname", cmd_set_hostname,
        CLI_PRIV_PRIVILEGED, CLI_MODE_CONFIG,
        "Set system hostname");
}
```

### Command handler signature

```c
static int8_t my_handler(cli_struct_t *cli, const char *cmd,
                         uint16_t argc, const char *argv[])
{
    // argv[0] = matched command word
    // argv[1..argc-1] = remaining tokens typed by user
    cli_println(cli, "Result: %s", argv[1]);
    return CLI_OK;   // or CLI_ERR
}
```

---

## Privilege Levels and Modes

### Privilege levels

| Constant | Value | Meaning |
|---|---|---|
| `CLI_PRIV_UNPRIVILEGED` | 0 | Unprivileged (default on connect) |
| `CLI_PRIV_USER` | 0 | Alias for `CLI_PRIV_UNPRIVILEGED` |
| `CLI_PRIV_PRIVILEGED` | 10 | Privileged (`enable` mode) |
| `CLI_PRIV_SUPERADMIN` | 15 | Super-admin mode |

### Built-in modes

| Constant | Value | Prompt style |
|---|---|---|
| `CLI_MODE_ANY` | 0 | Wildcard — matches all modes |
| `CLI_MODE_EXEC` | 1 | `router>` |
| `CLI_MODE_ENABLE` | 2 | `router#` |
| `CLI_MODE_CONFIG` | 3 | `router(config)#` |
| `CLI_MODE_USER_BASE` | 20 | First user-defined mode |

### Custom modes

```c
#define MODE_INTERFACE  (CLI_MODE_USER_BASE + 0)
#define MODE_ROUTE_MAP  (CLI_MODE_USER_BASE + 1)

cli_set_mode_name(cli, MODE_INTERFACE, "config-if");
// prompt → router(config-if)#

cli_set_mode(cli, MODE_INTERFACE);
```

---

## Output and Pipe Filters

Use `cli_println()` inside command handlers. Output is automatically passed through any active pipe filter the user appended to the command.

```c
// In a command handler:
cli_println(cli, "Interface GigE0/0  status up");
cli_println(cli, "Interface GigE0/1  status down");
cli_println(cli, "Interface GigE0/2  status up");
```

User types:

```
router# show interfaces | grep down
Interface GigE0/1  status down

router# show interfaces | count
Count: 3

router# show interfaces | begin GigE0/1
Interface GigE0/1  status down
Interface GigE0/2  status up

router# show interfaces | exclude up
Interface GigE0/1  status down
```

---

## Authentication

```c
// Option A — local table (up to CLI_MAX_USERS entries)
cli_add_user(cli, "admin", "admin");
cli_add_user(cli, "guest", "guest");
cli_require_authorization(cli, true);

// Option B — custom callback (overrides the local table)
static int my_auth(const char *user, const char *pass)
{
    if (strcmp(user, "admin") == 0 && strcmp(pass, getenv("CLI_PASS")) == 0)
        return CLI_OK;
    return CLI_ERR_AUTH;
}
cli_set_authorization_cb(cli, my_auth);
cli_require_authorization(cli, true);

// Enable secret (for 'enable' privilege escalation)
cli_set_enable_secret(cli, "cisco");
```

---

## Non-Blocking / Embedded Usage

For bare-metal MCUs or RTOS tasks, call `cli_session_engine()` in a bare-metal super-loop or RTOS task/thread loop:

```c
// RTOS task (or superloop)
void cli_task(void *arg)
{
    cli_struct_t *cli = (cli_struct_t *)arg;

    cli_start(cli);
    while (1) {
        if (cli_session_engine(cli) != CLI_OK) {
            cli_done(cli);
            cli_start(cli); // restart the CLI engine
        }
        // Other superloop work here ...
        cli_sleep_ms(1000);
    }
}
```

`cli_session_engine()` can be non-blocking depending on the underlying transport layer and is re-entrant per session (each `cli_struct_t` is independent).

---

## Programmatic Command Execution

Execute commands directly through the API without feeding bytes through a transport. Output is still sent through `transport->write()` — use a no-op transport (shown below) to discard or capture it.

### `cli_exec` — Execute a command string

```c
int8_t result = cli_exec(&s_cli, "show version");
if (result == CLI_OK) {
    // Command executed successfully
}
```

Copies the string into the internal input buffer, runs the full pipeline (trim, tokenize, resolve, callback), and resets the buffer.

### `cli_exec_argv` — Execute pre-parsed arguments

```c
const char *args[] = {"show", "version"};
int8_t result = cli_exec_argv(&s_cli, args, 2);
```

Joins `argv[0..argc-1]` with spaces and calls `cli_exec()`. A thin convenience wrapper when you already have tokenized input.

### No-op transport (silent execution)

```c
static cli_transport_ret_t nop_available(void *ctx) { (void)ctx; return 0; }
static cli_transport_ret_t nop_read(void *ctx)      { (void)ctx; return -1; }
static cli_transport_ret_t nop_write(void *ctx, const uint8_t *buf,
                                     cli_transport_buflen_t len) {
    (void)ctx; (void)buf; return len;
}
static cli_transport_ret_t nop_flush(void *ctx)     { (void)ctx; return 0; }

cli_transport_struct_t nop_tp = {
    .kind      = CLI_TRANSPORT_UNKNOWN,
    .available = nop_available,
    .read      = nop_read,
    .write     = nop_write,
    .flush     = nop_flush,
    .ctx       = NULL,
};
cli_init(&s_cli, "host", &nop_tp, NULL, s_cmd_pool, CLI_MAX_COMMANDS);
cli_add_builtin_cmds(&s_cli);
cli_exec(&s_cli, "show version");   // runs silently
```

### Capturing output

Replace `write` with a callback that appends to a buffer:

```c
typedef struct {
    char buf[4096];
    size_t len;
} capture_ctx_t;

static cli_transport_ret_t cap_write(void *ctx, const uint8_t *data,
                                     cli_transport_buflen_t len) {
    capture_ctx_t *cap = (capture_ctx_t *)ctx;
    size_t n = (len < (sizeof(cap->buf) - cap->len)) ? (size_t)len
                                                      : (sizeof(cap->buf) - cap->len);
    memcpy(&cap->buf[cap->len], data, n);
    cap->len += n;
    return len;
}
```

---

## Built-in Commands

These are registered by calling `cli_add_builtin_cmds(cli)`:

| Command | Mode | Privilege | Action |
|---|---|---|---|
| `exit` | any | user | Leave current mode; quit if already in exec |
| `quit` | any | user | End the session |
| `enable` | exec | user | Enter privileged mode (prompts for secret if set) |
| `disable` | enable | privileged | Return to user exec mode |
| `configure` | enable | privileged | Enter global config mode |
| `end` | config | privileged | Return to privileged exec mode |
| `help` | any | user | Show available commands |
| `usage` | any | user | Usage and quick-start topics (submenu) |
| `usage commands` | any | user | Show common command usage |
| `usage keys` | any | user | Show line-edit key bindings |
| `history` | any | user | Display command history |
| `clear` | any | user | Clear terminal screen |

---

## API Reference

See [`cli/cli.h`](cli/cli.h) for the full API reference including lifecycle, configuration, authentication, command registration, running, output, mode/privilege, and return codes.

---

## Platform Notes

### Linux / macOS

No special setup needed. GCC and Clang both work out of the box.

### Windows (MinGW)

- Use `mingw32-make CC=gcc PLATFORM=windows` or `scripts\build-helpers\build.bat`
- `-lws2_32` is linked automatically by the build scripts
- The `#pragma comment(lib, "ws2_32.lib")` pragmas are `_MSC_VER`-only and are silently ignored by GCC

### Windows (MSVC)

- Open a **Visual Studio x64 Native Tools Command Prompt** or let `scripts\build-helpers\build.bat` source `vcvars64.bat` automatically
- Uses `nmake /f Makefile.nmake`
- Compiles with `/std:c11 /W4 /WX`

### Bare-metal / RTOS embedded

- Include `cli/cli.h` and `cli/cli.c` in your project
- Provide a `cli_cmd_struct_t` pool and a transport vtable before calling `cli_init()`
- Call `cli_start()` + `cli_session_engine()` in a superloop, or run `cli_session_engine()` in a RTOS task loop
- Tune `CLI_MAX_*` macros down to fit your RAM budget
- See [`examples-embedded/`](examples-embedded) for MCU demo projects

---

## Contributions

Contributions of all sizes are warmly welcome!. Please feel free to:

- Report issues using [the issue guide](Docs/create_a_issue.md)
- Submit pull requests
- Improve documentation
- Suggest new features
- Start a discussion

Let's make the library better for everyone.

---

## License

MIT License — see ['LICENSE'](LICENSE) file and source file headers.

---

## Author

Muhammad Hassaan Shah

- GitHub: [@thesourcebuild](https://github.com/thesourcebuild)
- Project: [github.com/thesourcebuild/OpenLibCLI](https://github.com/thesourcebuild/OpenLibCLI)
