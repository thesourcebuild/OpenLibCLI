/**
 * @file cli_transport_pipe.h
 * @brief Named and anonymous pipe transport for OpenLibCLI.
 *
 * Provides @c cli_transport_struct_t bindings for inter-process communication (IPC)
 * via OS pipes on both Windows and POSIX platforms.
 *
 * **Named pipes**
 * - Windows: a single duplex HANDLE (path: @c \\\\.\pipe\<name>)
 * - POSIX: two FIFOs created on disk:
 *   - @c <name>.in bytes flowing into the CLI (CLI reads)
 *   - @c <name>.out bytes flowing out of the CLI (CLI writes)
 *
 * **Anonymous pipes**
 *
 * Two unidirectional pipe pairs are created internally and exposed as a
 * matched server/client context pair so both sides can communicate
 * bidirectionally. Neither end blocks the other's @c open() call.
 *
 * **Quick-start - named pipe server**
 * @code
 *   #define CLI_MAX_COMMANDS 64
 *
 *   cli_pipe_ctx_struct_t  pipe_ctx = {0};
 *   cli_transport_struct_t transport;
 *
 *   if (cli_named_pipe_listen("/tmp/my_cli", &pipe_ctx) < 0)
 *       // handle error
 *   cli_pipe_init(&pipe_ctx, &transport);
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
 *   cli_pipe_close(&pipe_ctx);
 * @endcode
 *
 * **Quick-start - named pipe client**
 * @code
 *   cli_pipe_ctx_struct_t  pipe_ctx = {0};
 *   cli_transport_struct_t transport;
 *
 *   if (cli_named_pipe_connect("/tmp/my_cli", &pipe_ctx) < 0)
 *       // handle error
 *   cli_pipe_init(&pipe_ctx, &transport);
 *   cli_pipe_close(&pipe_ctx);
 * @endcode
 *
 * **Quick-start - anonymous pipe pair**
 * @code
 *   cli_pipe_ctx_struct_t  srv = {0}, clt = {0};
 *   cli_transport_struct_t srv_tp, clt_tp;
 *
 *   if (cli_anon_pipe_create(&srv, &clt) < 0)
 *       // handle error
 *   cli_pipe_init(&srv, &srv_tp);
 *   cli_pipe_init(&clt, &clt_tp);
 * @endcode
 *
 * @note On POSIX, writing to a pipe whose read end has been closed delivers
 *       SIGPIPE. Applications should ignore SIGPIPE or install a handler
 *       before using this transport.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef OPENLIBCLI_PIPE_H
#define OPENLIBCLI_PIPE_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "../../../cli/cli.h"
#include "../../../cli/cli_env_detect.h"

/** @defgroup CLI_Pipe Pipe Transport
 *  @brief Named and anonymous pipe IPC transport.
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/** @brief Operation succeeded. */
#define CLI_PIPE_OK (0)

/** @brief Operation failed. */
#define CLI_PIPE_ERR (-1)

/*=======================================================================================
 * Public Macros
 *=======================================================================================*/

/* No public function-like macros. */

/*=======================================================================================
 * Public Types
 *=======================================================================================*/

/**
 * @brief Opaque pipe transport context.
 *
 * Zero-initialise before use (@c cli_pipe_ctx_struct_t ctx = {0};), then pass to
 * one of the open or create functions below. Treat every field as internal.
 */
typedef struct {
#if ENV_IS_WINDOWS
  void *h_read;  /**< Windows HANDLE read end (@c NULL = closed). */
  void *h_write; /**< Windows HANDLE write end (@c NULL = closed). */
#else
  int32_t read_fd;  /**< POSIX read file descriptor (@c -1 = closed). */
  int32_t write_fd; /**< POSIX write file descriptor (@c -1 = closed). */
#endif
} cli_pipe_ctx_struct_t;

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Public Function Prototypes
 *=======================================================================================*/

/**
 * @brief Create a named pipe and block until one client connects.
 *
 * - Windows: creates @c \\\\.\pipe\<name> and waits in
 *   @c ConnectNamedPipe().
 * - POSIX: creates @c <name>.in and @c <name>.out FIFOs and opens them in a
 *   deadlock-safe order.
 *
 * @param[in]  name  Logical name or base path.
 * @param[out] ctx   Caller-allocated context that must outlive the session.
 *
 * @return @c 0 on success, @c -1 on error.
 */
int32_t cli_named_pipe_listen(const char *name, cli_pipe_ctx_struct_t *ctx);

/**
 * @brief Connect to a server blocked in @c cli_named_pipe_listen().
 *
 * @param[in]  name  Logical name or base path.
 * @param[out] ctx   Caller-allocated context that must outlive the session.
 *
 * @return @c 0 on success, @c -1 on error.
 */
int32_t cli_named_pipe_connect(const char *name, cli_pipe_ctx_struct_t *ctx);

/**
 * @brief Create a bidirectional anonymous pipe pair.
 *
 * After this call @p server and @p client hold opposite ends of two internal
 * unidirectional pipes so that each side can read what the other writes.
 *
 * @param[out] server  Context for the CLI-engine side.
 * @param[out] client  Context for the peer side.
 *
 * @return @c 0 on success, @c -1 on error.
 */
int32_t cli_anon_pipe_create(cli_pipe_ctx_struct_t *server, cli_pipe_ctx_struct_t *client);

/**
 * @brief Bind an initialised pipe context to a @c cli_transport_struct_t vtable.
 *
 * Call after @c cli_named_pipe_listen(), @c cli_named_pipe_connect(), or
 * @c cli_anon_pipe_create() to obtain the transport struct required by
 * @c cli_init().
 *
 * @param[in]  ctx        Initialised pipe context.
 * @param[out] transport  Filled with @c read and @c write callbacks
 *                        (@c flush is set to @c NULL).
 */
void cli_pipe_init(cli_pipe_ctx_struct_t *ctx, cli_transport_struct_t *transport);

/**
 * @brief Close the pipe handles or file descriptors held by @p ctx.
 *
 * Safe to call more than once.
 *
 * @param[in] ctx  Pipe context to close.
 */
void cli_pipe_close(cli_pipe_ctx_struct_t *ctx);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* OPENLIBCLI_PIPE_H */

/*################################### END OF FILE ######################################*/
