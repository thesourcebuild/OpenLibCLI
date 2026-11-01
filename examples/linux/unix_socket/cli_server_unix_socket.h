/**
 * @file cli_server_unix_socket.h
 * @brief Types and constants for the OpenLibCLI UNIX socket server example.
 *
 * Shared by @c cli_server_unix_socket.c to centralise the default socket
 * path for the UNIX domain socket CLI server demo.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CLI_SERVER_UNIX_SOCKET_H
#define CLI_SERVER_UNIX_SOCKET_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "../../shared/cli_demo_setup.h"

/** @defgroup Example_UnixSocketServer UNIX Socket Server Example
 *  @brief UNIX domain socket CLI server demo constants.
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/** @brief Socket file name used in @c $XDG_RUNTIME_DIR (the default location). */
#define SOCK_DEFAULT_NAME "OpenLibCLI.sock"

#ifndef SOCK_DEFAULT_PATH
/**
 * @brief Socket path used when @c $XDG_RUNTIME_DIR is not set.
 *
 * @c /tmp is shared by all users: another user could create a socket at this path
 * first. The server then fails with @c EADDRINUSE, and the client refuses a
 * server that does not run as the same user (@c cli_unix_peer_is_self()).
 *
 * Connect with:
 * @code
 *   socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/OpenLibCLI.sock
 *   nc -U $XDG_RUNTIME_DIR/OpenLibCLI.sock
 * @endcode
 *
 * Override at compile time with @c -DSOCK_DEFAULT_PATH=\"/run/my_cli.sock\".
 */
#define SOCK_DEFAULT_PATH "/tmp/OpenLibCLI.sock"
#endif

/*=======================================================================================
 * Public Macros
 *=======================================================================================*/

/* No public function-like macros. */

/*=======================================================================================
 * Public Types
 *=======================================================================================*/

/* No public example-specific types. */

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Public Function Prototypes
 *=======================================================================================*/

/* No public helper functions declared by this header. */

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* CLI_SERVER_UNIX_SOCKET_H */

/*################################### END OF FILE ######################################*/
