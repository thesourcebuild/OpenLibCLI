/**
 * @file cli_transport_tcp.h
 * @brief Portable TCP server helpers for OpenLibCLI.
 *
 * Abstracts WinSock2 / POSIX BSD socket differences for creating a listening
 * TCP server and for binding an accepted stream socket to a
 * @c cli_transport_struct_t. Pair with the Telnet transport (@c cli_transport_telnet.h) for a
 * Telnet-aware session, or use @c cli_tcp_init() for a raw byte-stream
 * session.
 *
 * **Typical server loop**
 * @code
 *   #define CLI_MAX_COMMANDS 64
 *
 *   int32_t listen_fd = cli_tcp_listen(23);   // Telnet port
 *   while (1) {
 *       int32_t conn = cli_tcp_accept(listen_fd);
 *
 *       cli_tcp_ctx_struct_t   tcp_ctx;
 *       cli_transport_struct_t transport;
 *       cli_tcp_init(&tcp_ctx, &transport, conn, false);
 *
 *       cli_struct_t       cli;
 *       cli_cmd_struct_t   cmd_pool[CLI_MAX_COMMANDS];
 *
 *       cli_init(&cli, "host", &transport, NULL, cmd_pool, CLI_MAX_COMMANDS);
 *       cli_add_builtin_cmds(&cli);
 *
 *       cli_start(&cli);
 *
 *       while (1) {
 *           if (cli_get_session_state(&cli) == CLI_SESSION_RUN) {
 *               (void)cli_tcp_wait(&tcp_ctx, CLI_TRANSPORT_POLL_MS); // input, or a timer
 *           }
 *           if (cli_session_engine(&cli) != CLI_OK) break;
 *       }
 *
 *       cli_done(&cli);
 *       cli_tcp_close(conn);
 *   }
 *   cli_tcp_close(listen_fd);
 * @endcode
 *
 * @copyright Copyright (c) 2026 thesourcebuild and Muhammad Hassaan Shah
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef OPENLIBCLI_TCP_H
#define OPENLIBCLI_TCP_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "../../../cli/cli.h"

/** @defgroup CLI_TCP TCP Transport Helpers
 *  @brief Portable TCP listen / accept / close utilities.
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/** @brief Operation succeeded. */
#define CLI_TCP_OK (0)

/** @brief Operation failed. */
#define CLI_TCP_ERR (-1)

#ifndef CLI_TCP_SEND_TIMEOUT_MS
/** @brief A send blocked for this long (client not reading) ends the session. */
#define CLI_TCP_SEND_TIMEOUT_MS 5000
#endif

/*=======================================================================================
 * Public Macros
 *=======================================================================================*/

/* No public function-like macros. */

/*=======================================================================================
 * Public Types
 *=======================================================================================*/

/**
 * @brief Context block for the raw TCP byte-stream transport.
 *
 * Allocate one per accepted connection and pass it to @c cli_tcp_init().
 * Treat all fields as internal state.
 */
typedef struct {
  int32_t fd;        /**< Connected socket file descriptor. */
  bool auto_newline; /**< Inject @c '\n' when a line is left open by an idle peer. */
  bool line_open;    /**< Last received byte was not a line terminator. */
  uint8_t rx_buf[CLI_MAX_INPUT_LEN + 2]; /**< Receive buffer. */
  uint16_t rx_len;                       /**< Valid bytes in @c rx_buf. */
  uint16_t rx_pos;                       /**< Next unread byte offset. */
} cli_tcp_ctx_struct_t;

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Public Function Prototypes
 *=======================================================================================*/

/**
 * @brief Create a listening TCP socket bound to all interfaces on @p port.
 *
 * Sets @c SO_REUSEADDR (POSIX) so the server can restart immediately after a
 * crash without "Address already in use" errors. On Windows
 * @c SO_EXCLUSIVEADDRUSE is used instead, because Windows' @c SO_REUSEADDR
 * would let another process bind the same port.
 *
 * @param[in] port  TCP port number to listen on (host byte order).
 *
 * @return Listening socket fd (>= 0) on success, @c -1 on error.
 */
int32_t cli_tcp_listen(uint16_t port);

/**
 * @brief Block until one client connects on @p listen_fd.
 *
 * Sets @c TCP_NODELAY (disables Nagle algorithm for low-latency character
 * echo) and @c SO_SNDTIMEO (@c CLI_TCP_SEND_TIMEOUT_MS) on the accepted socket;
 * on macOS also @c SO_NOSIGPIPE. Reads do not wait: the server loop waits for input with
 * @c cli_tcp_wait().
 *
 * @param[in] listen_fd  Fd returned by @c cli_tcp_listen().
 *
 * @return Connected client fd (>= 0) on success, @c -1 on error.
 */
int32_t cli_tcp_accept(int32_t listen_fd);

/**
 * @brief Bind a connected TCP socket to a @c cli_transport_struct_t vtable.
 *
 * This exposes a plain stream socket to the CLI core without Telnet IAC
 * negotiation. When @p auto_newline is @c true, the transport submits a line
 * that the peer left without @c '\r' / @c '\n' once no further data arrives
 * during a whole @c cli_tcp_wait() (for line-oriented clients that omit the
 * terminator). Leave it @c false for interactive, character-at-a-time clients.
 *
 * @param[out] ctx           Caller-allocated context that must outlive the session.
 * @param[out] transport     Filled with @c read, @c write, and @c flush callbacks.
 * @param[in]  fd            Connected socket fd returned by @c cli_tcp_accept().
 * @param[in]  auto_newline  Enable synthetic newline injection for raw mode.
 */
void cli_tcp_init(cli_tcp_ctx_struct_t *ctx, cli_transport_struct_t *transport, int32_t fd,
                  bool auto_newline);

/**
 * @brief Close a TCP socket.
 *
 * Calls @c close() on POSIX or @c closesocket() on Windows.
 * Safe to call with @c fd = -1.
 *
 * @param[in] fd  Socket fd to close.
 */
void cli_tcp_close(int32_t fd);

/**
 * @brief Wait until the peer sends something, or @p timeout_ms elapses.
 *
 * Call it from the session loop before each @c cli_session_engine() call while the
 * session runs: the transport's reads do not wait, so each engine call returns promptly,
 * also while the peer keeps typing (timers, the idle timeout and lockouts are checked once
 * per call). Returns at once while received input is still waiting. With
 * @c auto_newline, a line the peer left open is ended when nothing arrives during the
 * whole wait.
 *
 * @param[in,out] ctx         Context initialised by @c cli_tcp_init().
 * @param[in]     timeout_ms  Longest wait, e.g. @c CLI_TRANSPORT_POLL_MS.
 *
 * @return 1 when input is waiting, 0 on timeout, -1 on a socket error (the next engine call
 *         reports it).
 */
int32_t cli_tcp_wait(cli_tcp_ctx_struct_t *ctx, uint32_t timeout_ms);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* OPENLIBCLI_TCP_H */

/*################################### END OF FILE ######################################*/
