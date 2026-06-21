/**
 * @file cli_server_tcp.h
 * @brief Types and constants for the OpenLibCLI TCP server example.
 *
 * Shared by @c cli_server_tcp.c to centralise the default port and the
 * compile-time feature switches for Telnet negotiation and raw-TCP
 * auto-newline handling.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CLI_SERVER_TCP_H
#define CLI_SERVER_TCP_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "../../shared/cli_demo_setup.h"

/** @defgroup Example_TCPServer TCP Server Example
 *  @brief TCP CLI server demo constants and feature switches.
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/** @brief Default TCP port the server listens on. */
#define TCP_SERVER_DEFAULT_PORT 2323U

/*
 * @brief Append a synthetic newline to raw TCP bursts that lack one.
 *
 */
#ifndef TCP_RAW_AUTO_NEWLINE
#define TCP_RAW_AUTO_NEWLINE 0
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

#endif /* CLI_SERVER_TCP_H */

/*################################### END OF FILE ######################################*/
