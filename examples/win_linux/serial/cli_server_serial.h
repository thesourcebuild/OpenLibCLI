/**
 * @file cli_server_serial.h
 * @brief Self-contained host stdin/stdout serial demo runner.
 *
 * Provides the @c cli_server_serial_run() entry point for the host-side
 * serial demo without depending on @c cli_serial_app.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CLI_SERVER_SERIAL_H
#define CLI_SERVER_SERIAL_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include <cli_env_detect.h>
#include <stdint.h>

/** @defgroup Example_ServerSerial Serial Server Example
 *  @brief Host-side runner for the serial CLI demo using stdin and stdout.
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/* No public constants. */

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

#endif /* CLI_SERVER_SERIAL_H */

/*################################### END OF FILE ######################################*/
