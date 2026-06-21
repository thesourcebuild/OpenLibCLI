/**
 * @file cli_server_pipe.h
 * @brief Types and constants for the OpenLibCLI pipe server example.
 *
 * Shared by @c cli_server_pipe.c to centralise the default pipe name used
 * in named-pipe and anonymous-pipe demo modes.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CLI_SERVER_PIPE_H
#define CLI_SERVER_PIPE_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "../../shared/cli_demo_setup.h"

/** @defgroup Example_Pipe Pipe Server Example
 *  @brief Named and anonymous pipe CLI server demo constants.
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/**
 * @brief Default logical name or base path for the named pipe.
 *
 * - Windows: expanded to @c \\\\.\pipe\\OpenLibCLI
 * - POSIX: two FIFOs created as @c OpenLibCLI.in and @c OpenLibCLI.out
 *
 * Override at compile time with @c -DPIPE_DEFAULT_NAME=\"my_pipe\".
 */
#define PIPE_DEFAULT_NAME "OpenLibCLI"

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

#endif /* CLI_SERVER_PIPE_H */

/*################################### END OF FILE ######################################*/
