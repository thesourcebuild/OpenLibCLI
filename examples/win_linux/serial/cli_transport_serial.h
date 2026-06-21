/**
 * @file cli_transport_serial.h
 * @brief Host serial stdin/stdout transport for OpenLibCLI.
 *
 * Provides a host-side serial transport that reads from stdin and writes to
 * stdout on Windows and Unix-like systems. A POSIX file-descriptor transport
 * is also declared here for ptys, pipes, and similar stream endpoints.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef OPENLIBCLI_SERIAL_H
#define OPENLIBCLI_SERIAL_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include <cli.h>
#include <cli_env_detect.h>

/** @defgroup CLI_Serial Serial Transport
 *  @brief Host serial stdin/stdout transport and POSIX fd transport.
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/** @brief Operation succeeded. */
#define CLI_SERIAL_OK (0)

/** @brief Operation failed. */
#define CLI_SERIAL_ERR (-1)

/*=======================================================================================
 * Public Macros
 *=======================================================================================*/

/* No public function-like macros. */

/*=======================================================================================
 * Public Types
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
/**
 * @brief Context block for the generic POSIX fd transport.
 */
typedef struct {
  int32_t read_fd;  /**< File descriptor to read from. */
  int32_t write_fd; /**< File descriptor to write to. */
} cli_fd_ctx_struct_t;
#endif

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Public Function Prototypes
 *=======================================================================================*/

/**
 * @brief Bind the host stdin/stdout serial transport to a @c cli_transport_struct_t
 *        vtable.
 *
 * @param[out] transport  Filled with @c read, @c write, and @c flush callbacks.
 */
void cli_serial_init(cli_transport_struct_t *transport);

#if ENV_IS_UNIX_LIKE
/**
 * @brief Bind a transport to a pair of POSIX file descriptors.
 *
 * Suitable for stdin/stdout, a pty, a named pipe, or any pair of file
 * descriptors.
 *
 * @param[out] ctx        Caller-allocated fd context (must outlive session).
 * @param[out] transport  Filled with @c read and @c write callbacks
 *                        (@c flush is set to @c NULL).
 * @param[in]  read_fd    File descriptor to read from.
 * @param[in]  write_fd   File descriptor to write to.
 */
void cli_fd_init(cli_fd_ctx_struct_t *ctx, cli_transport_struct_t *transport, int32_t read_fd,
                 int32_t write_fd);

/**
 * @brief Release a POSIX fd transport context back to the static pool.
 *
 * Call after @c cli_done() to free the pool slot for reuse by a future
 * @c cli_fd_init() call. Zeroes all function pointers in @p transport.
 *
 * @param[in] transport  Transport previously filled by @c cli_fd_init();
 *                       @c NULL or an already released transport is a no-op.
 */
void cli_fd_deinit(cli_transport_struct_t *transport);
#endif

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* OPENLIBCLI_SERIAL_H */

/*################################### END OF FILE ######################################*/
