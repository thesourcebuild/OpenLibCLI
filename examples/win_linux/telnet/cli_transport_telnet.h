/**
 * @file cli_transport_telnet.h
 * @brief Telnet transport layer for OpenLibCLI.
 *
 * Handles RFC 854 IAC option negotiation and presents a clean byte stream to
 * the CLI engine. Works on both POSIX sockets and Winsock.
 *
 * The transport sends the following options to the client immediately after
 * connection (via @c cli_telnet_negotiate()):
 * - @c WILL ECHO      - server handles echo, not the client
 * - @c WILL SGA       - suppress go-ahead (full-duplex)
 * - @c DONT LINEMODE  - character-at-a-time mode
 * - @c DO   NAWS      - client reports window size (optional)
 *
 * **Typical usage**
 * @code
 *   #define CLI_MAX_COMMANDS 64
 *
 *   int32_t conn = cli_tcp_accept(listen_fd);
 *
 *   cli_telnet_ctx_struct_t  ctx;
 *   cli_transport_struct_t   transport;
 *   cli_telnet_init(&ctx, &transport, conn);
 *   cli_telnet_negotiate(&ctx);
 *
 *   cli_struct_t       cli;
 *   cli_cmd_struct_t   cmd_pool[CLI_MAX_COMMANDS];
 *
 *   cli_init(&cli, "host", &transport, NULL, cmd_pool, CLI_MAX_COMMANDS);
 *   cli_add_builtin_cmds(&cli);
 *
 *   cli_start(&cli);
 *
 *   while (1) {
 *       if (cli_session_engine(&cli) != CLI_OK) break;
 *   }
 *
 *   cli_done(&cli);
 *   cli_tcp_close(conn);
 * @endcode
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef OPENLIBCLI_TELNET_H
#define OPENLIBCLI_TELNET_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include <cli.h>

/** @defgroup CLI_Telnet Telnet Transport
 *  @brief RFC 854 Telnet IAC negotiation and byte-stream transport.
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/** @brief Operation succeeded. */
#define CLI_TELNET_OK (0)

/** @brief Operation failed or the peer disconnected. */
#define CLI_TELNET_ERR (-1)

/*=======================================================================================
 * Public Macros
 *=======================================================================================*/

/* No public function-like macros. */

/*=======================================================================================
 * Public Types
 *=======================================================================================*/

/**
 * @brief Context block for the Telnet transport.
 *
 * Allocate one per accepted connection (stack or static); pass to
 * @c cli_telnet_init(). Treat all fields as internal state.
 */
typedef struct {
  int32_t fd;                    /**< Connected socket file descriptor. */
  uint8_t state;                 /**< IAC parser state. */
  uint8_t verb;                  /**< IAC verb byte being processed. */
  bool negotiated;               /**< True once initial option negotiation has been sent. */
  bool local_echo;               /**< True when this endpoint has enabled local echo. */
  bool local_sga;                /**< True when this endpoint has enabled suppress-go-ahead. */
  bool remote_naws;              /**< True when the remote endpoint has enabled NAWS. */
  bool remote_linemode_rejected; /**< True once remote linemode has been rejected. */
  uint8_t rx_buf[256];           /**< Receive staging buffer. */
  uint16_t rx_len;               /**< Bytes currently stored in @c rx_buf. */
  uint16_t rx_pos;               /**< Read cursor within @c rx_buf. */
  uint8_t tx_buf[64];            /**< Reserved transmit staging buffer. */
  uint16_t tx_len;               /**< Reserved transmit staging length. */
} cli_telnet_ctx_struct_t;

/** @brief Invalid socket / fd sentinel for Telnet helpers. */
#define CLI_TELNET_INVALID_SOCKET (-1)

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Public Function Prototypes
 *=======================================================================================*/

/**
 * @brief Initialise a Telnet transport context and fill the vtable.
 *
 * Zeroes @p ctx, stores @p fd, sets @c SO_RCVTIMEO on the socket
 * (@c CLI_TRANSPORT_POLL_MS), and wires the @c available, @c read,
 * @c write, and @c flush callbacks into @p transport.
 *
 * @param[out] ctx        Caller-allocated context (must outlive the session).
 * @param[out] transport  Filled with @c available, @c read, @c write, and @c flush callbacks.
 * @param[in]  fd         Accepted socket fd (POSIX @c int32_t or Winsock
 *                        @c SOCKET cast to @c int32_t).
 */
void cli_telnet_init(cli_telnet_ctx_struct_t *ctx, cli_transport_struct_t *transport, int32_t fd);

/**
 * @brief Send the initial IAC option bundle to the connected client.
 *
 * Transmits @c WILL ECHO, @c WILL SGA, @c DONT LINEMODE, and @c DO NAWS in
 * one write. Call once immediately after @c cli_telnet_init().
 *
 * @param[in] ctx  Initialised Telnet context.
 */
void cli_telnet_negotiate(cli_telnet_ctx_struct_t *ctx);

/**
 * @brief Create a listening TCP socket for Telnet sessions.
 *
 * @param[in] port  TCP port in host byte order.
 *
 * @return Listening fd (>=0) on success, @c CLI_TELNET_INVALID_SOCKET on error.
 */
int32_t cli_telnet_listen(uint16_t port);

/**
 * @brief Accept one client from a Telnet listening socket.
 *
 * Configures @c TCP_NODELAY and @c SO_RCVTIMEO
 * (@c CLI_TRANSPORT_POLL_MS) on the accepted client socket.
 *
 * @param[in] listen_fd  Fd returned by @c cli_telnet_listen().
 *
 * @return Connected client fd (>=0) on success, @c CLI_TELNET_INVALID_SOCKET on error.
 */
int32_t cli_telnet_accept(int32_t listen_fd);

/**
 * @brief Read raw socket data and process IAC negotiation.
 *
 * Calls @c recv() on the socket (which has @c SO_RCVTIMEO set to
 * @c CLI_TRANSPORT_POLL_MS), runs the telnet IAC state machine, and
 * appends decoded user bytes to the internal rx buffer. The transport's
 * @c available callback calls this automatically; applications normally do
 * not need to call it directly.
 *
 * @param[in] ctx  Telnet context (set by cli_telnet_init).
 *
 * @return @c CLI_OK on success (or no data available),
 *         @c CLI_ERR on disconnect or fatal socket error.
 */
int8_t cli_telnet_handler(cli_telnet_ctx_struct_t *ctx);

/**
 * @brief Close a Telnet socket.
 *
 * Safe to call with an invalid fd.
 *
 * @param[in] fd  Socket fd to close.
 */
void cli_telnet_close(int32_t fd);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* OPENLIBCLI_TELNET_H */

/*################################### END OF FILE ######################################*/
