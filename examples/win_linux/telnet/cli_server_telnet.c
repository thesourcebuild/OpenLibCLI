/**
 * @file cli_server_telnet.c
 * @brief OpenLibCLI Telnet server example.
 *
 * Listens on TCP port 2323 by default and serves a CLI
 * session for each incoming TCP connection.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/
#include <cli_env_detect.h>
#include "../../shared/cli_demo_setup.h"
#include <utils/cli_args_parse.h>
#include "cli_transport_telnet.h"
#include "cli_server_telnet.h"

#include <stdio.h>
#include <stdlib.h>

#if ENV_IS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/time.h>
#endif

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

/** @brief MOTD banner displayed to clients on Telnet connection. */
#define CLI_DEMO_BANNER_TELNET                                                                     \
  "************************************************************\r\n"                               \
  "*             OpenLibCLI  --  Telnet Transport             *\r\n"                               \
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

/** @brief Per-session application data for the Telnet demo. */
static demo_app_data_struct_t s_app;

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

#if CLI_ENABLE_TIMMING_STATS
static uint32_t server_micros(void *ctx);
#endif
static int8_t cli_server_loop(cli_struct_t *cli);

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/
/**
 * @brief Entry point for the Telnet transport demo executable. Set up the Telnet CLI server session
 * and run it until it exits.
 *
 * Configures the Telnet listener, registers the demo command set, creates one
 * CLI session, and drives @c cli_session_engine() until the session ends.
 *
 * @param[in] argc Command-line argument count.
 * @param[in] argv Command-line arguments.
 *
 * @return @c 0 on clean exit.
 */

int main(int argc, char *argv[]) {
  uint16_t port = TELNET_SERVER_DEFAULT_PORT;
  int result = 0;

  int32_t listen_fd = CLI_TELNET_INVALID_SOCKET;

  if (argc > 1 && (!parse_u16(argv[1], &port) || port == 0U)) {
    /* 0 would bind a random ephemeral port; anything else invalid must not be guessed at. */
    fprintf(stderr, "Invalid port '%s' (expected 1..65535)\n", argv[1]);
    result = 1;
  } else {
#if !ENV_IS_WINDOWS
    /* A client that disconnects while output is being sent must end its session (EPIPE),
     * not kill the server. */
    signal(SIGPIPE, SIG_IGN);
#endif
    listen_fd = cli_telnet_listen(port);
  }

  if (result != 0) {
    /* Invalid arguments: reported above. */
  } else if (listen_fd < 0) {
    fprintf(stderr, "Failed to listen on port %u\n", port);
    result = 1;
  } else {

    printf("OpenLibCLI CLI server listening on port %u\n", port);
    printf("Connect with:  telnet localhost %u\n\n", port);
    /* Status lines are shown at once, also when stdout is redirected (fully buffered then)
     * and the server is stopped by a signal. */
    fflush(stdout);

    uint32_t session_num = 0;

    while (true) {
      int32_t client_fd = cli_telnet_accept(listen_fd);
      if (client_fd < 0) {
        fprintf(stderr, "accept() failed\n");
        cli_sleep_ms(100U); /* back off: persistent errors (e.g. EMFILE) would spin */
      } else {

        printf("Client connected (session %u)\n", ++session_num);
        fflush(stdout);

        static cli_telnet_ctx_struct_t telnet_ctx;
        static cli_transport_struct_t transport;
        cli_telnet_init(&telnet_ctx, &transport, client_fd);
        cli_telnet_negotiate(&telnet_ctx);

#if CLI_ENABLE_TIMMING_STATS
        cli_platform_struct_t platform = {0};
        platform.micros = server_micros;
        platform.micros_ctx = NULL;
        cli_init(&demo_cli, "router", &transport, &platform, demo_cmd_pool, CLI_MAX_COMMANDS);
#else
        cli_init(&demo_cli, "router", &transport, NULL, demo_cmd_pool, CLI_MAX_COMMANDS);
#endif

        //>! These are the defaults. For demonstration setting them explicitly here.
        cli_set_exit_root_policy(&demo_cli, CLI_EXIT_ROOT_POLICY_CLOSE_SESSION);
        cli_set_quit_policy(&demo_cli, CLI_QUIT_POLICY_CLOSE_SESSION);
        cli_set_auth_failure_mode(&demo_cli, CLI_AUTH_FAILURE_MODE_CLOSE);

        // cli_set_exit_root_policy(&demo_cli, CLI_EXIT_ROOT_POLICY_RESET_SESSION);
        // cli_set_quit_policy(&demo_cli, CLI_QUIT_POLICY_RESET_SESSION);
        // cli_set_auth_failure_mode(&demo_cli, CLI_AUTH_FAILURE_MODE_LOCKOUT);

        demo_register_commands(&demo_cli);
        demo_setup_session(&demo_cli, &s_app, session_num, 12345, 11000, CLI_DEMO_BANNER_TELNET);
        /* One client at a time: an idle connection is closed so the next client can get in. */
        cli_set_idle_timeout_mode(&demo_cli, CLI_IDLE_TIMEOUT_POLICY_CLOSE);

        int8_t rc = cli_server_loop(&demo_cli);

#if CLI_ENABLE_TIMMING_STATS
        cli_tick_stats_struct_t stats;
        cli_tick_stats_get(&demo_cli, &stats);
        printf("Session %u ended (rc=%d)  tick us  min=%u  max=%u  avg=%u  n=%u\n", session_num, rc,
               stats.min_us, stats.max_us, stats.avg_us, stats.count);
#else
        printf("Session %u ended (rc=%d)\n", session_num, rc);
#endif
        fflush(stdout);

        cli_done(&demo_cli);
        cli_telnet_close(client_fd);
      }
    }
  }

  return result;
}

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

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

#if CLI_ENABLE_TIMMING_STATS
/**
 * @brief High-resolution microsecond time source for tick profiling.
 *
 * @param[in] ctx  Unused.
 *
 * @return Microseconds since an arbitrary epoch.
 */
static uint32_t server_micros(void *ctx) {
  (void)ctx;

#if defined(_WIN32)
  LARGE_INTEGER freq;
  LARGE_INTEGER cnt;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&cnt);
  /* Split into whole seconds and remainder so counts * 10^6 never overflow 64 bits. */
  uint64_t c = (uint64_t)cnt.QuadPart;
  uint64_t f = (uint64_t)freq.QuadPart;
  return (uint32_t)((c / f) * 1000000ULL + ((c % f) * 1000000ULL) / f);
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint32_t)(tv.tv_sec * 1000000UL + tv.tv_usec);
#endif
}
#endif

/*################################### END OF FILE ######################################*/
