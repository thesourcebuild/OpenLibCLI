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
 * ./build/gcc/linux/binaries/cli_server_unix_socket client /tmp/OpenLibCLI.sock
 *
 * Or connect with socat or nc:
 * socat - UNIX-CONNECT:/tmp/OpenLibCLI.sock
 * nc -U /tmp/OpenLibCLI.sock
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
#include <pthread.h>
#include <signal.h>
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
  "*         OpenLibCLI  --  UNIX Domain Socket              *\r\n"                                \
  "*         Pure-C, 100%% Static Allocation CLI Engine        *\r\n"                              \
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
#endif

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
static int8_t cmd_show_socket(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                              const char *argv[]);
static void register_commands(cli_struct_t *cli);
static void setup_session(cli_struct_t *cli, uint32_t session_id);
static void run_server(const char *path);
static void *relay_reader_fn(void *arg);
static void *relay_writer_fn(void *arg);
static void client_raw_mode(void);
static void client_restore_term(void);
static void run_client(const char *path);
static void print_usage(const char *prog);
static int8_t cli_server_loop(cli_struct_t *cli);
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
  const char *path = SOCK_DEFAULT_PATH;
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
      run_client(path);
    } else {
      run_server(path);
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
  int8_t rc;
  while (1) {
    rc = cli_session_engine(cli);
    if (rc != CLI_OK) {
      cli->session_state = CLI_SESSION_STOP;
      break;
    }
  }
  return rc;
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
}

/**
 * @brief Listen on @p path and serve sequential CLI sessions.
 *
 * @param[in] path  UNIX socket filesystem path.
 */
static void run_server(const char *path) {
  signal(SIGPIPE, SIG_IGN);

  int32_t srv_fd = cli_unix_listen(path);
  if (srv_fd < 0) {
    perror("cli_unix_listen");
    fprintf(stderr, "Failed to create socket at '%s'\n", path);
  } else {
    printf("OpenLibCLI UNIX socket server listening.\n");
    printf("  Socket : %s\n\n", path);
    printf("Connect with:\n");
    printf("  socat - UNIX-CONNECT:%s\n", path);
    printf("  nc -U %s\n", path);
    printf("  %s client %s\n\n", "cli_server_unix_socket", path);

    uint32_t session_num = 0;

    for (;;) {
      printf("Waiting for connection ...\n");
      fflush(stdout);

      int32_t conn_fd = cli_unix_accept(srv_fd);
      if (conn_fd < 0) {
        perror("cli_unix_accept");
        continue;
      }

      printf("Client connected (session %d)\n", ++session_num);

      static cli_unix_socket_ctx_t sock_ctx;
      static cli_transport_struct_t transport;
      cli_unix_socket_init(&sock_ctx, &transport, conn_fd);

      cli_init(&demo_cli, "unix-router", &transport, NULL, demo_cmd_pool, CLI_MAX_COMMANDS);
      register_commands(&demo_cli);

      setup_session(&demo_cli, session_num);
      int8_t rc = cli_server_loop(&demo_cli);
      printf("Session %d ended (rc=%d)\n", session_num, rc);

      cli_done(&demo_cli);
      cli_unix_close(conn_fd);
    }
  }
}

/**
 * @brief Reader thread that drains socket output to stdout.
 *
 * @param[in] arg  Relay context pointer.
 *
 * @return Always @c NULL.
 */
static void *relay_reader_fn(void *arg) {
  relay_t *relay = (relay_t *)arg;
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
  relay_t *relay = (relay_t *)arg;
  uint8_t byte;

  while (!relay->done) {
    ssize_t n = read(STDIN_FILENO, &byte, 1);
    if (n <= 0) {
      break;
    }
    if (write(relay->fd, &byte, 1) < 0) {
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
      term.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ECHOE);
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
 */
static void run_client(const char *path) {
  printf("Connecting to %s ...\n", path);
  fflush(stdout);

  int32_t fd = cli_unix_connect(path);
  if (fd < 0) {
    perror("cli_unix_connect");
    fprintf(stderr, "Failed to connect to '%s'\n", path);
    fprintf(stderr, "  (Is the server running?)\n");
  } else {
    printf("Connected. Type 'exit' or 'quit' to end the session.\n\n");
    fflush(stdout);

    relay_t relay = {fd, 0};
    pthread_t reader_thread;
    pthread_t writer_thread;

    client_raw_mode();
    pthread_create(&reader_thread, NULL, relay_reader_fn, &relay);
    pthread_create(&writer_thread, NULL, relay_writer_fn, &relay);

    pthread_join(reader_thread, NULL);
    relay.done = 1;
    pthread_cancel(writer_thread);
    pthread_join(writer_thread, NULL);
    client_restore_term();

    cli_unix_close(fd);
    printf("\nSession closed.\n");
  }
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
          prog, SOCK_DEFAULT_PATH, prog);
}
#endif

/*################################### END OF FILE ######################################*/
