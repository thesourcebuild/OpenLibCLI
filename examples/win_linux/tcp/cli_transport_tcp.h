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
 *           if (cli_session_engine(&cli) != CLI_OK) break;
 *       }
 *
 *       cli_done(&cli);
 *       cli_tcp_close(conn);
 *   }
 *   cli_tcp_close(listen_fd);
 * @endcode
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
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
  bool auto_newline; /**< Inject @c '\n' after a burst that lacks line ending. */
  uint8_t rx_buf[CLI_MAX_INPUT_LEN + 2]; /**< Receive buffer plus synthetic newline room. */
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
 * Sets @c SO_REUSEADDR so the server can restart immediately after a crash
 * without "Address already in use" errors.
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
 * echo) and @c SO_RCVTIMEO (set to @c CLI_TRANSPORT_POLL_MS) on the accepted
 * socket.
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
 * negotiation. When @p auto_newline is @c true, the transport appends a
 * synthetic @c '\n' to any received burst that does not already end with
 * @c '\r' or @c '\n'.
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

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* OPENLIBCLI_TCP_H */

/*################################### END OF FILE ######################################*/
