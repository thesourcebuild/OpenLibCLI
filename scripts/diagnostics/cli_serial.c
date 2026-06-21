/**
 * @file cli_serial.c
 * @brief Platform-agnostic UART / serial CLI application implementation.
 *
 * Compiles on the host and Arduino by accepting transport/platform vtables
 * from the embedding application.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ARDUINO)
#include "../../cli/cli.c"
#include "../../cli/cli.h"
#include "../../examples/shared/cli_demo_setup.h"
#else
#include "../../cli/cli.h"
#include "../../examples/shared/cli_demo_setup.h"
#endif
#include "cli_serial.h"

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

/** @brief MOTD banner for the UART / serial transport demo. */
#define CLI_DEMO_BANNER_SERIAL                                                                     \
  "************************************************************\r\n"                               \
  "*             OpenLibCLI  --  Serial (UART)               *\r\n"                                \
  "*         Pure-C, 100% Static Allocation CLI Engine        *\r\n"                               \
  "************************************************************"

/*=======================================================================================
 * Private Variables
 *=======================================================================================*/

static demo_app_data_struct_t s_app;

static cli_struct_t s_cli;

#if defined(__AVR__) || defined(ARDUINO_ARCH_AVR)
#define CLI_SERIAL_CMD_POOL_SIZE 12
#else
#define CLI_SERIAL_CMD_POOL_SIZE CLI_MAX_COMMANDS
#endif
static cli_cmd_struct_t s_cmd_pool[CLI_SERIAL_CMD_POOL_SIZE];

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

void cli_serial_setup(const cli_transport_struct_t *transport,
                      const cli_platform_struct_t *platform) {
  if (!transport) {
    return;
  }

  cli_init(&s_cli, "MCU", transport, platform, s_cmd_pool, CLI_SERIAL_CMD_POOL_SIZE);

  cli_set_exit_root_policy(&s_cli, CLI_EXIT_ROOT_POLICY_RESET_SESSION);
  cli_set_quit_policy(&s_cli, CLI_QUIT_POLICY_RESET_SESSION);

#if defined(__AVR__) || defined(ARDUINO_ARCH_AVR)
  memset(&s_app, 0, sizeof(s_app));
  s_app.session_id = 1;
  s_app.rx_packets = 12345;
  s_app.tx_packets = 11000;
  cli_set_userdata(&s_cli, &s_app);
  cli_set_banner(&s_cli, "OpenLibCLI AVR CLI");
  cli_set_enable_secret(&s_cli, "cisco");
#if CLI_ENABLE_MODE_NAMES
  cli_set_mode_name(&s_cli, CLI_MODE_CONFIG, "config");
#endif
  cli_add_user(&s_cli, "admin", "admin", CLI_PRIV_PRIVILEGED);
  cli_require_authorization(&s_cli, true);

  cli_cmd_handle_t h_show = cli_register_command(&s_cli, CLI_CMD_ROOT, "show", NULL, CLI_PRIV_USER,
                                                 CLI_MODE_ANY, "Show info");
  cli_register_command(&s_cli, h_show, "version", cmd_show_version, CLI_PRIV_USER, CLI_MODE_ANY,
                       "Version");
#else
  demo_setup_session(&s_cli, &s_app, 1, 12345, 11000, CLI_DEMO_BANNER_SERIAL);
  demo_register_commands(&s_cli);
#endif

  cli_start(&s_cli);
}

void cli_serial_poll(void) {
  if (cli_session_engine(&s_cli) != CLI_OK) {
    cli_serial_app_shutdown();
  }
}

#if CLI_ENABLE_TIMMING_STATS
void cli_serial_poll_stats_get(cli_tick_stats_struct_t *out) {
  cli_tick_stats_get(&s_cli, out);
}

void cli_serial_poll_stats_reset(void) {
  cli_tick_stats_reset(&s_cli);
}
#endif

bool cli_serial_app_is_running(void) {
  return s_cli.hostname[0] != '\0';
}

void cli_serial_app_shutdown(void) {
  cli_done(&s_cli);
}

/*################################### END OF FILE ######################################*/
