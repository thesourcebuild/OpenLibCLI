# =============================================================================
#  OpenLibCLI Library — GNU Makefile  (GCC / MinGW / Clang)
#
#  Output layout
#  -------------
#    build/gcc/windows/   platform root
#    build/gcc/linux/     platform root
#    build/gcc/macos/     platform root
#    build/gcc/embedded-baremetal/  platform root
#
#  Targets
#  -------
#  all            Build the shared library and host examples [default]
#                 (BUILD_LIB_STATIC=1 adds the static library)
#  lib            Build static library only
#  shared         Build shared library only
#  cli_server_telnet  Build the telnet server example
#  cli_server_tcp     Build TCP server example
#  cli_server_serial  Build the embedded / Serial example
#  cli_server_pipe    Build the pipe server example
#  cli_server_unix_socket  Build the UNIX domain socket server example
#  embedded-baremetal  Cross-compile the library for bare-metal (set CC)
#  run-telnet     Build + run telnet server (PORT=2323)
#  run-tcp        Build + run TCP server (PORT=2323)
#  run-serial     Build + run embedded Serial demo
#  run-pipe       Build + run pipe demo
#  clean          Remove artefacts for current platform only
#  clean-all      Remove entire build/ tree
#  distclean      Alias for clean-all
#  help           Print this message
#
#  Toolchain overrides
#  -------------------
#  make CC=clang
#  make CC=arm-none-eabi-gcc PLATFORM=embedded-baremetal
#  make CC=x86_64-w64-mingw32-gcc PLATFORM=windows
#  make CC=arm-none-eabi-gcc PLATFORM=embedded-baremetal
#  make PORT=9999 run-telnet
#
#  For MSVC use:  nmake /f Makefile.nmake
#  Wrapper scripts:  build.bat (Windows)  build.sh (Unix)
# =============================================================================

# ---------------------------------------------------------------------------
#  Toolchain
#  NOTE: On Windows/MinGW 'cc' is not always in PATH — pass CC=gcc explicitly,
#  or use build.bat which does this automatically.
# ---------------------------------------------------------------------------
CC    ?= gcc
AR    ?= ar

# Recipe shell. Native Windows make (MAKE_HOST *-mingw32) runs recipes with any sh.exe it
# finds on PATH (Git, MSYS2, busybox) and cmd.exe otherwise; the Windows commands used
# below need cmd.exe, so it is selected explicitly. MSYS2 / Cygwin make are POSIX
# environments: sh with the POSIX commands, as on Linux.
ifneq ($(findstring mingw32,$(MAKE_HOST)),)
SHELL     := cmd.exe
CMD_SHELL := 1
endif

ifeq ($(CMD_SHELL),1)
    RM     = del /f /q
    RMDIR  = rd /s /q
    COPY   = copy /y
else
    RM    ?= rm -f
    RMDIR ?= rm -rf
    COPY   = cp -f
endif

# $(call say,text): print one line of text exactly (empty: a blank line). cmd.exe echoes
# quotes literally and treats | < > & ^ as operators, which are escaped with ^; "echo."
# prints an empty line (both are commands make runs through cmd.exe).
# $(call mkdir_p,dir): create a directory and its parents if missing; succeeds whenever the
# directory exists afterwards (a parallel job may have created it first).
ifeq ($(CMD_SHELL),1)
    say     = $(if $(1),echo $(subst >,^>,$(subst <,^<,$(subst |,^|,$(subst &,^&,$(subst ^,^^,$(1)))))),echo.)
    mkdir_p = (if not exist "$(subst /,\,$(1))" mkdir "$(subst /,\,$(1))") 2>nul & if exist "$(subst /,\,$(1))" (exit /b 0) else exit /b 1
else
    say     = printf '%s\n' '$(1)'
    mkdir_p = mkdir -p $(1)
endif

# ---------------------------------------------------------------------------
#  Source directories
# ---------------------------------------------------------------------------
SRC_DIR     := cli
INC_DIR     := cli
EXAMPLE_DIR := examples
BUILD_BASE  := build/gcc

# ---------------------------------------------------------------------------
#  Platform detection  (auto; override with PLATFORM=windows|linux|macos|embedded-baremetal)
# ---------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
    PLATFORM ?= windows
else
    _UNAME := $(shell uname -s 2>/dev/null || echo linux)
    ifeq ($(_UNAME),Darwin)
        PLATFORM ?= macos
    else ifneq ($(filter FreeBSD NetBSD OpenBSD DragonFly,$(_UNAME)),)
        PLATFORM ?= bsd
    else
        PLATFORM ?= linux
    endif
endif

# ---------------------------------------------------------------------------
#  Per-platform output directory and flags
# ---------------------------------------------------------------------------
ifeq ($(PLATFORM),windows)
    BUILD_DIR  := $(BUILD_BASE)/windows
    PLAT_LIBS  := -lws2_32
    EXE_EXT    := .exe
else ifeq ($(PLATFORM),embedded-baremetal)
    BUILD_DIR  := $(BUILD_BASE)/embedded-baremetal
    PLAT_LIBS  :=
    EXE_EXT    :=
else ifeq ($(PLATFORM),macos)
    BUILD_DIR  := $(BUILD_BASE)/macos
    PLAT_LIBS  :=
    EXE_EXT    :=
else ifeq ($(PLATFORM),bsd)
    BUILD_DIR  := $(BUILD_BASE)/bsd
    PLAT_LIBS  :=
    EXE_EXT    :=
else
    BUILD_DIR  := $(BUILD_BASE)/linux
    PLAT_LIBS  :=
    EXE_EXT    :=
endif

# AddressSanitizer — separate output directory (flags: see "Compiler flags")
BUILD_ASAN ?= 0
ifeq ($(BUILD_ASAN),1)
BUILD_DIR := $(BUILD_DIR)-asan
endif

EMBED_DIR := $(BUILD_BASE)/embedded-baremetal
BIN_DIR   := $(BUILD_DIR)/binaries
LIB_DIR   := $(BUILD_DIR)/libs
OBJ_DIR   := $(BUILD_DIR)/obj
EMBED_BIN_DIR := $(EMBED_DIR)/binaries
EMBED_LIB_DIR := $(EMBED_DIR)/libs
EMBED_OBJ_DIR := $(EMBED_DIR)/obj

# ---------------------------------------------------------------------------
#  Compiler flags
# ---------------------------------------------------------------------------
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -Wpedantic -Wshadow \
           -Wstrict-prototypes -Wmissing-prototypes \
           -Wno-unused-parameter \
            -O2 -g -ffunction-sections -fdata-sections
CFLAGS += -I$(INC_DIR)

# Feature-test macros: -std=c99 makes glibc / musl hide POSIX (nanosleep, clock_gettime,
# lstat, mkfifo, ...) and the BSD extras the examples use (flock). _DEFAULT_SOURCE gives
# the system's normal set back. macOS and the BSDs show everything without a macro; a
# POSIX level there would hide getpeereid() and SO_NOSIGPIPE instead. Chosen by the target
# platform, so a cross build for Linux gets it too.
ifeq ($(PLATFORM),linux)
CFLAGS += -D_DEFAULT_SOURCE
endif

# Linker flags for host examples (Apple's linker spells them differently)
ifeq ($(PLATFORM),macos)
LDFLAGS      ?= -Wl,-dead_strip
MAP_FLAG     := -Wl,-map,
else
LDFLAGS      ?= -Wl,--gc-sections
MAP_FLAG     := -Wl,-Map,
endif

ifeq ($(BUILD_ASAN),1)
CFLAGS    += -fsanitize=address -fno-omit-frame-pointer -g
LDFLAGS   += -fsanitize=address
endif

ARFLAGS      := rcs

# ---------------------------------------------------------------------------
#  Run-time option
# ---------------------------------------------------------------------------
PORT ?= 2323

# ---------------------------------------------------------------------------
#  Optional transports  (1=enabled, 0=disabled)
#
#  If any BUILD_EXAMPLE_* is set explicitly (command line or environment),
#  all unset ones default to 0 (opt-in model).  Otherwise the defaults
#  below apply.
# ---------------------------------------------------------------------------
_EX_CLI := $(filter-out undefined,$(origin BUILD_EXAMPLE_TELNET) $(origin BUILD_EXAMPLE_TCP) $(origin BUILD_EXAMPLE_SERIAL) $(origin BUILD_EXAMPLE_PIPE) $(origin BUILD_EXAMPLE_UNIX_SOCKET))
ifdef _EX_CLI
BUILD_EXAMPLE_TELNET ?= 0
BUILD_EXAMPLE_TCP ?= 0
BUILD_EXAMPLE_SERIAL ?= 0
BUILD_EXAMPLE_PIPE ?= 0
BUILD_EXAMPLE_UNIX_SOCKET ?= 0
else
BUILD_EXAMPLE_TELNET ?= 1
BUILD_EXAMPLE_TCP ?= 0
BUILD_EXAMPLE_SERIAL ?= 1
BUILD_EXAMPLE_PIPE ?= 0
BUILD_EXAMPLE_UNIX_SOCKET ?= 0
endif

# ---------------------------------------------------------------------------
#  Embedded bare-metal overrides — only the static library, no host examples
# ---------------------------------------------------------------------------
ifeq ($(PLATFORM),embedded-baremetal)
BUILD_EXAMPLE_TELNET = 0
BUILD_EXAMPLE_TCP    = 0
BUILD_EXAMPLE_SERIAL = 0
BUILD_EXAMPLE_PIPE   = 0
BUILD_EXAMPLE_UNIX_SOCKET = 0
BUILD_LIB_STATIC     = 1
BUILD_LIB_SHARED     = 0
endif

# ---------------------------------------------------------------------------
#  Library type selection
# ---------------------------------------------------------------------------
BUILD_LIB_STATIC  ?= 0
BUILD_LIB_SHARED  ?= 1

# ---------------------------------------------------------------------------
#  Output targets
# ---------------------------------------------------------------------------
STATIC_LIB_OUT := $(LIB_DIR)/libopenlibcli.a
ifeq ($(PLATFORM),windows)
SHARED_LIB_OUT := $(LIB_DIR)/OpenLibCLI.dll
SHARED_IMPLIB_OUT := $(LIB_DIR)/libopenlibcli.dll.a
SHARED_LINK_FLAGS := -shared -Wl,--out-implib,$(SHARED_IMPLIB_OUT)
else ifeq ($(PLATFORM),macos)
SHARED_LIB_OUT := $(LIB_DIR)/libopenlibcli.dylib
SHARED_IMPLIB_OUT :=
SHARED_LINK_FLAGS := -dynamiclib -install_name @rpath/libopenlibcli.dylib
else
SHARED_LIB_OUT := $(LIB_DIR)/libopenlibcli.so
SHARED_IMPLIB_OUT :=
SHARED_LINK_FLAGS := -shared -Wl,-soname,libopenlibcli.so
endif

# Copy of the shared library next to the examples (a Windows DLL is found there).
SHARED_RUNTIME := $(BIN_DIR)/$(notdir $(SHARED_LIB_OUT))

# What the examples link: the shared library, or the static one when the shared library is
# not built. The shared library is linked by name with an rpath relative to the executable,
# so the examples run from any directory (Windows: the DLL copied next to them).
ifeq ($(BUILD_LIB_SHARED),1)
LIB_OUT          := $(SHARED_LIB_OUT)
LIB_RUNTIME      := $(SHARED_RUNTIME)
EXAMPLE_LIB_DEFS := -DCLI_SHARED
ifeq ($(PLATFORM),windows)
LIB_LINK := $(SHARED_LIB_OUT)
else ifeq ($(PLATFORM),macos)
LIB_LINK := -L$(LIB_DIR) -lopenlibcli -Wl,-rpath,@loader_path/../libs
else
LIB_LINK := -L$(LIB_DIR) -lopenlibcli -Wl,-rpath,'$$ORIGIN/../libs'
endif
else
LIB_OUT          := $(STATIC_LIB_OUT)
LIB_RUNTIME      :=
EXAMPLE_LIB_DEFS :=
LIB_LINK         := $(STATIC_LIB_OUT)
endif
TELNET_BIN   := $(BIN_DIR)/cli_server_telnet$(EXE_EXT)
TCP_BIN      := $(BIN_DIR)/cli_server_tcp$(EXE_EXT)
SERIAL_BIN   := $(BIN_DIR)/cli_server_serial$(EXE_EXT)
PIPE_BIN     := $(BIN_DIR)/cli_server_pipe$(EXE_EXT)
UNIX_SOCK_BIN := $(BIN_DIR)/cli_server_unix_socket$(EXE_EXT)
EMBED_LIB    := $(EMBED_LIB_DIR)/libopenlibcli.a

SHARED_CFLAGS := $(CFLAGS) -DCLI_SHARED -DBUILD_LIB_SHARED
ifneq ($(PLATFORM),windows)
ifneq ($(PLATFORM),embedded-baremetal)
SHARED_CFLAGS += -fPIC
endif
endif

# Consumer CFLAGS for the examples (CLI_SHARED when they link the shared library)
EXAMPLE_CFLAGS := $(CFLAGS) $(EXAMPLE_LIB_DEFS)

# ---------------------------------------------------------------------------
#  Library sources → objects
# ---------------------------------------------------------------------------
LIB_SRCS := $(SRC_DIR)/cli.c $(SRC_DIR)/utils/cli_args_parse.c

LIB_OBJS        := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(LIB_SRCS))
LIB_SHARED_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/shared/%.o,$(LIB_SRCS))
EMBED_LIB_OBJS  := $(patsubst $(SRC_DIR)/%.c,$(EMBED_OBJ_DIR)/%.o,$(LIB_SRCS))

# Example objects. Compiled for the library they link (CLI_SHARED or not), so each link
# mode has its own directory: switching BUILD_LIB_SHARED never mixes the two.
LINK_MODE       := $(if $(filter 1,$(BUILD_LIB_SHARED)),shared,static)
LINK_MODE_OTHER := $(if $(filter 1,$(BUILD_LIB_SHARED)),static,shared)
EX_OBJ_DIR := $(OBJ_DIR)/examples-$(LINK_MODE)

# The executables keep their names in both modes, so the mode they were last linked in is
# recorded by a marker file: when the other mode's marker exists, this mode's marker is
# made again (phony, always out of date), which relinks the examples.
LINK_STAMP       := $(OBJ_DIR)/linked-$(LINK_MODE)
LINK_STAMP_OTHER := $(OBJ_DIR)/linked-$(LINK_MODE_OTHER)
ifneq ($(wildcard $(LINK_STAMP_OTHER)),)
.PHONY: $(LINK_STAMP)
endif
ex_objs     = $(patsubst $(EXAMPLE_DIR)/%.c,$(EX_OBJ_DIR)/%.o,$(1))
DEMO_OBJ        := $(call ex_objs,$(EXAMPLE_DIR)/shared/cli_demo_setup.c)
TELNET_OBJS     := $(call ex_objs,$(addprefix $(EXAMPLE_DIR)/win_linux/telnet/,cli_server_telnet.c cli_transport_telnet.c)) $(DEMO_OBJ)
TCP_OBJS        := $(call ex_objs,$(addprefix $(EXAMPLE_DIR)/win_linux/tcp/,cli_server_tcp.c cli_transport_tcp.c)) $(DEMO_OBJ)
SERIAL_OBJS     := $(call ex_objs,$(addprefix $(EXAMPLE_DIR)/win_linux/serial/,cli_server_serial.c cli_transport_serial.c)) $(DEMO_OBJ)
PIPE_OBJS       := $(call ex_objs,$(addprefix $(EXAMPLE_DIR)/win_linux/pipe/,cli_server_pipe.c cli_transport_pipe.c)) $(DEMO_OBJ)
UNIX_SOCK_OBJS  := $(call ex_objs,$(addprefix $(EXAMPLE_DIR)/linux/unix_socket/,cli_server_unix_socket.c cli_transport_unix_socket.c)) $(DEMO_OBJ)
EX_OBJS         := $(sort $(TELNET_OBJS) $(TCP_OBJS) $(SERIAL_OBJS) $(PIPE_OBJS) $(UNIX_SOCK_OBJS))

ALL_HOST_TARGETS :=
ifeq ($(BUILD_LIB_STATIC),1)
ALL_HOST_TARGETS += lib
endif
ifeq ($(BUILD_LIB_SHARED),1)
ALL_HOST_TARGETS += shared
endif

ifeq ($(BUILD_EXAMPLE_TELNET),1)
ALL_HOST_TARGETS += $(TELNET_BIN)
endif

ifeq ($(BUILD_EXAMPLE_TCP),1)
ALL_HOST_TARGETS += $(TCP_BIN)
endif

ifeq ($(BUILD_EXAMPLE_SERIAL),1)
ALL_HOST_TARGETS += $(SERIAL_BIN)
endif

ifeq ($(BUILD_EXAMPLE_PIPE),1)
ALL_HOST_TARGETS += $(PIPE_BIN)
endif

ifeq ($(BUILD_EXAMPLE_UNIX_SOCKET),1)
ifneq ($(PLATFORM),windows)
ALL_HOST_TARGETS += $(UNIX_SOCK_BIN)
endif
endif

# ---------------------------------------------------------------------------
#  Default target — host platform
# ---------------------------------------------------------------------------
.PHONY: all
all: $(ALL_HOST_TARGETS)
	@$(call say,)
	@$(call say,Build complete  [PLATFORM=$(PLATFORM)  CC=$(CC)])
	@$(call say,  Output dir    : $(BUILD_DIR)/)
	@$(call say,  Binaries      : $(BIN_DIR)/)
	@$(call say,  Libraries     : $(LIB_DIR)/)
	@$(call say,  Objects       : $(OBJ_DIR)/)
ifneq ($(BUILD_ASAN),0)
	@$(call say,  Sanitizer     : AddressSanitizer)
endif
	@$(call say,  Map files     : $(BUILD_DIR)/)
ifeq ($(BUILD_LIB_SHARED),1)
	@$(call say,  Shared lib    : $(LIB_OUT))
ifneq ($(strip $(SHARED_IMPLIB_OUT)),)
	@$(call say,  Import lib    : $(SHARED_IMPLIB_OUT))
endif
endif
ifeq ($(BUILD_LIB_STATIC),1)
	@$(call say,  Static lib    : $(STATIC_LIB_OUT))
endif
ifeq ($(BUILD_EXAMPLE_TELNET),1)
	@$(call say,  Telnet server : $(TELNET_BIN))
endif
ifeq ($(BUILD_EXAMPLE_TCP),1)
	@$(call say,  TCP server    : $(TCP_BIN))
endif
ifeq ($(BUILD_EXAMPLE_SERIAL),1)
	@$(call say,  Serial server : $(SERIAL_BIN))
endif
ifeq ($(BUILD_EXAMPLE_PIPE),1)
	@$(call say,  Pipe server   : $(PIPE_BIN))
endif
ifeq ($(BUILD_EXAMPLE_UNIX_SOCKET),1)
ifneq ($(PLATFORM),windows)
	@$(call say,  Unix socket   : $(UNIX_SOCK_BIN))
endif
endif
	@$(call say,)
	@$(call say,Run targets:)
ifeq ($(BUILD_EXAMPLE_TELNET),1)
	@$(call say,  make run-telnet PORT=$(PORT))
endif
ifeq ($(BUILD_EXAMPLE_TCP),1)
	@$(call say,  make run-tcp PORT=$(PORT))
endif
ifeq ($(BUILD_EXAMPLE_SERIAL),1)
	@$(call say,  make run-serial)
endif
ifeq ($(BUILD_EXAMPLE_PIPE),1)
	@$(call say,  make run-pipe)
endif
ifeq ($(BUILD_EXAMPLE_UNIX_SOCKET),1)
ifneq ($(PLATFORM),windows)
	@$(call say,  make run-unix-socket)
endif
endif

# ---------------------------------------------------------------------------
#  Static library  (host)
# ---------------------------------------------------------------------------
# 'lib' and 'shared' can always be built on request; BUILD_LIB_STATIC / BUILD_LIB_SHARED
# only choose what 'all' builds.
.PHONY: lib
lib: $(STATIC_LIB_OUT)

$(STATIC_LIB_OUT): $(LIB_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

.PHONY: shared
shared: $(SHARED_LIB_OUT) $(SHARED_RUNTIME)

$(SHARED_LIB_OUT): $(LIB_SHARED_OBJS) | $(LIB_DIR)
	$(CC) $(SHARED_CFLAGS) $(SHARED_LINK_FLAGS) -o $@ $(LIB_SHARED_OBJS) $(PLAT_LIBS)

# A rule of its own: a copy that failed is made again by the next build.
$(SHARED_RUNTIME): $(SHARED_LIB_OUT) | $(BIN_DIR)
ifeq ($(CMD_SHELL),1)
	$(COPY) $(subst /,\,$<) $(subst /,\,$@)
else
	$(COPY) $< $@
endif

# ---------------------------------------------------------------------------
#  Compile objects  (host, with auto-dependency tracking: a changed header rebuilds
#  everything that includes it)
# ---------------------------------------------------------------------------
$(OBJ_DIR)/shared/%.o: $(SRC_DIR)/%.c
	@$(call mkdir_p,$(@D))
	$(CC) $(SHARED_CFLAGS) -MMD -MP -c $< -o $@

$(EX_OBJ_DIR)/%.o: $(EXAMPLE_DIR)/%.c
	@$(call mkdir_p,$(@D))
	$(CC) $(EXAMPLE_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@$(call mkdir_p,$(@D))
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(LIB_OBJS:.o=.d) $(LIB_SHARED_OBJS:.o=.d) $(EX_OBJS:.o=.d)

# Link-mode marker (see LINK_STAMP)
$(LINK_STAMP):
	@$(call mkdir_p,$(@D))
ifeq ($(CMD_SHELL),1)
	@if exist "$(subst /,\,$(LINK_STAMP_OTHER))" del /q "$(subst /,\,$(LINK_STAMP_OTHER))"
	@type nul > "$(subst /,\,$@)"
else
	@rm -f $(LINK_STAMP_OTHER)
	@: > $@
endif

# ---------------------------------------------------------------------------
#  Example: Telnet server  (host)
# ---------------------------------------------------------------------------
.PHONY: cli_server_telnet telnet
ifeq ($(BUILD_EXAMPLE_TELNET),1)
cli_server_telnet telnet: $(TELNET_BIN)

$(TELNET_BIN): $(TELNET_OBJS) $(LIB_OUT) $(LIB_RUNTIME) $(LINK_STAMP) | $(BIN_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(LDFLAGS) $(TELNET_OBJS) $(LIB_LINK) $(PLAT_LIBS) $(MAP_FLAG)$(BUILD_DIR)/cli_server_telnet.map -o $@
else
cli_server_telnet telnet:
	@$(call say,cli_server_telnet requires BUILD_EXAMPLE_TELNET=1.)
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Example: TCP server  (host)
# ---------------------------------------------------------------------------
.PHONY: cli_server_tcp tcp
ifeq ($(BUILD_EXAMPLE_TCP),1)
cli_server_tcp tcp: $(TCP_BIN)

$(TCP_BIN): $(TCP_OBJS) $(LIB_OUT) $(LIB_RUNTIME) $(LINK_STAMP) | $(BIN_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(LDFLAGS) $(TCP_OBJS) $(LIB_LINK) $(PLAT_LIBS) $(MAP_FLAG)$(BUILD_DIR)/cli_server_tcp.map -o $@
else
cli_server_tcp tcp:
	@$(call say,cli_server_tcp requires BUILD_EXAMPLE_TCP=1.)
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Example: Serial server  (host simulation)
# ---------------------------------------------------------------------------
.PHONY: cli_server_serial uart
ifeq ($(BUILD_EXAMPLE_SERIAL),1)
cli_server_serial uart: $(SERIAL_BIN)

$(SERIAL_BIN): $(SERIAL_OBJS) $(LIB_OUT) $(LIB_RUNTIME) $(LINK_STAMP) | $(BIN_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(LDFLAGS) $(SERIAL_OBJS) $(LIB_LINK) $(PLAT_LIBS) $(MAP_FLAG)$(BUILD_DIR)/cli_server_serial.map -o $@
else
cli_server_serial uart:
	@$(call say,cli_server_serial requires BUILD_EXAMPLE_SERIAL=1.)
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Example: Pipe server  (named + anonymous pipe IPC)
# ---------------------------------------------------------------------------
.PHONY: cli_server_pipe pipe
ifeq ($(BUILD_EXAMPLE_PIPE),1)
cli_server_pipe pipe: $(PIPE_BIN)

ifeq ($(PLATFORM),windows)
PIPE_THREAD_LIBS := -ladvapi32
else
PIPE_THREAD_LIBS := -lpthread
endif

$(PIPE_BIN): $(PIPE_OBJS) $(LIB_OUT) $(LIB_RUNTIME) $(LINK_STAMP) | $(BIN_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(LDFLAGS) $(PIPE_OBJS) $(LIB_LINK) $(PLAT_LIBS) $(PIPE_THREAD_LIBS) \
	    $(MAP_FLAG)$(BUILD_DIR)/cli_server_pipe.map -o $@
else
cli_server_pipe pipe:
	@$(call say,cli_server_pipe requires BUILD_EXAMPLE_PIPE=1.)
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Example: UNIX domain socket server  (POSIX only: Linux / macOS / BSD)
# ---------------------------------------------------------------------------
ifeq ($(BUILD_EXAMPLE_UNIX_SOCKET),1)
ifneq ($(PLATFORM),windows)
.PHONY: cli_server_unix_socket unix-socket
cli_server_unix_socket unix-socket: $(UNIX_SOCK_BIN)

$(UNIX_SOCK_BIN): $(UNIX_SOCK_OBJS) $(LIB_OUT) $(LIB_RUNTIME) $(LINK_STAMP) | $(BIN_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(LDFLAGS) $(UNIX_SOCK_OBJS) $(LIB_LINK) $(PLAT_LIBS) -lpthread \
	    $(MAP_FLAG)$(BUILD_DIR)/cli_server_unix_socket.map -o $@
else
.PHONY: cli_server_unix_socket unix-socket
cli_server_unix_socket unix-socket:
	@$(call say,cli_server_unix_socket is only available on POSIX platforms.)
	@exit 1
endif
else
.PHONY: cli_server_unix_socket unix-socket
cli_server_unix_socket unix-socket:
	@$(call say,cli_server_unix_socket requires BUILD_EXAMPLE_UNIX_SOCKET=1.)
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Embedded cross-compile target  →  build/gcc/embedded-baremetal/libs/libopenlibcli.a
#
#  Usage:
#    make CC=arm-none-eabi-gcc embedded-baremetal   # CC: the cross-compiler (there is no default)
#    make CC=riscv32-unknown-elf-gcc embedded-baremetal
#    make CFLAGS="-mcpu=cortex-m0 -mthumb -Os -Icli" embedded-baremetal
# ---------------------------------------------------------------------------
.PHONY: embedded-baremetal
ifeq ($(PLATFORM),embedded-baremetal)
embedded-baremetal: lib
else
embedded-baremetal: $(EMBED_LIB)
endif
	@$(call say,)
	@$(call say,Embedded bare-metal build complete  [CC=$(CC)])
ifeq ($(PLATFORM),embedded-baremetal)
	@$(call say,  Output dir : $(BUILD_DIR)/)
	@$(call say,  Libraries  : $(LIB_DIR)/)
	@$(call say,  Objects    : $(OBJ_DIR)/)
	@$(call say,  Library    : $(STATIC_LIB_OUT))
else
	@$(call say,  Output dir : $(EMBED_DIR)/)
	@$(call say,  Libraries  : $(EMBED_LIB_DIR)/)
	@$(call say,  Objects    : $(EMBED_OBJ_DIR)/)
	@$(call say,  Library    : $(EMBED_LIB))
endif
	@$(call say,)
	@$(call say,  Link this library into your MCU firmware and implement)
	@$(call say,  the serial callbacks via cli_serial_init().)

ifneq ($(PLATFORM),embedded-baremetal)
$(EMBED_LIB): $(EMBED_LIB_OBJS) | $(EMBED_LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

$(EMBED_OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@$(call mkdir_p,$(@D))
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(EMBED_LIB_OBJS:.o=.d)
endif

# ---------------------------------------------------------------------------
#  AddressSanitizer target  —  rebuild everything with -fsanitize=address
# ---------------------------------------------------------------------------
.PHONY: asan
asan:
	$(MAKE) BUILD_ASAN=1 all

# ---------------------------------------------------------------------------
#  Run targets
# ---------------------------------------------------------------------------
.PHONY: run-telnet
ifeq ($(BUILD_EXAMPLE_TELNET),1)
run-telnet: $(TELNET_BIN)
ifeq ($(CMD_SHELL),1)
	@$(subst /,\,$(TELNET_BIN)) $(PORT)
else
	@$(TELNET_BIN) $(PORT)
endif
else
run-telnet:
	@$(call say,run-telnet requires BUILD_EXAMPLE_TELNET=1.)
	@exit 1
endif

.PHONY: run-tcp
ifeq ($(BUILD_EXAMPLE_TCP),1)
run-tcp: $(TCP_BIN)
ifeq ($(CMD_SHELL),1)
	@$(subst /,\,$(TCP_BIN)) $(PORT)
else
	@$(TCP_BIN) $(PORT)
endif
else
run-tcp:
	@$(call say,run-tcp requires BUILD_EXAMPLE_TCP=1.)
	@exit 1
endif

.PHONY: run-serial
ifeq ($(BUILD_EXAMPLE_SERIAL),1)
run-serial: $(SERIAL_BIN)
ifeq ($(CMD_SHELL),1)
	@$(subst /,\,$(SERIAL_BIN))
else
	@$(SERIAL_BIN)
endif
else
run-serial:
	@$(call say,run-serial requires BUILD_EXAMPLE_SERIAL=1.)
	@exit 1
endif

.PHONY: run-pipe
ifeq ($(BUILD_EXAMPLE_PIPE),1)
run-pipe: $(PIPE_BIN)
ifeq ($(CMD_SHELL),1)
	@$(subst /,\,$(PIPE_BIN)) anon
else
	@$(PIPE_BIN) anon
endif
else
run-pipe:
	@$(call say,run-pipe requires BUILD_EXAMPLE_PIPE=1.)
	@exit 1
endif

ifeq ($(BUILD_EXAMPLE_UNIX_SOCKET),1)
ifneq ($(PLATFORM),windows)
.PHONY: run-unix-socket
run-unix-socket: $(UNIX_SOCK_BIN)
ifeq ($(CMD_SHELL),1)
	@$(subst /,\,$(UNIX_SOCK_BIN))
else
	@$(UNIX_SOCK_BIN)
endif
else
.PHONY: run-unix-socket
run-unix-socket:
	@$(call say,run-unix-socket is only available on POSIX platforms.)
	@exit 1
endif
else
.PHONY: run-unix-socket
run-unix-socket:
	@$(call say,run-unix-socket requires BUILD_EXAMPLE_UNIX_SOCKET=1.)
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Create output directories
# ---------------------------------------------------------------------------
$(sort $(BUILD_DIR) $(BIN_DIR) $(LIB_DIR) $(EMBED_DIR) $(EMBED_LIB_DIR)):
	@$(call mkdir_p,$@)


# ---------------------------------------------------------------------------
#  Clean — current platform only
# ---------------------------------------------------------------------------
.PHONY: clean
# Loose files in the output directory. Make expands the patterns itself, so the commands
# get plain file names: no wildcard depends on how a shell treats "\*" or "/".
CLEAN_FILES = $(wildcard $(addprefix $(BUILD_DIR)/*,.map .obj .o .d .a .lib .so .dll .dylib \
                                     .exe .out .app .pdb .ilk))
CLEAN_DIRS  = $(wildcard $(BIN_DIR) $(LIB_DIR) $(OBJ_DIR))

clean:
ifeq ($(CMD_SHELL),1)
	$(if $(CLEAN_DIRS),$(RMDIR) $(subst /,\,$(CLEAN_DIRS)))
	$(if $(CLEAN_FILES),$(RM) $(subst /,\,$(CLEAN_FILES)))
else
	$(if $(CLEAN_DIRS),$(RMDIR) $(CLEAN_DIRS))
	$(if $(CLEAN_FILES),$(RM) $(CLEAN_FILES))
endif
	@$(call say,Cleaned contents of $(BUILD_DIR)/ (folder kept).)

# ---------------------------------------------------------------------------
#  Clean — entire build tree
# ---------------------------------------------------------------------------
.PHONY: clean-all distclean
clean-all distclean:
ifeq ($(CMD_SHELL),1)
	-$(RMDIR) $(subst /,\,$(BUILD_BASE))
else
	-$(RMDIR) $(BUILD_BASE)
endif
	@$(call say,Removed $(BUILD_BASE)/.)

.PHONY: clean-asan
clean-asan:
	$(MAKE) clean BUILD_ASAN=1

# ---------------------------------------------------------------------------
#  Help
# ---------------------------------------------------------------------------
.PHONY: help
help:
	@$(call say,)
	@$(call say,OpenLibCLI Library - GNU Make (GCC / MinGW / Clang))
	@$(call say,=======================================================)
	@$(call say,)
	@$(call say,Output directories:)
	@$(call say,  build/gcc/windows/   MinGW / Windows cross-compile)
	@$(call say,  build/gcc/linux/     GCC Linux / macOS)
	@$(call say,  build/gcc/macos/     Clang macOS)
	@$(call say,  build/gcc/embedded-baremetal/  Bare-metal cross-compile)
	@$(call say,)
	@$(call say,Targets:)
	@$(call say,  all            Shared library + examples  [default; add BUILD_LIB_STATIC=1 for static])
	@$(call say,  lib            Static library only)
	@$(call say,  shared         Shared library only)
	@$(call say,  cli_server_telnet  Telnet server example)
	@$(call say,  cli_server_tcp     TCP server example)
	@$(call say,  cli_server_serial  Serial server example (host simulation))
	@$(call say,  cli_server_pipe    Named/anonymous pipe server example)
	@$(call say,  cli_server_unix_socket  UNIX domain socket server (POSIX only))
	@$(call say,  embedded-baremetal  Cross-compile for bare-metal -> build/gcc/embedded-baremetal/)
	@$(call say,  asan           Build everything with AddressSanitizer (host only))
	@$(call say,)
	@$(call say,Run targets:)
	@$(call say,  run-telnet     Build + run telnet server)
	@$(call say,  run-tcp        Build + run TCP server)
	@$(call say,  run-serial     Build + run Serial demo)
	@$(call say,  run-pipe       Build + run pipe demo (anonymous pipe: interactive))
	@$(call say,  run-unix-socket  Build + run UNIX socket server (POSIX only))
	@$(call say,  clean          Remove artefacts for current platform)
	@$(call say,  clean-all      Remove entire build/ tree)
	@$(call say,  clean-asan     Remove ASAN build artefacts)
	@$(call say,  help           Show this message)
	@$(call say,)
	@$(call say,Variables:)
	@$(call say,  PLATFORM=windows|linux|macos|embedded-baremetal  (auto-detected))
	@$(call say,  BUILD_EXAMPLE_TELNET=0|1       Enable Telnet transport and telnet demo)
	@$(call say,  BUILD_EXAMPLE_TCP=0|1          Enable TCP transport and tcp demo)
	@$(call say,  BUILD_EXAMPLE_SERIAL=0|1       Enable generic serial transport and serial demo)
	@$(call say,  BUILD_EXAMPLE_PIPE=0|1         Enable pipe transport and pipe demo)
	@$(call say,  BUILD_EXAMPLE_UNIX_SOCKET=0|1  Enable UNIX socket transport and POSIX demo)
	@$(call say,  BUILD_LIB_STATIC=0|1          Build static library  (default: 0))
	@$(call say,  BUILD_LIB_SHARED=0|1          Build shared library  (default: 1))
	@$(call say,  BUILD_ASAN=0|1               Build with AddressSanitizer  (default: 0))
	@$(call say,  CC=gcc              Host compiler)
	@$(call say,  CC                  Compiler (set to cross-compiler for embedded))
	@$(call say,  PORT=2323           Telnet port (run-telnet))
	@$(call say,)
	@$(call say,Examples:)
	@$(call say,  make)
	@$(call say,  make BUILD_EXAMPLE_PIPE=1   pipe example only: setting any BUILD_EXAMPLE_* turns the others off)
	@$(call say,  make run-telnet PORT=9999)
	@$(call say,  make CC=clang)
	@$(call say,  make asan)
	@$(call say,  make CC=arm-none-eabi-gcc PLATFORM=embedded-baremetal)
	@$(call say,  make CC=x86_64-w64-mingw32-gcc PLATFORM=windows)
	@$(call say,)
	@$(call say,MSVC / NMake:  nmake /f Makefile.nmake [help])
	@$(call say,)
