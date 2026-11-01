/**
 * @file cli_transport_pipe.h
 * @brief Named and anonymous pipe transport for OpenLibCLI.
 *
 * Provides @c cli_transport_struct_t bindings for inter-process communication (IPC)
 * via OS pipes on both Windows and POSIX platforms.
 *
 * **Named pipes**
 * - Windows: a single duplex HANDLE (path: @c \\\\.\pipe\<name>), accessible to the
 *   server's user only, local clients only.
 * - POSIX: two FIFOs created on disk (owner-only, mode 0600):
 *   - @c <name>.in bytes flowing into the CLI (CLI reads)
 *   - @c <name>.out bytes flowing out of the CLI (CLI writes)
 *
 * One server per name: the first listen reserves the name (POSIX: a lock on
 * @c <name>.lock; Windows: a named mutex) until @c cli_pipe_close(); a second
 * server fails to start. One client at a time: while a session runs, further
 * clients wait (Windows: no free pipe instance; POSIX: the FIFO names are removed
 * until the next listen). On POSIX two clients that open the FIFOs in the very same
 * instant can both get in; only the owning user can open them at all. Clients check
 * that the server runs as the same user before sending anything, and the Windows
 * server checks every client the same way (POSIX: the FIFOs' mode does). On Windows
 * this compares the processes' users, which a process of the same user at another
 * elevation (an administrator console) may not reveal: such a pair is refused.
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
 *   cli_pipe_ctx_struct_t  pipe_ctx = CLI_PIPE_CTX_INIT;  // kept for every session
 *   cli_transport_struct_t transport;
 *   cli_struct_t           cli;
 *   cli_cmd_struct_t       cmd_pool[CLI_MAX_COMMANDS];
 *
 *   while (cli_named_pipe_listen("/tmp/my_cli", &pipe_ctx) == CLI_PIPE_OK) {
 *       cli_pipe_init(&pipe_ctx, &transport);
 *       cli_init(&cli, "host", &transport, NULL, cmd_pool, CLI_MAX_COMMANDS);
 *       cli_add_builtin_cmds(&cli);
 *       cli_start(&cli);
 *       while (cli_session_engine(&cli) == CLI_OK) {
 *       }
 *       cli_done(&cli);
 *       cli_named_pipe_disconnect(&pipe_ctx);  // ends the session, keeps the name
 *   }
 *   cli_pipe_close(&pipe_ctx);                 // releases the name
 * @endcode
 *
 * **Quick-start - named pipe client**
 * @code
 *   cli_pipe_ctx_struct_t  pipe_ctx = CLI_PIPE_CTX_INIT;
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
 *   cli_pipe_ctx_struct_t  srv = CLI_PIPE_CTX_INIT, clt = CLI_PIPE_CTX_INIT;
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
 * @note On the server end of a named pipe a write that makes no progress for
 *       @c CLI_PIPE_SEND_TIMEOUT_MS (the client stopped reading) fails, which
 *       ends the session, instead of blocking the server.
 *
 * @note Windows: link with @c advapi32 (access control of the named pipe).
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

#ifndef CLI_PIPE_CONNECT_TIMEOUT_MS
/** @brief How long @c cli_named_pipe_connect() waits for the server to accept (it may be
 *         serving another client, or re-creating the pipe between sessions). */
#define CLI_PIPE_CONNECT_TIMEOUT_MS 10000U
#endif

#ifndef CLI_PIPE_SEND_TIMEOUT_MS
/** @brief Server end of a named pipe: a write blocked for this long ends the session. */
#define CLI_PIPE_SEND_TIMEOUT_MS 5000U
#endif

/** @brief Initialiser for a context that holds nothing (every handle / descriptor closed). */
#if ENV_IS_WINDOWS
#define CLI_PIPE_CTX_INIT {NULL, NULL, NULL, NULL}
#else
#define CLI_PIPE_CTX_INIT {-1, -1, -1}
#endif

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
 * Initialise with @c CLI_PIPE_CTX_INIT. A server passes the same context to every
 * @c cli_named_pipe_listen(): the name reserved by the first one stays reserved until
 * @c cli_pipe_close(). The open functions set every other field, to the closed values
 * when they fail, so @c cli_pipe_close() is always safe afterwards. Treat every field
 * as internal.
 */
typedef struct {
#if ENV_IS_WINDOWS
  void *h_read;  /**< Windows HANDLE read end (@c NULL = closed). */
  void *h_write; /**< Windows HANDLE write end (@c NULL = closed). */
  void *h_event; /**< Event for overlapped I/O on a server pipe (@c NULL otherwise). */
  void *h_lock;  /**< Server: mutex reserving the pipe name (@c NULL = none). */
#else
  int32_t read_fd;  /**< POSIX read file descriptor (@c -1 = closed). */
  int32_t write_fd; /**< POSIX write file descriptor (@c -1 = closed). */
  int32_t lock_fd;  /**< Server: descriptor holding the lock on <name>.lock (@c -1 = none). */
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
 * The first call on @p ctx reserves @p name for this server (POSIX: an exclusive lock
 * on @c <name>.lock, which stays on disk; Windows: a named mutex). While another server
 * holds it the call fails (POSIX @c errno = @c EADDRINUSE, Windows
 * @c ERROR_ALREADY_EXISTS).
 * - Windows: creates an instance of @c \\\\.\pipe\<name> that only this user can open,
 *   from this machine only, and waits in @c ConnectNamedPipe().
 * - POSIX: creates new owner-only @c <name>.in and @c <name>.out FIFOs (replacing any
 *   left behind) and waits for a client. Once the client has both open, the names are
 *   removed again, so nobody else can join the session.
 *
 * @param[in]     name  Logical name or base path.
 * @param[in,out] ctx   Server context (@c CLI_PIPE_CTX_INIT before the first call); it
 *                      must outlive the session.
 *
 * @return @c 0 on success, @c -1 on error.
 */
int32_t cli_named_pipe_listen(const char *name, cli_pipe_ctx_struct_t *ctx);

/**
 * @brief Connect to a server blocked in @c cli_named_pipe_listen().
 *
 * While the server is busy (serving another client) or between sessions (pipe
 * not yet re-created), the call keeps waiting, up to @c CLI_PIPE_CONNECT_TIMEOUT_MS
 * in total. A server that does not run as this user is refused (Windows
 * @c ERROR_ACCESS_DENIED; POSIX @c EPERM for FIFOs not owned by this user or open to
 * others).
 * - Windows: the server may only identify, not impersonate, the client.
 * - POSIX: opens @c <name>.in, then @c <name>.out; the connection is made once the
 *   server's first output (banner or prompt) arrives.
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
 * @brief Close everything held by @p ctx, including a server's reservation of its name.
 *
 * Safe to call more than once.
 *
 * @param[in] ctx  Pipe context to close.
 */
void cli_pipe_close(cli_pipe_ctx_struct_t *ctx);

/**
 * @brief End a server session obtained from @c cli_named_pipe_listen().
 *
 * Closes the session's pipe ends without blocking; the name stays reserved for the
 * next @c cli_named_pipe_listen(). Output already written stays readable: the client
 * receives it and then sees the end of the connection. The client cannot reach the
 * next session's pipe (Windows: a new instance; POSIX: new FIFOs), so nothing it still
 * holds can be taken for the next client.
 *
 * @param[in] ctx  Server-side pipe context.
 */
void cli_named_pipe_disconnect(cli_pipe_ctx_struct_t *ctx);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* OPENLIBCLI_PIPE_H */

/*################################### END OF FILE ######################################*/
