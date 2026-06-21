/**
 * @file cli_serial.h
 * @brief Platform-agnostic UART / serial CLI application lifecycle.
 *
 * Declares the shared setup, poll, and shutdown interface used by both the
 * host stdin/stdout runner and the Arduino sketch. Serial byte I/O is
 * supplied through @c cli_transport_struct_t, while timing is supplied through
 * @c cli_platform_struct_t at setup.
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

#include "../../cli/cli.h"
#include <stdbool.h>
#include <stdint.h>

/** @defgroup Example_SerialApp Serial App
 *  @brief Shared UART CLI application lifecycle used across all platforms.
 *  {@
 */

/*=======================================================================================
 * Public Function Prototypes
 *=======================================================================================*/

void cli_serial_setup(const cli_transport_struct_t *transport,
                      const cli_platform_struct_t *platform);
void cli_serial_poll(void);
bool cli_serial_app_is_running(void);
void cli_serial_app_shutdown(void);

#if CLI_ENABLE_TIMMING_STATS
void cli_serial_poll_stats_get(cli_tick_stats_struct_t *out);
void cli_serial_poll_stats_reset(void);
#endif

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* OPENLIBCLI_SERIAL_H */
