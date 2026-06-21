/**
 * @file cli_demo_setup.c
 * @brief Shared session setup and IOS-style command handlers for all
 *        OpenLibCLI example programs.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include <cli_version.h>
#include <cli_env_detect.h>
#include "cli_demo_setup.h"

#if defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__) || defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif ENV_IS_UNIX_LIKE
#include <time.h>
#endif

#include <stdlib.h>
#include <string.h>

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

/* No private constants. */

/*=======================================================================================
 * Private Macros
 *=======================================================================================*/

/* No private function-like macros. */

/*=======================================================================================
 * Private Types
 *=======================================================================================*/

/* No private types. */

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

static int8_t demo_periodic_cb(cli_struct_t *cli);

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/** @brief Shared CLI instance and command pool for single-session demos. */
cli_struct_t demo_cli;
cli_cmd_struct_t demo_cmd_pool[CLI_MAX_COMMANDS];

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

void demo_setup_session(cli_struct_t *cli, demo_app_data_struct_t *app, uint32_t session_id,
                        uint32_t rx_packets, uint32_t tx_packets, const char *banner) {
  memset(app, 0, sizeof(*app));
  app->session_id = session_id;
  app->rx_packets = rx_packets;
  app->tx_packets = tx_packets;
  cli_set_userdata(cli, app);

  cli_set_banner(cli, banner);
  cli_set_idle_timeout(cli, (uint32_t)CLI_DEMO_IDLE_TIMEOUT_SEC);
  cli_set_enable_secret(cli, "cisco");
#if CLI_ENABLE_MODE_NAMES
  cli_set_mode_name(cli, CLI_MODE_CONFIG, "config");
#endif

  cli_add_user(cli, "admin", "admin", CLI_PRIV_PRIVILEGED);
  cli_add_user(cli, "guest", "guest", CLI_PRIV_USER);
  cli_require_authorization(cli, true);

  cli_set_periodic_cb(cli, demo_periodic_cb);
  cli_set_periodic_interval(cli, 5);

  // Dumb terminal no (ANSI/VT100) support
  // Uncomment the below line
  // cli_set_ansi_supported(cli, false);
}

cli_cmd_handle_t demo_register_commands(cli_struct_t *cli) {
  cli_cmd_handle_t h_show;
  cli_cmd_handle_t h_network;
  cli_cmd_handle_t h_calc;
  cli_cmd_handle_t h_arith;

  cli_add_builtin_cmds(cli);
  cli_register_command(cli, CLI_CMD_ROOT, "hostname", cmd_hostname, CLI_PRIV_PRIVILEGED,
                       CLI_MODE_CONFIG, "Set system hostname");

  h_show = cli_register_command(cli, CLI_CMD_ROOT, "show", NULL, CLI_PRIV_USER, CLI_MODE_ANY,
                                "Show system information");
  cli_register_command(cli, h_show, "version", cmd_show_version, CLI_PRIV_USER, CLI_MODE_ANY,
                       "Show software version");
  cli_register_command(cli, h_show, "counters", cmd_show_counters, CLI_PRIV_USER, CLI_MODE_ANY,
                       "Show packet counters");
  cli_register_command(cli, h_show, "interfaces", cmd_show_interfaces, CLI_PRIV_USER, CLI_MODE_ANY,
                       "Show interface status");

  h_network = cli_register_command(cli, CLI_CMD_ROOT, "network", NULL, CLI_PRIV_USER, CLI_MODE_ANY,
                                   "Network commands");
  cli_register_command(cli, h_network, "ping", cmd_ping, CLI_PRIV_USER, CLI_MODE_ANY,
                       "Send ICMP echo requests");
  cli_register_command(cli, h_network, "traceroute", cmd_traceroute, CLI_PRIV_USER, CLI_MODE_ANY,
                       "Trace route to host");
  cli_register_command(cli, CLI_CMD_ROOT, "reboot", cmd_reboot, CLI_PRIV_PRIVILEGED,
                       CLI_MODE_ENABLE, "Reload / reboot the system");

  h_calc = cli_register_command(cli, CLI_CMD_ROOT, "calc", cmd_calculator, CLI_PRIV_USER,
                                CLI_MODE_EXEC, "Enter calc menu");
  h_arith = cli_register_command(cli, h_calc, "arith", cmd_arithmetic, CLI_PRIV_USER, CLI_MODE_ANY,
                                 "Enter arithmetic submenu");
  cli_register_command(cli, h_arith, "add", cmd_add, CLI_PRIV_USER, CLI_MODE_ANY,
                       "Add two numbers (add <a> <b>)");
  cli_register_command(cli, h_arith, "sub", cmd_sub, CLI_PRIV_USER, CLI_MODE_ANY,
                       "Subtract two numbers (sub <a> <b>)");

  return h_show;
}

int8_t cmd_show_version(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_println(cli, "OpenLibCLI Library version: %s", CLI_VERSION_STRING);
  cli_println(cli, "Platforms   : Linux / macOS / Windows / Embedded");
  cli_println(cli, "Transports  : Telnet, TCP, Serial (UART, stdin/stdout), pipe");
  return CLI_OK;
}

int8_t cmd_show_counters(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  demo_app_data_struct_t *app = (demo_app_data_struct_t *)cli_get_userdata(cli);
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_println(cli, "RX packets : %u", app->rx_packets);
  cli_println(cli, "TX packets : %u", app->tx_packets);
  return CLI_OK;
}

int8_t cmd_show_interfaces(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                           const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_println(cli, "Interface GigE0/0  status up");
  cli_println(cli, "Interface GigE0/1  status down");
  cli_println(cli, "Interface GigE0/2  status up");
  return CLI_OK;
}

int8_t cmd_ping(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  int8_t rc = CLI_ERR;

  if (argc < 2) {
    cli_println(cli, "Usage: ping <host>");
  } else {
    cli_println(cli, "Sending 5, 100-byte pings to %s:", argv[1]);
    for (int16_t i = 0; i < 5; i++) {
      cli_println(cli, "Reply from %s: bytes=100 time<1ms TTL=64", argv[1]);
    }
    cli_println(cli, "Ping statistics: 5 sent, 5 received, 0%% loss");
    rc = CLI_OK;
  }
  return rc;
}

int8_t cmd_traceroute(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  int8_t rc = CLI_ERR;

  if (argc < 2) {
    cli_println(cli, "Usage: traceroute <host>");
  } else {
    cli_println(cli, "Tracing route to %s:", argv[1]);
    cli_println(cli, "  1  192.168.1.254   <1 ms");
    cli_println(cli, "  2  10.0.0.1        1 ms");
    cli_println(cli, "  3  %s  2 ms", argv[1]);
    rc = CLI_OK;
  }
  return rc;
}

int8_t cmd_reboot(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_println(cli, "System reload scheduled.");
  cli_println(cli, "(Demo mode: no actual reload performed)");
  return CLI_OK;
}

int8_t cmd_hostname(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  int8_t rc = CLI_ERR;

  if (argc < 2) {
    cli_println(cli, "Usage: hostname <name>");
  } else {
    cli_set_hostname(cli, argv[1]);
    cli_println(cli, "Hostname set to: %s", argv[1]);
    rc = CLI_OK;
  }

  return rc;
}

int8_t cmd_calculator(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
#if CLI_ENABLE_MODE_NAMES
  cli_set_mode_name(cli, APP_MODE_CALCULATOR, "");
  cli_set_mode_name(cli, APP_MODE_ARITHMETIC, "");
#endif
  cli_set_mode(cli, APP_MODE_CALCULATOR);
  return CLI_OK;
}

int8_t cmd_arithmetic(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_set_mode(cli, APP_MODE_ARITHMETIC);
  return CLI_OK;
}

int8_t cmd_add(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  int8_t rc = CLI_ERR;

  if (argc < 3 || argc > 4) {
    cli_println(cli, "Usage: add <a> <b>  or  add <a> + <b>");
  } else if (argc == 4 && strcmp(argv[2], "+") != 0) {
    cli_println(cli, "Usage: add <a> + <b>");
  } else {
    int32_t a;
    int32_t b;
    cli_sscanf(argv[1], "%u", &a);
    cli_sscanf(argv[2], "%u", &b);
    cli_println(cli, "%d", a + b);
    rc = CLI_OK;
  }

  return rc;
}

int8_t cmd_sub(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  int8_t rc = CLI_ERR;

  if (argc < 3 || argc > 4) {
    cli_println(cli, "Usage: sub <a> <b>  or  sub <a> - <b>");
  } else if (argc == 4 && strcmp(argv[2], "-") != 0) {
    cli_println(cli, "Usage: sub <a> - <b>");
  } else {
    int32_t a;
    int32_t b;
    cli_sscanf(argv[1], "%u", &a);
    cli_sscanf(argv[2], "%u", &b);
    cli_println(cli, "%d", a - b);
    rc = CLI_OK;
  }

  return rc;
}

void cli_sleep_ms(uint32_t ms) {
#if ENV_IS_WINDOWS
  Sleep((DWORD)ms);
#elif ENV_IS_UNIX_LIKE
  struct timespec ts;
  ts.tv_sec = (time_t)(ms / 1000U);
  ts.tv_nsec = (long)((ms % 1000U) * 1000000UL);
  (void)nanosleep(&ts, NULL);
#else
  (void)ms;
#endif
}

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

static int8_t demo_periodic_cb(cli_struct_t *cli) {
  demo_app_data_struct_t *app = (demo_app_data_struct_t *)cli_get_userdata(cli);

  if (app) {
    app->rx_packets += 7;
    app->tx_packets += 3;
  }

  return CLI_OK;
}

/*################################### END OF FILE ######################################*/
