/**
 * @file cli_server_unix_socket.c
 * @brief OpenLibCLI UNIX domain socket server example.
 *
 * Listens on a UNIX domain socket and serves a CLI session
 * for every incoming connection.
 *
 * make cli_server_unix_socket BUILD_EXAMPLE_UNIX_SOCKET
 *
 * Terminal 1 — server:
 * cd /path/to/OpenLibCLI
 * ./build/gcc/linux/binaries/cli_server_unix_socket
 *
 * Terminal 2 — client:
 * cd /path/to/OpenLibCLI
 * ./build/gcc/linux/binaries/cli_server_unix_socket client
 *
 * The socket is $XDG_RUNTIME_DIR/OpenLibCLI.sock (a per-user directory), or
 * /tmp/OpenLibCLI.sock when that variable is not set. Or connect with socat or nc:
 * socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/OpenLibCLI.sock
 * nc -U $XDG_RUNTIME_DIR/OpenLibCLI.sock
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "../../shared/cli_demo_setup.h"
#include "cli_transport_unix_socket.h"
#include "cli_server_unix_socket.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if ENV_IS_UNIX_LIKE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>
#endif

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
/** @brief MOTD banner displayed to clients on connection. */
#define CLI_DEMO_BANNER_UNIX                                                                       \
  "************************************************************\r\n"                               \
  "*         OpenLibCLI  --  UNIX Domain Socket               *\r\n"                               \
  "*         Pure-C, 100% Static Allocation CLI Engine        *\r\n"                               \
  "************************************************************"
#endif

/*=======================================================================================
 * Private Macros
 *=======================================================================================*/

/* No private function-like macros. */

/*=======================================================================================
 * Private Types
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
/** @brief Shared state for the client relay threads. */
typedef struct {
  int32_t fd;            /**< Connected socket fd. */
  volatile int32_t done; /**< Set to 1 by the reader when the socket closes. */
} relay_struct_t;
#endif

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Private Variables
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
static demo_app_data_struct_t s_app; /**< Per-session application data. */
static struct termios s_orig_term;
static int32_t s_raw_mode;
static volatile sig_atomic_t s_stop; /**< Set by SIGINT / SIGTERM to shut the server down. */
/** Self-pipe: the stop handler writes a byte so a waiting poll() wakes up at once, however
 *  the signal races with the wait (a flag checked before a blocking call can be missed). */
static int s_stop_pipe[2] = {-1, -1};
/** Default socket path in the per-user runtime directory (see default_socket_path()). */
static char s_default_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
#endif

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
static int8_t cmd_show_socket(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                              const char *argv[]);
static void register_commands(cli_struct_t *cli);
static void setup_session(cli_struct_t *cli, uint32_t session_id);
static int run_server(const char *path);
static void *relay_reader_fn(void *arg);
static void *relay_writer_fn(void *arg);
static void client_raw_mode(void);
static void client_restore_term(void);
static int run_client(const char *path);
static void print_usage(const char *prog);
static int8_t cli_server_loop(cli_struct_t *cli);
static void on_stop_signal(int sig);
static bool stop_signals_install(void);
static int wait_readable_or_stop(int32_t fd, int timeout_ms);
static const char *default_socket_path(void);
#endif

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

#if !ENV_IS_UNIX_LIKE
int main(void) {
  fprintf(stderr, "cli_server_unix_socket: UNIX domain sockets are not supported "
                  "on this platform.\n");
  return 1;
}
#else
int main(int argc, char *argv[]) {
  int rc = 0;
  const char *mode = "server";
  const char *path = default_socket_path();
  bool run_app = true;

  if (argc >= 2) {
    if (strcmp(argv[1], "client") == 0) {
      mode = "client";
      if (argc >= 3) {
        path = argv[2];
      }
    } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
      print_usage(argv[0]);
      run_app = false;
    } else {
      path = argv[1];
    }
  }

  if (run_app) {
    if (strcmp(mode, "client") == 0) {
      rc = run_client(path);
    } else {
      rc = run_server(path);
    }
  }

  return rc;
}
#endif

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
/**
 * @brief Blocking session loop runs @c cli_session_engine() until exit.
 *
 * @param[in] cli  Active CLI session.
 *
 * @return @c CLI_ERR_QUIT on clean exit, @c CLI_ERR on transport error.
 */
static int8_t cli_server_loop(cli_struct_t *cli) {
  cli_start(cli);
  int8_t rc = CLI_OK;
  while (rc == CLI_OK && !s_stop) {
    /* Sleep until input arrives (or the poll interval elapses for timers), instead of
     * spinning on the non-blocking available() check. Not before the banner and the first
     * prompt: those are shown without waiting for input. */
    if (cli_get_session_state(cli) == CLI_SESSION_RUN) {
      (void)wait_readable_or_stop(((cli_unix_socket_ctx_struct_t *)cli->transport.ctx)->fd,
                                  CLI_TRANSPORT_POLL_MS);
    }
    if (!s_stop) {
      rc = cli_session_engine(cli);
    }
  }
  if (rc == CLI_OK) {
    rc = CLI_ERR_QUIT; /* stopped by signal */
  }
  cli->session_state = CLI_SESSION_STOP;
  return rc;
}

/**
 * @brief SIGINT / SIGTERM handler: request a clean server shutdown.
 *
 * @param[in] sig  Signal number (unused).
 */
static void on_stop_signal(int sig) {
  int saved_errno = errno;

  (void)sig;
  s_stop = 1;
  /* write() is async-signal-safe and the end is non-blocking. It fails only when the pipe
   * is full, i.e. a wake-up is already pending, which is all that is needed. */
  if (s_stop_pipe[1] >= 0 && write(s_stop_pipe[1], "", 1) < 0) {
    /* Nothing to do: see above. */
  }
  errno = saved_errno;
}

/**
 * @brief Create the stop self-pipe and install the SIGINT / SIGTERM handler.
 *
 * @return @c true on success.
 */
static bool stop_signals_install(void) {
  struct sigaction sa;
  bool ok = (pipe(s_stop_pipe) == 0);

  if (ok) {
    int flags = fcntl(s_stop_pipe[1], F_GETFL, 0);
    ok = (flags >= 0 && fcntl(s_stop_pipe[1], F_SETFL, flags | O_NONBLOCK) == 0);
  }
  if (ok) {
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop_signal;
    sigemptyset(&sa.sa_mask);
    ok = (sigaction(SIGINT, &sa, NULL) == 0 && sigaction(SIGTERM, &sa, NULL) == 0);
  }

  return ok;
}

/**
 * @brief Wait until @p fd is readable, a stop signal arrives, or @p timeout_ms passes.
 *
 * @param[in] fd          Descriptor to wait on.
 * @param[in] timeout_ms  Timeout in milliseconds (-1 = no timeout).
 *
 * @return 1 if @p fd is readable (or reports hang-up / error), 0 otherwise.
 */
static int wait_readable_or_stop(int32_t fd, int timeout_ms) {
  struct pollfd pfd[2];
  int ready;

  pfd[0].fd = fd;
  pfd[0].events = POLLIN;
  pfd[0].revents = 0;
  pfd[1].fd = s_stop_pipe[0];
  pfd[1].events = POLLIN;
  pfd[1].revents = 0;

  ready = poll(pfd, 2, timeout_ms);
  return (ready > 0 && pfd[0].revents != 0) ? 1 : 0;
}

/**
 * @brief Handler for @c show socket.
 *
 * @param[in] cli   Active CLI session.
 * @param[in] cmd   Unused.
 * @param[in] argc  Unused.
 * @param[in] argv  Unused.
 *
 * @return Always @c CLI_OK.
 */
static int8_t cmd_show_socket(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                              const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_println(cli, "Transport    : UNIX domain socket");
  cli_println(cli, "Socket type  : AF_UNIX SOCK_STREAM");
  cli_println(cli, "Direction    : bidirectional (full-duplex)");
  cli_println(cli, "Connect with :");
  cli_println(cli, "  socat - UNIX-CONNECT:<path>");
  cli_println(cli, "  nc -U <path>");
  return CLI_OK;
}

/**
 * @brief Register the shared commands plus the UNIX-socket-specific subcommand.
 */
static void register_commands(cli_struct_t *cli) {
  cli_cmd_handle_t h_show = demo_register_commands(cli);
  cli_register_command(cli, h_show, "socket", cmd_show_socket, CLI_PRIV_USER, CLI_MODE_ANY,
                       "Show socket transport info");
}

/**
 * @brief Apply demo defaults to a newly created CLI session.
 *
 * @param[in] cli         Active CLI session.
 * @param[in] session_id  Sequential session number.
 */
static void setup_session(cli_struct_t *cli, uint32_t session_id) {
  demo_setup_session(cli, &s_app, session_id, 8800, 7500, CLI_DEMO_BANNER_UNIX);
  /* One client at a time: an idle connection is closed so the next client can get in. */
  cli_set_idle_timeout_mode(cli, CLI_IDLE_TIMEOUT_POLICY_CLOSE);
}

/**
 * @brief Listen on @p path and serve sequential CLI sessions.
 *
 * @param[in] path  UNIX socket filesystem path.
 *
 * @return 0 after a clean shutdown, 1 if the server could not be started.
 */
static int run_server(const char *path) {
  int status = 0;
  cli_unix_listener_struct_t listener = {CLI_UNIX_SOCK_ERR, CLI_UNIX_SOCK_ERR};

  signal(SIGPIPE, SIG_IGN);

  if (!stop_signals_install()) {
    perror("stop signal setup");
    status = 1;
  } else if (cli_unix_listen(path, &listener) != CLI_UNIX_SOCK_OK) {
    perror("cli_unix_listen");
    fprintf(stderr, "Failed to create socket at '%s'\n", path);
    status = 1;
  } else {
    /* Listening. */
  }

  if (listener.fd >= 0) {
    printf("OpenLibCLI UNIX socket server listening.\n");
    printf("  Socket : %s\n\n", path);
    printf("Connect with:\n");
    printf("  socat - UNIX-CONNECT:%s\n", path);
    printf("  nc -U %s\n", path);
    printf("  %s client %s\n\n", "cli_server_unix_socket", path);

    uint32_t session_num = 0;

    while (!s_stop) {
      printf("Waiting for connection ...\n");
      fflush(stdout);

      /* Wait for a client or a stop request; only then block in accept(). */
      if (wait_readable_or_stop(listener.fd, -1) == 0) {
        continue; /* stop requested (or poll interrupted): re-check s_stop */
      }

      int32_t conn_fd = cli_unix_accept(listener.fd);
      if (conn_fd < 0) {
        if (errno != EINTR) {
          perror("cli_unix_accept");
          cli_sleep_ms(100U); /* back off on persistent errors (e.g. EMFILE) */
        }
        continue;
      }
      if (!cli_unix_peer_is_self(conn_fd)) {
        /* The 0600 socket should keep other users out; do not rely on it alone. */
        fprintf(stderr, "Refused a connection from another user\n");
        cli_unix_close(conn_fd);
        continue;
      }

      printf("Client connected (session %u)\n", (unsigned)++session_num);

      static cli_unix_socket_ctx_struct_t sock_ctx;
      static cli_transport_struct_t transport;
      cli_unix_socket_init(&sock_ctx, &transport, conn_fd);

      cli_init(&demo_cli, "unix-router", &transport, NULL, demo_cmd_pool, CLI_MAX_COMMANDS);
      register_commands(&demo_cli);

      setup_session(&demo_cli, session_num);
      int8_t rc = cli_server_loop(&demo_cli);
      printf("Session %u ended (rc=%d)\n", (unsigned)session_num, rc);

      cli_done(&demo_cli);
      cli_unix_close(conn_fd);
    }

    cli_unix_listener_close(&listener, path);
    printf("Server stopped; removed %s\n", path);
  }

  return status;
}

/**
 * @brief Reader thread that drains socket output to stdout.
 *
 * @param[in] arg  Relay context pointer.
 *
 * @return Always @c NULL.
 */
static void *relay_reader_fn(void *arg) {
  relay_struct_t *relay = (relay_struct_t *)arg;
  uint8_t buf[512];
  int32_t received;

  while ((received = (int32_t)read(relay->fd, buf, sizeof(buf))) > 0) {
    fwrite(buf, 1, (size_t)received, stdout);
    fflush(stdout);
  }

  relay->done = 1;
  return NULL;
}

/**
 * @brief Writer thread that relays stdin bytes to the socket.
 *
 * @param[in] arg  Relay context pointer.
 *
 * @return Always @c NULL.
 */
static void *relay_writer_fn(void *arg) {
  relay_struct_t *relay = (relay_struct_t *)arg;
  uint8_t byte;

  while (!relay->done) {
    ssize_t n = read(STDIN_FILENO, &byte, 1);
    if (n == 0) {
      /* End of input (e.g. piped commands): tell the server, which ends the session after
       * the commands sent so far; the reader keeps printing the replies until then. */
      (void)shutdown(relay->fd, SHUT_WR);
      break;
    }
    if (n < 0 || write(relay->fd, &byte, 1) < 0) {
      break;
    }
  }

  return NULL;
}

/**
 * @brief Switch the client terminal to raw, no-echo byte input.
 */
static void client_raw_mode(void) {
  if (!s_raw_mode) {
    struct termios term;
    if (tcgetattr(STDIN_FILENO, &s_orig_term) == 0) {
      term = s_orig_term;
      /* ISIG off: Ctrl-C / Ctrl-Z go to the remote CLI instead of killing this client with
       * the terminal still in no-echo mode. IXON / IEXTEN off: Ctrl-S, Ctrl-Q, Ctrl-V and
       * Ctrl-O are passed through instead of being handled by the terminal. */
      term.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN);
      term.c_iflag &= (tcflag_t) ~(IXON);
      term.c_cc[VMIN] = 1;
      term.c_cc[VTIME] = 0;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &term) == 0) {
        s_raw_mode = 1;
      }
    }
  }
}

/**
 * @brief Restore the client terminal settings saved by @c client_raw_mode().
 */
static void client_restore_term(void) {
  if (s_raw_mode) {
    tcsetattr(STDIN_FILENO, TCSANOW, &s_orig_term);
    s_raw_mode = 0;
  }
}

/**
 * @brief Connect to the server at @p path and relay stdin and stdout.
 *
 * @param[in] path  UNIX socket filesystem path.
 *
 * @return 0 when the session ran, 1 if the connection failed.
 */
static int run_client(const char *path) {
  int status = 0;

  /* A keystroke sent just after the server closed must fail with EPIPE, not kill the client
   * with the terminal still in raw mode. */
  signal(SIGPIPE, SIG_IGN);

  printf("Connecting to %s ...\n", path);
  fflush(stdout);

  int32_t fd = cli_unix_connect(path);
  if (fd < 0) {
    perror("cli_unix_connect");
    fprintf(stderr, "Failed to connect to '%s'\n", path);
    fprintf(stderr, "  (Is the server running?)\n");
    status = 1;
  } else if (!cli_unix_peer_is_self(fd)) {
    /* Someone else's socket at this path: never send it a username and password. */
    fprintf(stderr, "Refusing '%s': the server is not running as this user\n", path);
    cli_unix_close(fd);
    status = 1;
  } else {
    printf("Connected. Type 'exit' or 'quit' to end the session.\n\n");
    fflush(stdout);

    relay_struct_t relay = {fd, 0};
    pthread_t reader_thread;
    pthread_t writer_thread;

    client_raw_mode();
    if (pthread_create(&reader_thread, NULL, relay_reader_fn, &relay) != 0) {
      perror("pthread_create");
    } else {
      if (pthread_create(&writer_thread, NULL, relay_writer_fn, &relay) != 0) {
        perror("pthread_create");
        shutdown(fd, SHUT_RDWR); /* unblock the reader */
        pthread_join(reader_thread, NULL);
      } else {
        pthread_join(reader_thread, NULL);
        relay.done = 1;
        pthread_cancel(writer_thread);
        pthread_join(writer_thread, NULL);
      }
    }
    client_restore_term();

    cli_unix_close(fd);
    printf("\nSession closed.\n");
  }

  return status;
}

/**
 * @brief Print usage information to stderr.
 *
 * @param[in] prog  Program name.
 */
static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage:\n"
          "  %s [<path>]           server (default: %s)\n"
          "  %s client [<path>]    client relay (stdin/stdout)\n",
          prog, default_socket_path(), prog);
}

/**
 * @brief Default socket path: @c $XDG_RUNTIME_DIR/OpenLibCLI.sock, a directory only this
 *        user can write to, or @c SOCK_DEFAULT_PATH when the variable is not set (or the
 *        path would be too long).
 *
 * @return Path string (static storage).
 */
static const char *default_socket_path(void) {
  const char *dir = getenv("XDG_RUNTIME_DIR");
  const char *path = SOCK_DEFAULT_PATH;

  if (dir != NULL && dir[0] == '/') {
    int n = snprintf(s_default_path, sizeof(s_default_path), "%s/%s", dir, SOCK_DEFAULT_NAME);
    if (n > 0 && (size_t)n < sizeof(s_default_path)) {
      path = s_default_path;
    }
  }

  return path;
}
#endif

/*################################### END OF FILE ######################################*/
