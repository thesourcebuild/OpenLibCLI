/**
 * @file cli_server_pipe.c
 * @brief OpenLibCLI named and anonymous pipe transport example implementation.
 *
 * Demonstrates both named-pipe and anonymous-pipe transports on Windows and
 * POSIX.
 *
 * **Modes**
 * | Invocation                           | Behaviour                          |
 * |--------------------------------------|------------------------------------|
 * | @c cli_server_pipe                   | Named pipe server (default name)   |
 * | @c cli_server_pipe @c named @c [n]   | Named pipe server                  |
 * | @c cli_server_pipe @c client @c [n]  | Named pipe client (stdin relay)    |
 * | @c cli_server_pipe @c anon           | Anonymous pipe self-demo           |
 *
 * **Typical two-terminal workflow**
 * @code
 *   # Terminal 1 server:
 *   ./cli_server_pipe
 *
 *   # Terminal 2 client:
 *   ./cli_server_pipe client
 * @endcode
 *
 * **Named-pipe platform notes**
 * - Windows: pipe path becomes @c \\\\.\pipe\<name>
 * - POSIX  : two FIFOs are created @c <name>.in and @c <name>.out
 *
 * **Anonymous pipe mode**
 *
 * A background thread plays the "client" role.  No external tool is needed.
 *
 * **Build Linux / macOS** (from the repository root)
 * @code
 *   gcc -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Icli          \
 *       cli/cli.c cli/utils/cli_args_parse.c examples/shared/cli_demo_setup.c \
 *       examples/win_linux/pipe/cli_transport_pipe.c                     \
 *       examples/win_linux/pipe/cli_server_pipe.c -lpthread -o cli_server_pipe
 * @endcode
 * (macOS: add @c -D_DARWIN_C_SOURCE.)
 *
 * **Build Windows (MinGW)** (from the repository root)
 * @code
 *   gcc -std=c99 -Wall -Wextra -Icli                                    \
 *       cli/cli.c cli/utils/cli_args_parse.c examples/shared/cli_demo_setup.c \
 *       examples/win_linux/pipe/cli_transport_pipe.c                     \
 *       examples/win_linux/pipe/cli_server_pipe.c -ladvapi32 -o cli_server_pipe.exe
 * @endcode
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "../../shared/cli_demo_setup.h"
#include "cli_transport_pipe.h"
#include "cli_server_pipe.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__) || defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004 /* missing from older SDK headers */
#endif
#else
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#endif

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

/** @brief MOTD banner for the pipe transport demo. */
#define CLI_DEMO_BANNER_PIPE                                                                       \
  "************************************************************\r\n"                               \
  "*            OpenLibCLI  --  Pipe Transport                *\r\n"                               \
  "*         Pure-C, 100% Static Allocation CLI Engine        *\r\n"                               \
  "************************************************************"

/*=======================================================================================
 * Private Macros
 *=======================================================================================*/

#if ENV_IS_WINDOWS
#define PFT_THREAD_FN  DWORD WINAPI
#define PFT_THREAD_ARG LPVOID
#define PFT_THREAD_RET 0
#else
#define PFT_THREAD_FN  void *
#define PFT_THREAD_ARG void *
#define PFT_THREAD_RET NULL
#endif

/*=======================================================================================
 * Private Types
 *=======================================================================================*/

#if ENV_IS_WINDOWS
typedef HANDLE cli_thread_t;
#else
typedef pthread_t cli_thread_t;
#endif

/** @brief Shared state for the POSIX relay threads (named-pipe client and anonymous demo). */
typedef struct {
  cli_pipe_ctx_struct_t *ctx; /**< Client-side pipe context. */
  volatile int32_t done;      /**< Set to 1 by reader when the pipe closes. */
} relay_ctx_struct_t;

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Private Variables
 *=======================================================================================*/

/** @brief Per-session application data (single-session demo). */
static demo_app_data_struct_t s_app;

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

static void register_commands(cli_struct_t *cli);
static int run_named_server(const char *name);
static int run_named_client(const char *name);
static void run_anon_demo(void);
static void print_usage(const char *prog);
static int8_t cli_server_loop(cli_struct_t *cli);

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

/**
 * @brief Entry point for the pipe transport demo executable.
 *
 * Parses mode and pipe-name arguments, initializes OpenLibCLI, registers
 * demo commands, and dispatches into the selected transport demo mode.
 *
 * @param[in] argc Number of command-line arguments.
 * @param[in] argv Command-line arguments.
 *
 * @return @c 0 on normal flow, or @c 1 when an invalid mode is provided, the
 *         named pipe cannot be created, or the client cannot connect.
 */
int main(int argc, char *argv[]) {
  const char *mode = (argc > 1) ? argv[1] : "named";
  const char *name = (argc > 2) ? argv[2] : PIPE_DEFAULT_NAME;
  int status = 0;

#if !ENV_IS_WINDOWS
  /* Writing to a FIFO whose reader went away must end the session, not kill the process. */
  signal(SIGPIPE, SIG_IGN);
#endif

  if (strcmp(mode, "named") == 0) {
    status = run_named_server(name);
  } else if (strcmp(mode, "client") == 0) {
    status = run_named_client(name);
  } else if (strcmp(mode, "anon") == 0) {
    run_anon_demo();
  } else if (strcmp(mode, "help") == 0 || strcmp(mode, "--help") == 0) {
    print_usage(argv[0]);
  } else {
    fprintf(stderr, "Unknown mode: %s\n\n", mode);
    print_usage(argv[0]);
    status = 1;
  }

  return status;
}

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/**
 * @brief Start a new Windows thread.
 *
 * @param[out] t    Thread handle on success.
 * @param[in]  fn   Thread entry point.
 * @param[in]  arg  Argument passed to @p fn.
 *
 * @return @c true if the thread was started.
 */
static bool cli_thread_start(cli_thread_t *t, LPTHREAD_START_ROUTINE fn, void *arg) {
  *t = CreateThread(NULL, 0, fn, arg, 0, NULL);
  return (*t != NULL);
}
/**
 * @brief Wait for a Windows thread to finish and close its handle.
 * @param[in] t  Thread handle returned by @c cli_thread_start().
 */
static void cli_thread_join(cli_thread_t t) {
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
}

#else  /* POSIX */
/**
 * @brief Start a new POSIX thread.
 *
 * @param[out] t    Thread id on success.
 * @param[in]  fn   Thread entry function.
 * @param[in]  arg  Argument passed to @p fn.
 *
 * @return @c true if the thread was started.
 */
static bool cli_thread_start(cli_thread_t *t, void *(*fn)(void *), void *arg) {
  return (pthread_create(t, NULL, fn, arg) == 0);
}
/**
 * @brief Wait for a POSIX thread to finish.
 *
 * @param[in] t  Thread id returned by @c cli_thread_start().
 */
static void cli_thread_join(cli_thread_t t) {
  pthread_join(t, NULL);
}

/**
 * @brief Read from a POSIX pipe context.
 *
 * @param[in]  ctx  Pipe context.
 * @param[out] buf  Destination buffer.
 * @param[in]  len  Maximum bytes to read.
 *
 * @return Bytes read, or @c -1 on error.
 */
static int32_t cli_pipe_read(cli_pipe_ctx_struct_t *ctx, void *buf, uint32_t len) {
  return (int32_t)read(ctx->read_fd, buf, (size_t)len);
}
/**
 * @brief Write to a POSIX pipe context.
 *
 * @param[in] ctx  Pipe context.
 * @param[in] buf  Data to write.
 * @param[in] len  Byte count.
 *
 * @return Bytes written, or @c -1 on error.
 */
static int32_t cli_pipe_write(cli_pipe_ctx_struct_t *ctx, const void *buf, uint32_t len) {
  return (int32_t)write(ctx->write_fd, buf, (size_t)len);
}
#endif /* POSIX */

/* Command Handlers ---------------------------------------------------------------------*/

/**
 * @brief Handler for @c show @c pipe prints transport info.
 *
 * @param[in] cli   Active CLI session.
 * @param[in] cmd   Unused.
 * @param[in] argc  Unused.
 * @param[in] argv  Unused.
 *
 * @return @c CLI_OK.
 */
static int8_t cmd_show_pipe(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                            const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_println(cli, "Transport : named/anonymous pipe");
  cli_println(cli, "Mode      : %s",
              ENV_IS_WINDOWS ? "Windows named pipe (\\\\.\\pipe\\<name>)"
                             : "POSIX FIFO pair (<name>.in / <name>.out)");
  return CLI_OK;
}

/**
 * @brief Register all demo commands plus the pipe-specific
 *        @c show @c pipe sub-command.
 */
static void register_commands(cli_struct_t *cli) {
  cli_cmd_handle_t h_show = demo_register_commands(cli);
  cli_register_command(cli, h_show, "pipe", cmd_show_pipe, CLI_PRIV_USER, CLI_MODE_ANY,
                       "Show pipe transport info");
}

/**
 * @brief Apply demo defaults to a newly created CLI session.
 *
 * @param[in] cli         Active CLI session.
 * @param[in] session_id  Sequential session number.
 */
static void setup_session(cli_struct_t *cli, uint32_t session_id) {
  demo_setup_session(cli, &s_app, session_id, 4321, 3800, CLI_DEMO_BANNER_PIPE);
}

/**
 * @brief Blocking session loop runs @c cli_session_engine() until exit.
 *
 * @param[in] cli  Active CLI session.
 *
 * @return @c CLI_ERR_QUIT on clean exit, @c CLI_ERR on transport error.
 */
static int8_t cli_server_loop(cli_struct_t *cli) {
  cli_start(cli);
  int8_t rc;
  while (1) {
    /* Wait up to a second for input (timers still run in between). Not before the banner
     * and the first prompt, which are shown at once, and not once the pipe reports an error:
     * the engine ends the session then. */
    if (cli_get_session_state(cli) == CLI_SESSION_RUN) {
#if ENV_IS_WINDOWS
      DWORD start = GetTickCount();
      while (cli->transport.available(cli->transport.ctx) == 0) {
        if (GetTickCount() - start >= 1000) {
          break;
        }
        Sleep(1);
      }
#else
      struct pollfd pfd;
      pfd.fd = ((cli_pipe_ctx_struct_t *)cli->transport.ctx)->read_fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      (void)poll(&pfd, 1, 1000);
#endif
    }
    rc = cli_session_engine(cli);
    if (rc != CLI_OK) {
      cli->session_state = CLI_SESSION_STOP;
      break;
    }
  }
  return rc;
}

/* Named Pipe Server  ------------------------------------------------------------------*/

/**
 * @brief Listen on a named pipe and serve one client session at a time.
 *
 * Loops indefinitely, accepting one client per iteration via
 * @c cli_named_pipe_listen() and ending each session with
 * @c cli_named_pipe_disconnect(). An idle client is disconnected after the idle
 * timeout, so it cannot keep the next client out.
 *
 * @param[in] name  Logical pipe name / base path.
 *
 * @return 1 if the pipe cannot be created or opened (does not return otherwise).
 */
static int run_named_server(const char *name) {
#if ENV_IS_WINDOWS
  printf("Named pipe server ready.\n");
  printf("  Pipe path : \\\\.\\pipe\\%s\n", name);
#else
  printf("Named pipe server ready.\n");
  printf("  Read FIFO : %s.in\n", name);
  printf("  Write FIFO: %s.out\n", name);
#endif
  printf("Connect in another terminal with:\n");
  printf("  %s client %s\n\n", "cli_server_pipe", name);

  uint32_t session_num = 0;
  bool listening = true;
  /* One context for every session: the first listen reserves the name for this server. */
  cli_pipe_ctx_struct_t pipe_ctx = CLI_PIPE_CTX_INIT;

  while (listening) {
    printf("Waiting for client connection ...\n");
    fflush(stdout);

    cli_transport_struct_t transport;

    if (cli_named_pipe_listen(name, &pipe_ctx) < 0) {
      /* Creating / opening the pipe failed (another server, bad path, permissions, ...):
       * retrying the same call cannot succeed. */
#if ENV_IS_WINDOWS
      DWORD error = GetLastError();
      if (error == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "cli_named_pipe_listen: another server uses this name\n");
      } else {
        fprintf(stderr, "cli_named_pipe_listen failed (error %lu)\n", error);
      }
#else
      perror("cli_named_pipe_listen");
#endif
      listening = false;
    } else {
      printf("Client connected (session %d)\n", ++session_num);

      cli_pipe_init(&pipe_ctx, &transport);
      cli_init(&demo_cli, "pipe-router", &transport, NULL, demo_cmd_pool, CLI_MAX_COMMANDS);
      register_commands(&demo_cli);
      setup_session(&demo_cli, session_num);
      /* One client at a time: an idle connection is closed so the next client can get in. */
      cli_set_idle_timeout_mode(&demo_cli, CLI_IDLE_TIMEOUT_POLICY_CLOSE);
      int8_t rc = cli_server_loop(&demo_cli);
      printf("Session %d ended (rc=%d)\n", session_num, rc);

      cli_done(&demo_cli);
      cli_named_pipe_disconnect(&pipe_ctx);
    }
  }

  cli_pipe_close(&pipe_ctx); /* releases the name */
  return 1;
}

/*---------------------------------------------------------------------------------------
 * Named Pipe Client Mode (Windows: single-thread poll loop)
 *
 * Windows named pipes give a single duplex HANDLE for both read and write.
 * Putting two threads on that handle serialises all I/O at the kernel level
 * and causes the writer to stall.  Solution: a single-threaded poll loop
 * using PeekNamedPipe + ReadConsoleInputA + WriteFile.
 *---------------------------------------------------------------------------------------*/

#if ENV_IS_WINDOWS

/**
 * @brief Interactive relay for a Windows named-pipe client.
 *
 * Polls the pipe for server data and the console for keyboard input in a
 * single thread, avoiding the blocking / stalling issues with duplex handles.
 *
 * @param[in] ctx  Named-pipe context whose @c h_read == @c h_write.
 */
static void run_interactive_relay(cli_pipe_ctx_struct_t *ctx) {
  HANDLE h_read = (HANDLE)ctx->h_read;
  HANDLE h_write = (HANDLE)ctx->h_write;
  HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

  DWORD old_mode = 0;
  BOOL is_con = GetConsoleMode(h_stdin, &old_mode);
  if (is_con) {
    SetConsoleMode(h_stdin, (old_mode & ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT)) |
                                ENABLE_PROCESSED_INPUT);
  }

  /* The CLI redraws lines and clears the screen with VT100 sequences: have the console
   * interpret them instead of printing them (Windows 10 and later). */
  DWORD old_out_mode = 0;
  BOOL out_vt = GetConsoleMode(h_stdout, &old_out_mode) &&
                SetConsoleMode(h_stdout, old_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

  /* The CLI already sends CR LF: binary mode passes it through instead of turning the LF
   * into another CR LF. */
  fflush(stdout);
  int old_stdout_mode = _setmode(_fileno(stdout), _O_BINARY);

  /* Redirected input from a pipe is read only when data is there, so the server's output
   * keeps appearing while the producer is still running. A file never blocks. */
  bool stdin_is_pipe = (!is_con && GetFileType(h_stdin) == FILE_TYPE_PIPE);

  uint8_t pipe_buf[512];
  int32_t running = 1;
  bool input_open = true; /* redirected stdin not yet at end of file */

  while (running) {
    /* Drain server â†’ client */
    DWORD avail = 0;
    if (!PeekNamedPipe(h_read, NULL, 0, NULL, &avail, NULL)) {
      break;
    }
    while (avail > 0) {
      DWORD chunk = (avail < (DWORD)sizeof(pipe_buf)) ? avail : (DWORD)sizeof(pipe_buf);
      DWORD n = 0;
      if (!ReadFile(h_read, pipe_buf, chunk, &n, NULL) || n == 0) {
        running = 0;
        break;
      }
      fwrite(pipe_buf, 1, (size_t)n, stdout);
      fflush(stdout);
      avail -= n;
    }
    if (!running) {
      break;
    }

    /* Read from stdin */
    if (is_con) {
      DWORD n_events = 0;
      if (!GetNumberOfConsoleInputEvents(h_stdin, &n_events) || n_events == 0) {
        cli_sleep_ms(10U);
        continue;
      }
      INPUT_RECORD rec;
      if (!ReadConsoleInputA(h_stdin, &rec, 1, &n_events) || n_events == 0) {
        continue; /* rec was not filled in */
      }
      if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) {
        continue;
      }
      char c = rec.Event.KeyEvent.uChar.AsciiChar;
      const char *seq = NULL;
      DWORD seq_len = 0;

      if (c == '\0') {
        switch (rec.Event.KeyEvent.wVirtualKeyCode) {
        case VK_UP:
          seq = "\x1b[A";
          seq_len = 3;
          break;
        case VK_DOWN:
          seq = "\x1b[B";
          seq_len = 3;
          break;
        case VK_RIGHT:
          seq = "\x1b[C";
          seq_len = 3;
          break;
        case VK_LEFT:
          seq = "\x1b[D";
          seq_len = 3;
          break;
        case VK_HOME:
          seq = "\x1b[H";
          seq_len = 3;
          break;
        case VK_END:
          seq = "\x1b[F";
          seq_len = 3;
          break;
        case VK_DELETE:
          seq = "\x1b[3~";
          seq_len = 4;
          break;
        default:
          break;
        }

        if (seq) {
          DWORD written = 0;
          if (!WriteFile(h_write, seq, seq_len, &written, NULL)) {
            fprintf(stderr, "[client] WriteFile error %lu\n", GetLastError());
            running = 0;
          }
        }
      } else {
        DWORD written = 0;
        if (!WriteFile(h_write, &c, 1, &written, NULL)) {
          fprintf(stderr, "[client] WriteFile error %lu\n", GetLastError());
          running = 0;
        }
      }
    } else if (!input_open) {
      /* Input exhausted: keep showing the server's replies until it closes the session. */
      cli_sleep_ms(10U);
    } else {
      DWORD pending = 0;
      DWORD n_in = 0;

      if (stdin_is_pipe && PeekNamedPipe(h_stdin, NULL, 0, NULL, &pending, NULL) &&
          pending == 0U) {
        cli_sleep_ms(10U); /* nothing yet (a closed pipe fails the peek: ReadFile sees EOF) */
        continue;
      }
      if (!ReadFile(h_stdin, pipe_buf, (DWORD)sizeof(pipe_buf), &n_in, NULL) || n_in == 0U) {
        input_open = false;
        if (h_write != h_read) {
          /* Separate write end (anonymous pipes): closing it tells the server that the input
           * has ended, which ends the session after the commands sent so far. A duplex
           * named pipe cannot be half-closed: there the session ends on 'exit' / 'quit' in
           * the input or by the server's idle timeout. */
          CloseHandle(h_write);
          ctx->h_write = NULL;
        }
        continue;
      }
      DWORD n = 0;
      if (!WriteFile(h_write, pipe_buf, n_in, &n, NULL)) {
        running = 0;
      }
    }
  }

  if (is_con) {
    SetConsoleMode(h_stdin, old_mode);
  }
  if (old_stdout_mode != -1) {
    fflush(stdout);
    (void)_setmode(_fileno(stdout), old_stdout_mode);
  }
  if (out_vt) {
    SetConsoleMode(h_stdout, old_out_mode);
  }
}

/**
 * @brief Connect to the named-pipe server and relay stdin/stdout (Windows).
 *
 * @param[in] name  Logical pipe name.
 *
 * @return 0 when the session ran, 1 if no connection was made.
 */
static int run_named_client(const char *name) {
  cli_pipe_ctx_struct_t pipe_ctx = CLI_PIPE_CTX_INIT;
  int status = 0;

  printf("Connecting to pipe '%s' ...\n", name);
  fflush(stdout);

  if (cli_named_pipe_connect(name, &pipe_ctx) < 0) {
    if (GetLastError() == ERROR_ACCESS_DENIED) {
      fprintf(stderr, "Refusing pipe '%s': the server is not running as this user\n", name);
    } else {
      fprintf(stderr, "Failed to connect to pipe '%s' (error %lu)\n", name, GetLastError());
    }
    status = 1;
  } else {
    printf("Connected. Type 'exit' or 'quit' to end the session.\n\n");
    fflush(stdout);

    run_interactive_relay(&pipe_ctx);
    cli_pipe_close(&pipe_ctx);
    printf("\nSession closed.\n");
  }

  return status;
}

#else /* POSIX relay two independent fds, two pthreads */

static struct termios s_pipe_orig_term;
static int32_t s_pipe_raw_mode;

static void pipe_raw_mode(void) {
  if (!s_pipe_raw_mode) {
    struct termios term;
    if (tcgetattr(STDIN_FILENO, &s_pipe_orig_term) == 0) {
      term = s_pipe_orig_term;
      /* ISIG off: Ctrl-C / Ctrl-Z go to the CLI instead of killing this client with the
       * terminal still in no-echo mode. IXON / IEXTEN off: Ctrl-S, Ctrl-Q, Ctrl-V and Ctrl-O
       * are passed through instead of being handled by the terminal. */
      term.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN);
      term.c_iflag &= (tcflag_t) ~(IXON);
      term.c_cc[VMIN] = 1;
      term.c_cc[VTIME] = 0;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &term) == 0) {
        s_pipe_raw_mode = 1;
      }
    }
  }
}

static void pipe_restore_term(void) {
  if (s_pipe_raw_mode) {
    tcsetattr(STDIN_FILENO, TCSANOW, &s_pipe_orig_term);
    s_pipe_raw_mode = 0;
  }
}

/**
 * @brief Reader thread: drains the pipe to stdout.
 *
 * @param[in] arg  @c relay_ctx_struct_t pointer.
 *
 * @return @c NULL.
 */
static PFT_THREAD_FN relay_reader_fn(PFT_THREAD_ARG arg) {
  relay_ctx_struct_t *r = (relay_ctx_struct_t *)arg;
  uint8_t buf[512];
  int32_t n;
  while ((n = cli_pipe_read(r->ctx, buf, sizeof(buf))) > 0) {
    fwrite(buf, 1, (size_t)n, stdout);
    fflush(stdout);
  }
  r->done = 1;
  return PFT_THREAD_RET;
}

/**
 * @brief Writer thread: relays stdin to the pipe.
 *
 * @param[in] arg  @c relay_ctx_struct_t pointer.
 *
 * @return @c NULL.
 */
static PFT_THREAD_FN relay_writer_fn(PFT_THREAD_ARG arg) {
  relay_ctx_struct_t *r = (relay_ctx_struct_t *)arg;
  uint8_t byte;
  while (!r->done) {
    ssize_t n = read(STDIN_FILENO, &byte, 1);
    if (n == 0) {
      /* End of input (e.g. piped commands): closing our write end tells the server, which
       * ends the session after the commands sent so far; the reader keeps printing the
       * replies until then. Only this thread uses write_fd. */
      close(r->ctx->write_fd);
      r->ctx->write_fd = -1;
      break;
    }
    if (n < 0 || cli_pipe_write(r->ctx, (const char *)&byte, 1) < 0) {
      break;
    }
  }
  return PFT_THREAD_RET;
}

/**
 * @brief Connect to the named-pipe server and relay stdin/stdout (POSIX).
 *
 * @param[in] name  Logical pipe base path.
 *
 * @return 0 when the session ran, 1 if no connection was made.
 */
static int run_named_client(const char *name) {
  cli_pipe_ctx_struct_t pipe_ctx = CLI_PIPE_CTX_INIT;
  int status = 0;

  printf("Connecting to pipe '%s' ...\n", name);
  fflush(stdout);

  if (cli_named_pipe_connect(name, &pipe_ctx) < 0) {
    if (errno == EPERM) {
      fprintf(stderr, "Refusing '%s.in' / '%s.out': not this user's private FIFOs\n", name,
              name);
    } else {
      perror("cli_named_pipe_connect");
      fprintf(stderr, "Failed to connect to pipe '%s' (is the server running?)\n", name);
    }
    status = 1;
  } else {
    printf("Connected. Type 'exit' or 'quit' to end the session.\n\n");
    fflush(stdout);

    relay_ctx_struct_t rctx = {&pipe_ctx, 0};
    cli_thread_t reader_t;
    cli_thread_t writer_t;

    pipe_raw_mode();
    if (!cli_thread_start(&reader_t, relay_reader_fn, &rctx)) {
      perror("pthread_create");
    } else if (!cli_thread_start(&writer_t, relay_writer_fn, &rctx)) {
      perror("pthread_create");
      pthread_cancel(reader_t); /* read() is a cancellation point */
      cli_thread_join(reader_t);
    } else {
      cli_thread_join(reader_t);
      rctx.done = 1;
      pthread_cancel(writer_t);
      cli_thread_join(writer_t);
    }
    pipe_restore_term();

    cli_pipe_close(&pipe_ctx);
    printf("\nSession closed.\n");
  }

  return status;
}

#endif /* POSIX relay */

/*---------------------------------------------------------------------------------------
 * Anonymous Pipe Scripted Self-Demo
 *
 * Creates a pipe pair internally.  A background thread acts as a scripted
 * "client" feeding stdin into the CLI.  No external tools required.
 *---------------------------------------------------------------------------------------*/

/**
 * @brief Server thread: runs the CLI engine on the server-side pipe.
 *
 * @param[in] arg  @c cli_struct_t pointer.
 *
 * @return Platform thread return value.
 */
#if ENV_IS_WINDOWS
static PFT_THREAD_FN anon_server_fn(PFT_THREAD_ARG arg) {
  cli_struct_t *cli = (cli_struct_t *)arg;
  cli_server_loop(cli);

  /* The server thread is the only owner of the server-side handles: closing them here
   * also tells the relay (main thread) that the session is over. */
  cli_pipe_ctx_struct_t *ctx = (cli_pipe_ctx_struct_t *)cli->transport.ctx;
  if (ctx) {
    cli_pipe_close(ctx);
  }

  cli_done(cli);
  return PFT_THREAD_RET;
}
#endif /* ENV_IS_WINDOWS */

/**
 * @brief Run the anonymous-pipe interactive demo.
 *
 * Creates a pipe pair, starts the CLI engine in a background thread, and
 * reads input from stdin in the main thread.
 */
static void run_anon_demo(void) {
  cli_pipe_ctx_struct_t srv = CLI_PIPE_CTX_INIT, clt = CLI_PIPE_CTX_INIT;
  cli_transport_struct_t srv_tp;

  printf("Anonymous pipe demo INTERACTIVE session\n");
  printf("==========================================\n\n");
  fflush(stdout);

  if (cli_anon_pipe_create(&srv, &clt) < 0) {
    fprintf(stderr, "cli_anon_pipe_create failed\n");
  } else {
    cli_pipe_init(&srv, &srv_tp);
    cli_init(&demo_cli, "anon-demo", &srv_tp, NULL, demo_cmd_pool, CLI_MAX_COMMANDS);
    register_commands(&demo_cli);
    setup_session(&demo_cli, 1);

#if ENV_IS_WINDOWS
    cli_thread_t srv_t;
    if (!cli_thread_start(&srv_t, anon_server_fn, &demo_cli)) {
      fprintf(stderr, "CreateThread failed (%lu)\n", GetLastError());
      cli_done(&demo_cli);
      cli_pipe_close(&srv);
    } else {
      run_interactive_relay(&clt);
      /* Closing the client side breaks the pipe for the server, which then stops and
       * closes its own handles (srv) — only that thread touches them. */
      cli_pipe_close(&clt);
      cli_thread_join(srv_t);
    }
#else
    relay_ctx_struct_t actx = {&clt, 0};
    cli_thread_t reader_t;
    cli_thread_t writer_t;
    bool reader_ok;
    bool writer_ok;

    pipe_raw_mode();
    reader_ok = cli_thread_start(&reader_t, relay_reader_fn, &actx);
    writer_ok = reader_ok && cli_thread_start(&writer_t, relay_writer_fn, &actx);
    if (!writer_ok) {
      perror("pthread_create");
    } else {
      cli_server_loop(&demo_cli);
    }
    cli_done(&demo_cli);

    cli_pipe_close(&srv); /* EOF for the reader thread */
    actx.done = 1;
    if (writer_ok) {
      pthread_cancel(writer_t);
    }
    if (reader_ok) {
      cli_thread_join(reader_t);
    }
    if (writer_ok) {
      cli_thread_join(writer_t);
    }
    pipe_restore_term();
#endif

    cli_pipe_close(&clt);
    printf("\nAnonymous pipe demo finished.\n");
  }
}

/**
 * @brief Print usage information to stderr.
 *
 * @param[in] prog Program name (@c argv[0]).
 */
static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage:\n"
          "  %s                       named pipe server (default name)\n"
          "  %s named [<name>]        named pipe server\n"
          "  %s client [<name>]       named pipe client (relay stdin/stdout)\n"
          "  %s anon                  anonymous pipe self-demo (scripted)\n",
          prog, prog, prog, prog);
}

/*################################### END OF FILE ######################################*/
