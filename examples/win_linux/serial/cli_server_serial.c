/**
 * @file cli_server_serial.c
 * @brief Self-contained host stdin/stdout serial demo runner.
 *
 * Implements a host-side serial CLI demo using the host serial transport with
 * stdin/stdout as the byte source and sink.
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
#include "cli_transport_serial.h"
#include "cli_server_serial.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if ENV_IS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <conio.h>
#elif ENV_IS_UNIX_LIKE
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#endif

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

/** @brief MOTD banner for the host serial demo. */
#define CLI_DEMO_BANNER_SERIAL                                                                     \
  "************************************************************\r\n"                               \
  "*             OpenLibCLI  --  Serial (stdin stdout)        *\r\n"                               \
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

static demo_app_data_struct_t s_app;       /**< Per-session userdata. */
static cli_transport_struct_t s_transport; /**< Serial transport vtable. */

#if ENV_IS_UNIX_LIKE
static struct termios s_orig_term; /**< Saved terminal attributes for restore. */
static int32_t s_raw_mode = 0;     /**< Non-zero while raw mode is active. */
#endif

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

#if CLI_ENABLE_TIMMING_STATS
static uint32_t platform_micros(void *ctx);
#endif
#if ENV_IS_UNIX_LIKE
static void platform_raw_mode(void);
static void platform_restore_term(void);
#endif
static int8_t cli_server_loop(cli_struct_t *cli);

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/
/**
 * @brief Entry point for the serial transport demo executable. Set up the host serial CLI session
 * and run it until it exits.
 *
 * Configures the host terminal transport, registers the demo command
 * set, creates one CLI session, and drives @c cli_session_engine() until the
 * session ends.
 *
 * @return @c 0 on clean exit.
 */
int main(void) {
  cli_serial_init(&s_transport);

  printf("Starting Serial CLI server (stdin/stdout)\n");
  printf("Connect with: (Interactive terminal)\n");
  printf("Type 'exit' to quit.\n\n");

#if CLI_ENABLE_TIMMING_STATS
  cli_platform_struct_t platform = {0};
  platform.micros = platform_micros;
  platform.micros_ctx = NULL;
  cli_init(&demo_cli, "serial", &s_transport, &platform, demo_cmd_pool, CLI_MAX_COMMANDS);
#else
  cli_init(&demo_cli, "serial", &s_transport, NULL, demo_cmd_pool, CLI_MAX_COMMANDS);
#endif
  demo_register_commands(&demo_cli);
  demo_setup_session(&demo_cli, &s_app, 1, 4321, 3800, CLI_DEMO_BANNER_SERIAL);

#if ENV_IS_UNIX_LIKE
  platform_raw_mode();
#endif

  int8_t rc = cli_server_loop(&demo_cli);
  printf("Session ended (rc=%d)\n", rc);

#if ENV_IS_UNIX_LIKE
  platform_restore_term();
#endif

  cli_done(&demo_cli);
  return 0;
}

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

/**
 * @brief Blocking session loop runs @c cli_session_engine() until exit.
 *
 * Waits for stdin data with a 1-second timeout each iteration so idle
 * timeout and periodic callbacks can fire between keystrokes.
 *
 * @param[in] cli  Active CLI session.
 *
 * @return @c CLI_ERR_QUIT on clean exit, @c CLI_ERR on transport error.
 */
static int8_t cli_server_loop(cli_struct_t *cli) {
  cli_start(cli);
  int8_t rc;
  while (1) {
#if ENV_IS_WINDOWS
    DWORD start = GetTickCount();
    while (!_kbhit()) {
      if (GetTickCount() - start >= 1000) {
        break;
      }
      Sleep(1);
    }
#else
    struct timeval tv = {1, 0};
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
#endif
    rc = cli_session_engine(cli);
    if (rc != CLI_OK) {
      cli->session_state = CLI_SESSION_STOP;
      break;
    }
  }
  return rc;
}

/**
 * @brief Apply demo defaults to the host serial CLI session.
 *
 * @param[in] cli  Active CLI session.
 */
#if CLI_ENABLE_TIMMING_STATS
/**
 * @brief Provide a microsecond timestamp for CLI timing statistics.
 *
 * @param[in] ctx  Unused.
 *
 * @return Current microsecond tick count.
 */
static uint32_t platform_micros(void *ctx) {
  (void)ctx;

#if ENV_IS_WINDOWS
  LARGE_INTEGER freq;
  LARGE_INTEGER cnt;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&cnt);
  return (uint32_t)(cnt.QuadPart * 1000000ULL / freq.QuadPart);
#elif ENV_IS_UNIX_LIKE
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint32_t)(tv.tv_sec * 1000000UL + tv.tv_usec);
#else
  return 0U;
#endif
}
#endif

#if ENV_IS_UNIX_LIKE
/**
 * @brief Switch stdin to raw (non-canonical, no-echo) mode.
 */
static void platform_raw_mode(void) {
  if (!s_raw_mode) {
    struct termios term;
    tcgetattr(STDIN_FILENO, &s_orig_term);
    term = s_orig_term;
    term.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ECHOE | ISIG);
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
    s_raw_mode = 1;
  }
}

/**
 * @brief Restore stdin to the original terminal settings.
 */
static void platform_restore_term(void) {
  if (s_raw_mode) {
    tcsetattr(STDIN_FILENO, TCSANOW, &s_orig_term);
  }
  s_raw_mode = 0;
}
#endif

/*################################### END OF FILE ######################################*/
