/**
 * @file cli_server_tcp.c
 * @brief OpenLibCLI TCP server example.
 *
 * Demonstrates a TCP CLI server with optional Telnet negotiation or raw TCP
 * mode.
 *
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/
#include "../../../cli/cli_env_detect.h"
#include "../../shared/cli_demo_setup.h"
#include "cli_transport_tcp.h"
#include "cli_server_tcp.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

/** @brief MOTD banner for the TCP transport demo. */
#define CLI_DEMO_BANNER_TCP                                                                        \
  "************************************************************\r\n"                               \
  "*              OpenLibCLI  --  TCP Transport               *\r\n"                               \
  "*         Pure-C, 100% Static Allocation CLI Engine        *\r\n"                               \
  "************************************************************"

/*=======================================================================================
 * Private Macros
 *=======================================================================================*/

/* No private function-like macros. */

/*=======================================================================================
 * Private Types
 *=======================================================================================*/

/* No private types. */

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Private Variables
 *=======================================================================================*/

static demo_app_data_struct_t s_app; /**< Per-session application data. */

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

static void configure_session(cli_struct_t *cli, uint32_t session_num);
static int8_t cli_server_loop(cli_struct_t *cli);

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

/**
 * @brief Entry point for the TCP transport demo executable. Set up the TCP CLI server session
 * and run it until it exits.
 *
 * Configures the TCP listener, registers the demo command set, creates one
 * CLI session, and drives @c cli_session_engine() until the session ends.
 *
 * @param[in] argc Command-line argument count.
 * @param[in] argv Command-line arguments.
 *
 * @return @c 0 on clean exit.
 */
int main(int argc, char *argv[]) {
  uint16_t port = TCP_SERVER_DEFAULT_PORT;

  if (argc > 1) {
    port = (uint16_t)strtol(argv[1], NULL, 10);
  }

  int32_t listen_fd = cli_tcp_listen(port);
  if (listen_fd < 0) {
    fprintf(stderr, "Failed to listen on TCP port %u\n", port);
    return 1;
  }

  printf("OpenLibCLI TCP CLI server listening on port %u\n", port);
  printf("Mode: raw TCP\n");
  printf("Connect with: PuTTY (Raw mode)\n");
  printf("PuTTY setting required:\n");
  printf("  Option A: PuTTY > Terminal > Local echo = Force off, Local line editing = Force "
         "off\n");

  printf("Alternative: Use Telnet mode instead (PuTTY protocol = Telnet, port %u)\n\n", port);

  printf("  Option B: Use Telnet mode instead (PuTTY protocol = Telnet, port %u)\n", port);
  printf("            Run the Telnet server (cli_server_telnet.exe)\n\n");

  uint32_t session_num = 0;

  while (1) {
    int32_t client_fd = cli_tcp_accept(listen_fd);
    if (client_fd < 0) {
      fprintf(stderr, "accept() failed\n");
      continue;
    }

    printf("TCP client connected (session %d)\n", ++session_num);

    cli_tcp_ctx_struct_t tcp_ctx;
    cli_transport_struct_t transport;
    cli_tcp_init(&tcp_ctx, &transport, client_fd, (TCP_RAW_AUTO_NEWLINE != 0));

    cli_init(&demo_cli, "router", &transport, NULL, demo_cmd_pool, CLI_MAX_COMMANDS);
    demo_register_commands(&demo_cli);

    configure_session(&demo_cli, session_num);
    int8_t rc = cli_server_loop(&demo_cli);
    printf("TCP session %d ended (rc=%d)\n", session_num, rc);

    cli_done(&demo_cli);
    cli_tcp_close(client_fd);
  }
}

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

/**
 * @brief Blocking session loop drives the CLI until the session ends.
 *
 * @param[in] cli  Active CLI session.
 *
 * @return Exit code from @c cli_session_engine().
 */
/**
 * @brief Blocking session loop runs @c cli_session_engine() until exit.
 *
 * @param[in] cli  Active CLI session.
 *
 * @return @c CLI_ERR_QUIT on clean exit, @c CLI_ERR on transport error.
 */
static int8_t cli_server_loop(cli_struct_t *cli) {
  cli_start(cli);
  int8_t rc;
  while (1) {
    rc = cli_session_engine(cli);
    if (rc != CLI_OK) {
      cli->session_state = CLI_SESSION_STOP;
      break;
    }
  }
  return rc;
}

/**
 * @brief Apply demo defaults to a newly created CLI session.
 *
 * @param[in] cli          Active CLI session.
 * @param[in] session_num  Sequential session number.
 */
static void configure_session(cli_struct_t *cli, uint32_t session_num) {
  demo_setup_session(cli, &s_app, session_num, 12345, 11000, CLI_DEMO_BANNER_TCP);
}

/*################################### END OF FILE ######################################*/
