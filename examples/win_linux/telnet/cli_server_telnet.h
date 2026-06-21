/**
 * @file cli_server_telnet.h
 * @brief Types and constants for the OpenLibCLI Telnet server example.
 *
 * Shared by @c cli_server_telnet.c to centralise the default TCP port for
 * the Telnet CLI server demo.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CLI_SERVER_TELNET_H
#define CLI_SERVER_TELNET_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "../../shared/cli_demo_setup.h"

/** @defgroup Example_TelnetServer Telnet Server Example
 *  @brief Telnet CLI server demo constants.
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/**
 * @brief Default TCP port the Telnet server listens on.
 *
 * Connect with: @c telnet @c localhost @c 2323
 *
 * Override at compile time with @c -DTELNET_SERVER_DEFAULT_PORT=23.
 */
#define TELNET_SERVER_DEFAULT_PORT 2323U

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

#endif /* CLI_SERVER_TELNET_H */

/*################################### END OF FILE ######################################*/
