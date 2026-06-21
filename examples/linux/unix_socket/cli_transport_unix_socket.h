/**
 * @file cli_transport_unix_socket.h
 * @brief UNIX domain socket transport for OpenLibCLI.
 *
 * Provides a @c cli_transport_struct_t binding for @c AF_UNIX stream sockets on
 * POSIX platforms. This is the preferred local IPC transport on Linux, macOS,
 * and BSD because it uses one bidirectional file descriptor and one path.
 *
 * @note This header exposes UNIX socket declarations only when
 *       @c ENV_IS_UNIX_LIKE evaluates to non-zero.
 *
 * **Quick-start**
 * @code
 *   #define CLI_MAX_COMMANDS 64
 *
 *   static cli_struct_t      s_cli;
 *   static cli_cmd_struct_t  s_cmd_pool[CLI_MAX_COMMANDS];
 *
 *   cli_cmd_handle_t h_show =
 *       cli_add_command(&s_cli, CLI_CMD_ROOT,
 *                        "show", NULL,
 *                        CLI_PRIV_UNPRIVILEGED,
 *                        CLI_MODE_ANY, "Show commands");
 *   cli_add_command(&s_cli, h_show,
 *                    "version", cmd_show_version,
 *                    CLI_PRIV_UNPRIVILEGED,
 *                    CLI_MODE_ANY, "Show version");
 *
 *   cli_unix_socket_ctx_t us_ctx;
 *   cli_transport_struct_t       tp;
 *   int32_t               conn = cli_unix_accept(listen_fd);
 *   cli_unix_socket_init(&us_ctx, &tp, conn);
 *
 *   cli_init(&s_cli, "host", &tp, NULL, s_cmd_pool, CLI_MAX_COMMANDS);
 *   cli_add_builtin_cmds(&s_cli);
 *
 *   cli_start(&s_cli);
 *
 *   while (1) {
 *       if (cli_session_engine(&s_cli) != CLI_OK) break;
 *   }
 *
 *   cli_done(&s_cli);
 * @endcode
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef OPENLIBCLI_UNIX_SOCKET_H
#define OPENLIBCLI_UNIX_SOCKET_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "../../../cli/cli.h"
#include "../../../cli/cli_env_detect.h"

/** @defgroup CLI_UnixSocket UNIX Socket Transport
 *  @brief AF_UNIX stream-socket IPC transport (POSIX only).
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/** @brief Operation succeeded. */
#define CLI_UNIX_SOCK_OK (0)

/** @brief Operation failed. */
#define CLI_UNIX_SOCK_ERR (-1)

/*=======================================================================================
 * Public Macros
 *=======================================================================================*/

/* No public function-like macros. */

/*=======================================================================================
 * Public Types
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
/**
 * @brief UNIX socket transport context.
 *
 * Populate via @c cli_unix_socket_init(). Treat the field as internal.
 */
typedef struct {
  int32_t fd; /**< Connected socket file descriptor (@c -1 = closed). */
} cli_unix_socket_ctx_struct_t;
#endif

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Public Function Prototypes
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
/**
 * @brief Create a listening UNIX domain socket at @p path.
 *
 * Any stale socket file at @p path is removed before binding so that a
 * crashed server can restart without manual cleanup.
 *
 * @param[in] path  Filesystem path for the socket.
 *
 * @return Listening socket fd (>= 0) on success, @c -1 on error.
 */
int32_t cli_unix_listen(const char *path);

/**
 * @brief Block until one client connects to @p listen_fd.
 *
 * @param[in] listen_fd  Fd returned by @c cli_unix_listen().
 *
 * @return Connected socket fd (>= 0) on success, @c -1 on error.
 */
int32_t cli_unix_accept(int32_t listen_fd);

/**
 * @brief Connect to a server that is blocked in @c cli_unix_accept().
 *
 * @param[in] path  Same path string passed to @c cli_unix_listen().
 *
 * @return Connected socket fd (>= 0) on success, @c -1 on error.
 */
int32_t cli_unix_connect(const char *path);

/**
 * @brief Bind a connected fd to a @c cli_transport_struct_t vtable.
 *
 * @param[out] ctx        Caller-allocated context that must outlive the session.
 * @param[out] transport  Filled with @c read and @c write callbacks
 *                        (@c flush is set to @c NULL).
 * @param[in]  fd         Connected socket fd.
 */
void cli_unix_socket_init(cli_unix_socket_ctx_t *ctx, cli_transport_struct_t *transport,
                          int32_t fd);

/**
 * @brief Close a UNIX socket descriptor.
 *
 * Safe to call with @c fd = -1.
 *
 * @param[in] fd  Socket fd to close.
 */
void cli_unix_close(int32_t fd);

/**
 * @brief Remove the socket path from the filesystem.
 *
 * @param[in] path  Path previously passed to @c cli_unix_listen().
 */
void cli_unix_unlink(const char *path);
#endif

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* OPENLIBCLI_UNIX_SOCKET_H */

/*################################### END OF FILE ######################################*/
