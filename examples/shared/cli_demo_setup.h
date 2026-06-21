/**
 * @file cli_demo_setup.h
 * @brief Shared types, defaults, and declarations for all OpenLibCLI
 *        example programs.
 *
 * Every example includes this header to obtain:
 * - The @c demo_app_data_struct_t per-session userdata struct
 * - Custom CLI mode constants
 * - The @c CLI_DEMO_IDLE_TIMEOUT_SEC compile-time tunable
 * - Shared setup helpers and demo command declarations
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CLI_DEMO_SETUP_H
#define CLI_DEMO_SETUP_H

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include <cli.h>

/** @defgroup Example_DemoConfig Demo Shared Configuration
 *  @brief Common types, constants, and command declarations for all examples.
 *  @{
 */

/*=======================================================================================
 * Public Defines
 *=======================================================================================*/

/** @brief Calculator sub-menu mode. */
#define APP_MODE_CALCULATOR (CLI_MODE_USER_BASE)

/** @brief Arithmetic sub-menu mode. */
#define APP_MODE_ARITHMETIC (CLI_MODE_USER_BASE + 1)

/** @brief Idle timeout in seconds applied to all demo sessions. Set 0 to disable.
 *  @note Defaults to the library default @c CLI_DEFAULT_IDLE_TIMEOUT_SEC. */
#ifndef CLI_DEMO_IDLE_TIMEOUT_SEC
#define CLI_DEMO_IDLE_TIMEOUT_SEC CLI_DEFAULT_IDLE_TIMEOUT_SEC
#endif

/**
 * @brief Command-pool capacity for all demo programs.
 *
 * Each application using OpenLibCLI must define this before including
 * @c cli.h or provide its own pool array (the library no longer defines
 * a default).  Override on the compiler command line with
 * @c -DCLI_MAX_COMMANDS=128 to shrink or grow the pool.
 */
#ifndef CLI_MAX_COMMANDS
#define CLI_MAX_COMMANDS 256
#endif

/** @brief Mirror of @c CLI_ENABLE_TIMMING_STATS for example code. */
#define CLI_DEMO_ENABLE_STATS CLI_ENABLE_TIMMING_STATS

/*=======================================================================================
 * Public Macros
 *=======================================================================================*/

/* No public function-like macros. */

/*=======================================================================================
 * Public Types
 *=======================================================================================*/

/**
 * @brief Per-session application data stored as CLI userdata.
 */
typedef struct {
  uint32_t session_id; /**< Sequential session identifier. */
  uint32_t rx_packets; /**< Simulated RX packet counter. */
  uint32_t tx_packets; /**< Simulated TX packet counter. */
} demo_app_data_struct_t;

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/** @brief Shared CLI instance and command pool for simple single-session demos. */
extern cli_struct_t demo_cli;
extern cli_cmd_struct_t demo_cmd_pool[CLI_MAX_COMMANDS];

/*=======================================================================================
 * Public Function Prototypes
 *=======================================================================================*/

/**
 * @brief Configure a newly created CLI session with demo defaults.
 *
 * @param[in]  cli         Active CLI session returned by @c cli_init().
 * @param[out] app         Caller-allocated userdata struct to initialise.
 * @param[in]  session_id  Session number.
 * @param[in]  rx_packets  Initial RX packet counter value.
 * @param[in]  tx_packets  Initial TX packet counter value.
 * @param[in]  banner      MOTD banner string displayed at login.
 */
void demo_setup_session(cli_struct_t *cli, demo_app_data_struct_t *app, uint32_t session_id,
                        uint32_t rx_packets, uint32_t tx_packets, const char *banner);

/**
 * @brief Register the shared IOS-style command tree.
 *
 * @return Handle for the @c show parent node.
 */
cli_cmd_handle_t demo_register_commands(cli_struct_t *cli);

int8_t cmd_show_version(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);
int8_t cmd_show_counters(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);
int8_t cmd_show_interfaces(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);
int8_t cmd_ping(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);
int8_t cmd_traceroute(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);
int8_t cmd_reboot(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);
int8_t cmd_hostname(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);
int8_t cmd_calculator(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);
int8_t cmd_arithmetic(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);
int8_t cmd_add(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);
int8_t cmd_sub(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]);

/**
 * @brief Sleep for approximately @p ms milliseconds on hosted OS targets.
 *
 * Uses the appropriate platform API on Windows and Unix-like systems. On
 * unsupported targets this function is a no-op.
 *
 * @param[in] ms  Sleep duration in milliseconds.
 */
void cli_sleep_ms(uint32_t ms);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* CLI_DEMO_SETUP_H */

/*################################### END OF FILE ######################################*/
