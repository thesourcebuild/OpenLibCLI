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
#  all            Build static + shared libraries and host examples [default]
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
ifeq ($(OS),Windows_NT)
    RM    ?= cmd /c del /f /q
    RMDIR ?= cmd /c rd /s /q
else
    RM    ?= rm -f
    RMDIR ?= rm -rf
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
else
    BUILD_DIR  := $(BUILD_BASE)/linux
    PLAT_LIBS  :=
    EXE_EXT    :=
endif

# AddressSanitizer — separate output directory + sanitizer flags
BUILD_ASAN ?= 0
ifeq ($(BUILD_ASAN),1)
BUILD_DIR := $(BUILD_DIR)-asan
CFLAGS    += -fsanitize=address -fno-omit-frame-pointer -g
LDFLAGS   += -fsanitize=address
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

# POSIX feature test macro (nanosleep / timespec in cli_demo_setup.c)
ifneq ($(PLATFORM),windows)
CFLAGS += -D_POSIX_C_SOURCE=199309L
endif

# Linker flags for host examples
LDFLAGS      ?= -Wl,--gc-sections

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
SHARED_LINK_FLAGS := -dynamiclib
else
SHARED_LIB_OUT := $(LIB_DIR)/libopenlibcli.so
SHARED_IMPLIB_OUT :=
SHARED_LINK_FLAGS := -shared
endif
LIB_OUT      := $(SHARED_LIB_OUT)
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

# Consumer CFLAGS for examples linking against the shared library
EXAMPLE_CFLAGS := $(CFLAGS) -DCLI_SHARED

# ---------------------------------------------------------------------------
#  Library sources → objects
# ---------------------------------------------------------------------------
LIB_SRCS := $(SRC_DIR)/cli.c $(SRC_DIR)/utils/cli_args_parse.c

LIB_OBJS      := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(LIB_SRCS))
EMBED_LIB_OBJS := $(patsubst $(SRC_DIR)/%.c,$(EMBED_OBJ_DIR)/%.o,$(LIB_SRCS))
DEMO_SHARED_SRC := $(EXAMPLE_DIR)/shared/cli_demo_setup.c
DEMO_SHARED_HDR := $(EXAMPLE_DIR)/shared/cli_demo_setup.h

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
	@echo ""
	@echo "Build complete  [PLATFORM=$(PLATFORM)  CC=$(CC)]"
	@echo "  Output dir    : $(BUILD_DIR)/"
	@echo "  Binaries      : $(BIN_DIR)/"
	@echo "  Libraries     : $(LIB_DIR)/"
	@echo "  Objects       : $(OBJ_DIR)/"
ifneq ($(BUILD_ASAN),0)
	@echo "  Sanitizer     : AddressSanitizer"
endif
	@echo "  Map files     : $(BUILD_DIR)/"
	@echo "  Shared lib    : $(LIB_OUT)"
ifneq ($(strip $(SHARED_IMPLIB_OUT)),)
	@echo "  Import lib    : $(SHARED_IMPLIB_OUT)"
endif
ifeq ($(BUILD_LIB_STATIC),1)
	@echo "  Static lib    : $(STATIC_LIB_OUT)"
endif
ifeq ($(BUILD_EXAMPLE_TELNET),1)
	@echo "  Telnet server : $(TELNET_BIN)"
endif
ifeq ($(BUILD_EXAMPLE_TCP),1)
	@echo "  TCP server    : $(TCP_BIN)"
endif
ifeq ($(BUILD_EXAMPLE_SERIAL),1)
	@echo "  Serial server : $(SERIAL_BIN)"
endif
ifeq ($(BUILD_EXAMPLE_PIPE),1)
	@echo "  Pipe server   : $(PIPE_BIN)"
endif
ifeq ($(BUILD_EXAMPLE_UNIX_SOCKET),1)
ifneq ($(PLATFORM),windows)
	@echo "  Unix socket   : $(UNIX_SOCK_BIN)"
endif
endif
	@echo ""
	@echo "Run targets:"
ifeq ($(BUILD_EXAMPLE_TELNET),1)
	@echo "  make run-telnet PORT=$(PORT)"
endif
ifeq ($(BUILD_EXAMPLE_TCP),1)
	@echo "  make run-tcp PORT=$(PORT)"
endif
ifeq ($(BUILD_EXAMPLE_SERIAL),1)
	@echo "  make run-serial"
endif
ifeq ($(BUILD_EXAMPLE_PIPE),1)
	@echo "  make run-pipe"
endif
ifeq ($(BUILD_EXAMPLE_UNIX_SOCKET),1)
ifneq ($(PLATFORM),windows)
	@echo "  make run-unix-socket"
endif
endif

# ---------------------------------------------------------------------------
#  Static library  (host)
# ---------------------------------------------------------------------------
ifeq ($(BUILD_LIB_STATIC),1)
.PHONY: lib
lib: $(STATIC_LIB_OUT)

$(STATIC_LIB_OUT): $(LIB_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^
endif

ifeq ($(BUILD_LIB_SHARED),1)
.PHONY: shared
shared: $(SHARED_LIB_OUT)

$(SHARED_LIB_OUT): $(LIB_SRCS) | $(LIB_DIR)
	$(CC) $(SHARED_CFLAGS) $(SHARED_LINK_FLAGS) -o $@ $(LIB_SRCS) $(PLAT_LIBS)
endif

# ---------------------------------------------------------------------------
#  Compile library objects  (host, with auto-dependency tracking)
# ---------------------------------------------------------------------------
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(LIB_OBJS:.o=.d)

# ---------------------------------------------------------------------------
#  Example: Telnet server  (host)
# ---------------------------------------------------------------------------
.PHONY: cli_server_telnet telnet
ifeq ($(BUILD_EXAMPLE_TELNET),1)
cli_server_telnet telnet: $(TELNET_BIN)

$(TELNET_BIN): $(EXAMPLE_DIR)/win_linux/telnet/cli_server_telnet.c $(DEMO_SHARED_SRC) $(DEMO_SHARED_HDR) $(LIB_OUT) | $(BIN_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(LDFLAGS) $(EXAMPLE_DIR)/win_linux/telnet/cli_server_telnet.c $(EXAMPLE_DIR)/win_linux/telnet/cli_transport_telnet.c $(DEMO_SHARED_SRC) $(LIB_OUT) $(PLAT_LIBS) -Wl,-Map,$(BUILD_DIR)/cli_server_telnet.map -o $@
else
cli_server_telnet telnet:
	@echo "cli_server_telnet requires BUILD_EXAMPLE_TELNET=1."
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Example: TCP server  (host)
# ---------------------------------------------------------------------------
.PHONY: cli_server_tcp tcp
ifeq ($(BUILD_EXAMPLE_TCP),1)
cli_server_tcp tcp: $(TCP_BIN)

$(TCP_BIN): $(EXAMPLE_DIR)/win_linux/tcp/cli_server_tcp.c $(EXAMPLE_DIR)/win_linux/tcp/cli_server_tcp.h $(DEMO_SHARED_SRC) $(DEMO_SHARED_HDR) $(LIB_OUT) | $(BIN_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(LDFLAGS) $(EXAMPLE_DIR)/win_linux/tcp/cli_server_tcp.c $(EXAMPLE_DIR)/win_linux/tcp/cli_transport_tcp.c $(DEMO_SHARED_SRC) $(LIB_OUT) $(PLAT_LIBS) -Wl,-Map,$(BUILD_DIR)/cli_server_tcp.map -o $@
else
cli_server_tcp tcp:
	@echo "cli_server_tcp requires BUILD_EXAMPLE_TCP=1."
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Example: Serial server  (host simulation)
# ---------------------------------------------------------------------------
.PHONY: cli_server_serial uart
ifeq ($(BUILD_EXAMPLE_SERIAL),1)
cli_server_serial uart: $(SERIAL_BIN)

$(SERIAL_BIN): $(EXAMPLE_DIR)/win_linux/serial/cli_server_serial.c $(EXAMPLE_DIR)/win_linux/serial/cli_server_serial.h $(DEMO_SHARED_SRC) $(DEMO_SHARED_HDR) $(LIB_OUT) | $(BIN_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(LDFLAGS) $(EXAMPLE_DIR)/win_linux/serial/cli_server_serial.c $(EXAMPLE_DIR)/win_linux/serial/cli_transport_serial.c $(DEMO_SHARED_SRC) $(LIB_OUT) $(PLAT_LIBS) -Wl,-Map,$(BUILD_DIR)/cli_server_serial.map -o $@
else
cli_server_serial uart:
	@echo "cli_server_serial requires BUILD_EXAMPLE_SERIAL=1."
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Example: Pipe server  (named + anonymous pipe IPC)
# ---------------------------------------------------------------------------
.PHONY: cli_server_pipe pipe
ifeq ($(BUILD_EXAMPLE_PIPE),1)
cli_server_pipe pipe: $(PIPE_BIN)

ifeq ($(PLATFORM),windows)
PIPE_THREAD_LIBS :=
else
PIPE_THREAD_LIBS := -lpthread
endif

$(PIPE_BIN): $(EXAMPLE_DIR)/win_linux/pipe/cli_server_pipe.c $(DEMO_SHARED_SRC) $(DEMO_SHARED_HDR) $(LIB_OUT) | $(BIN_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(LDFLAGS) $(EXAMPLE_DIR)/win_linux/pipe/cli_server_pipe.c $(EXAMPLE_DIR)/win_linux/pipe/cli_transport_pipe.c $(DEMO_SHARED_SRC) $(LIB_OUT) $(PLAT_LIBS) $(PIPE_THREAD_LIBS) \
	    -Wl,-Map,$(BUILD_DIR)/cli_server_pipe.map -o $@
else
cli_server_pipe pipe:
	@echo "cli_server_pipe requires BUILD_EXAMPLE_PIPE=1."
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Example: UNIX domain socket server  (POSIX only: Linux / macOS / BSD)
# ---------------------------------------------------------------------------
ifeq ($(BUILD_EXAMPLE_UNIX_SOCKET),1)
ifneq ($(PLATFORM),windows)
.PHONY: cli_server_unix_socket unix-socket
cli_server_unix_socket unix-socket: $(UNIX_SOCK_BIN)

$(UNIX_SOCK_BIN): $(EXAMPLE_DIR)/linux/unix_socket/cli_server_unix_socket.c $(DEMO_SHARED_SRC) $(DEMO_SHARED_HDR) $(LIB_OUT) | $(BIN_DIR)
	$(CC) $(EXAMPLE_CFLAGS) $(LDFLAGS) $(EXAMPLE_DIR)/linux/unix_socket/cli_server_unix_socket.c $(EXAMPLE_DIR)/linux/unix_socket/cli_transport_unix_socket.c $(DEMO_SHARED_SRC) $(LIB_OUT) $(PLAT_LIBS) -lpthread \
	    -Wl,-Map,$(BUILD_DIR)/cli_server_unix_socket.map -o $@
else
.PHONY: cli_server_unix_socket unix-socket
cli_server_unix_socket unix-socket:
	@echo "cli_server_unix_socket is only available on POSIX platforms."
	@exit 1
endif
else
.PHONY: cli_server_unix_socket unix-socket
cli_server_unix_socket unix-socket:
	@echo "cli_server_unix_socket requires BUILD_EXAMPLE_UNIX_SOCKET=1."
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Embedded cross-compile target  →  build/gcc/embedded-baremetal/libs/libopenlibcli.a
#
#  Usage:
#    make embedded-baremetal                   # uses default CC=arm-none-eabi-gcc
#    make CC=riscv32-unknown-elf-gcc embedded-baremetal
#    make CFLAGS="-mcpu=cortex-m0 -mthumb -Os -Icli" embedded-baremetal
# ---------------------------------------------------------------------------
.PHONY: embedded-baremetal
embedded-baremetal: $(EMBED_LIB)
	@echo ""
	@echo "Embedded bare-metal build complete  [CC=$(CC)]"
	@echo "  Output dir : $(EMBED_DIR)/"
	@echo "  Libraries  : $(EMBED_LIB_DIR)/"
	@echo "  Objects    : $(EMBED_OBJ_DIR)/"
	@echo "  Library    : $(EMBED_LIB)"
	@echo ""
	@echo "  Link this library into your MCU firmware and implement"
	@echo "  the serial callbacks via cli_serial_init()."

$(EMBED_LIB): $(EMBED_LIB_OBJS) | $(EMBED_LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

$(EMBED_OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(EMBED_OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

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
	@$(TELNET_BIN) $(PORT)
else
run-telnet:
	@echo "run-telnet requires BUILD_EXAMPLE_TELNET=1."
	@exit 1
endif

.PHONY: run-tcp
ifeq ($(BUILD_EXAMPLE_TCP),1)
run-tcp: $(TCP_BIN)
	@$(TCP_BIN) $(PORT)
else
run-tcp:
	@echo "run-tcp requires BUILD_EXAMPLE_TCP=1."
	@exit 1
endif

.PHONY: run-serial
ifeq ($(BUILD_EXAMPLE_SERIAL),1)
run-serial: $(SERIAL_BIN)
	@$(SERIAL_BIN)
else
run-serial:
	@echo "run-serial requires BUILD_EXAMPLE_SERIAL=1."
	@exit 1
endif

.PHONY: run-pipe
ifeq ($(BUILD_EXAMPLE_PIPE),1)
run-pipe: $(PIPE_BIN)
	@$(PIPE_BIN) anon
else
run-pipe:
	@echo "run-pipe requires BUILD_EXAMPLE_PIPE=1."
	@exit 1
endif

ifeq ($(BUILD_EXAMPLE_UNIX_SOCKET),1)
ifneq ($(PLATFORM),windows)
.PHONY: run-unix-socket
run-unix-socket: $(UNIX_SOCK_BIN)
	@$(UNIX_SOCK_BIN)
else
.PHONY: run-unix-socket
run-unix-socket:
	@echo "run-unix-socket is only available on POSIX platforms."
	@exit 1
endif
else
.PHONY: run-unix-socket
run-unix-socket:
	@echo "run-unix-socket requires BUILD_EXAMPLE_UNIX_SOCKET=1."
	@exit 1
endif

# ---------------------------------------------------------------------------
#  Create output directories
# ---------------------------------------------------------------------------
$(BUILD_DIR) $(BIN_DIR) $(LIB_DIR):
ifeq ($(OS),Windows_NT)
	cmd /c if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
else
	mkdir -p $@
endif

$(OBJ_DIR) $(EMBED_DIR) $(EMBED_OBJ_DIR):
ifeq ($(OS),Windows_NT)
	cmd /c if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
	cmd /c if not exist "$(subst /,\,$(@)\utils)" if exist "$(subst /,\,$(@))" mkdir "$(subst /,\,$(@)\utils)"
else
	mkdir -p $@
	mkdir -p $@/utils
endif

# ---------------------------------------------------------------------------
#  Clean — current platform only
# ---------------------------------------------------------------------------
.PHONY: clean
clean:
	-$(RMDIR) $(BIN_DIR)
	-$(RMDIR) $(LIB_DIR)
	-$(RMDIR) $(OBJ_DIR)
	$(RM) $(BUILD_DIR)/*.map $(BUILD_DIR)/*.obj $(BUILD_DIR)/*.o $(BUILD_DIR)/*.d \
	      $(BUILD_DIR)/*.a $(BUILD_DIR)/*.lib $(BUILD_DIR)/*.so $(BUILD_DIR)/*.dll $(BUILD_DIR)/*.dylib \
	      $(BUILD_DIR)/*.exe $(BUILD_DIR)/*.out $(BUILD_DIR)/*.app \
	      $(BUILD_DIR)/*.pdb $(BUILD_DIR)/*.ilk
	@echo "Cleaned contents of $(BUILD_DIR)/ (folder kept)."

# ---------------------------------------------------------------------------
#  Clean — entire build tree
# ---------------------------------------------------------------------------
.PHONY: clean-all distclean
clean-all distclean:
	$(RMDIR) $(BUILD_BASE)
	@echo "Removed $(BUILD_BASE)/."

.PHONY: clean-asan
clean-asan:
	$(MAKE) clean BUILD_ASAN=1

# ---------------------------------------------------------------------------
#  Help
# ---------------------------------------------------------------------------
.PHONY: help
help:
	@echo ""
	@echo "OpenLibCLI Library — GNU Make (GCC / MinGW / Clang)"
	@echo "======================================================="
	@echo ""
	@echo "Output directories:"
	@echo "  build/gcc/windows/   MinGW / Windows cross-compile"
	@echo "  build/gcc/linux/     GCC Linux / macOS"
	@echo "  build/gcc/macos/     Clang macOS"
	@echo "  build/gcc/embedded-baremetal/  Bare-metal cross-compile"
	@echo ""
	@echo "Targets:"
	@echo "  all            Shared library + examples  [default; add BUILD_LIB_STATIC=1 for static]"
	@echo "  lib            Static library only"
	@echo "  shared         Shared library only"
	@echo "  cli_server_telnet  Telnet server example"
	@echo "  cli_server_tcp     TCP server example"
	@echo "  cli_server_serial  Serial server example (host simulation)"
	@echo "  cli_server_pipe    Named/anonymous pipe server example"
	@echo "  cli_server_unix_socket  UNIX domain socket server (POSIX only)"
	@echo "  embedded-baremetal  Cross-compile for bare-metal → build/gcc/embedded-baremetal/"
	@echo "  asan           Build everything with AddressSanitizer (host only)"
	@echo ""
	@echo "Run targets:"
	@echo "  run-telnet     Build + run telnet server"
	@echo "  run-tcp        Build + run TCP server"
	@echo "  run-serial     Build + run Serial demo"
	@echo "  run-pipe       Build + run pipe demo (anonymous/scripted)"
	@echo "  run-unix-socket  Build + run UNIX socket server (POSIX only)"
	@echo "  clean          Remove artefacts for current platform"
	@echo "  clean-all      Remove entire build/ tree"
	@echo "  clean-asan     Remove ASAN build artefacts"
	@echo "  help           Show this message"
	@echo ""
	@echo "Variables:"
	@echo "  PLATFORM=windows|linux|macos|embedded-baremetal  (auto-detected)"
	@echo "  BUILD_EXAMPLE_TELNET=0|1       Enable Telnet transport and telnet demo"
	@echo "  BUILD_EXAMPLE_TCP=0|1          Enable TCP transport (tcp demo also requires BUILD_EXAMPLE_TELNET=1)"
	@echo "  BUILD_EXAMPLE_SERIAL=0|1       Enable generic serial transport and serial demo"
	@echo "  BUILD_EXAMPLE_PIPE=0|1         Enable pipe transport and pipe demo"
	@echo "  BUILD_EXAMPLE_UNIX_SOCKET=0|1  Enable UNIX socket transport and POSIX demo"
	@echo "  BUILD_LIB_STATIC=0|1          Build static library  (default: 0)"
	@echo "  BUILD_LIB_SHARED=0|1          Build shared library  (default: 1)"
	@echo "  BUILD_ASAN=0|1               Build with AddressSanitizer  (default: 0)"
	@echo "  CC=gcc              Host compiler"
	@echo "  CC                  Compiler (set to cross-compiler for embedded)"
	@echo "  PORT=2323           Telnet port (run-telnet)"
	@echo ""
	@echo "Examples:"
	@echo "  make"
	@echo "  make BUILD_EXAMPLE_PIPE=0"
	@echo "  make run-telnet PORT=9999"
	@echo "  make CC=clang"
	@echo "  make asan"
	@echo "  make CC=arm-none-eabi-gcc PLATFORM=embedded-baremetal"
	@echo "  make CC=x86_64-w64-mingw32-gcc PLATFORM=windows"
	@echo ""
	@echo "MSVC / NMake:  nmake /f Makefile.nmake [help]"
	@echo ""
