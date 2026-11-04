/**
 * @file test_cli_engine.c
 * @brief Behavioural tests for the OpenLibCLI engine (cli/cli.c).
 *
 * Self-contained test harness — no external framework required.
 *
 * Each test drives a real session through a loopback transport: bytes are
 * queued as if typed by a user, @c cli_session_engine() is ticked, and the
 * bytes written back are inspected. A controllable clock replaces the host
 * time source so idle-timeout and lockout behaviour are deterministic.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "cli.h"

/*=======================================================================================
 * Test Harness Macros
 *=======================================================================================*/

static int s_tests_run = 0;
static int s_tests_fail = 0;
static int s_asserts = 0;
static const char *s_cur_test = NULL;

#define TEST_START(name)                                                                           \
  do {                                                                                             \
    s_cur_test = name;                                                                             \
    s_tests_run++;                                                                                 \
  } while (0)
#define TEST_END()                                                                                 \
  do {                                                                                             \
    s_cur_test = NULL;                                                                             \
  } while (0)

#define ASSERT(cond)                                                                               \
  do {                                                                                             \
    s_asserts++;                                                                                   \
    if (!(cond)) {                                                                                 \
      fprintf(stderr, "  FAIL [%s] line %d: %s\n", s_cur_test ? s_cur_test : "?", (int)__LINE__,   \
              #cond);                                                                              \
      s_tests_fail++;                                                                              \
    }                                                                                              \
  } while (0)

#define ASSERT_TRUE(cond)  ASSERT(cond)
#define ASSERT_FALSE(cond) ASSERT(!(cond))

#define ASSERT_EQ(a, b)                                                                            \
  do {                                                                                             \
    s_asserts++;                                                                                   \
    if ((a) != (b)) {                                                                              \
      fprintf(stderr, "  FAIL [%s] line %d: expected %lld, got %lld\n",                            \
              s_cur_test ? s_cur_test : "?", (int)__LINE__, (long long)(b), (long long)(a));       \
      s_tests_fail++;                                                                              \
    }                                                                                              \
  } while (0)

#define ASSERT_STR_EQ(a, b)                                                                        \
  do {                                                                                             \
    s_asserts++;                                                                                   \
    if (strcmp((a), (b)) != 0) {                                                                   \
      fprintf(stderr, "  FAIL [%s] line %d: expected \"%s\", got \"%s\"\n",                        \
              s_cur_test ? s_cur_test : "?", (int)__LINE__, (b), (a));                             \
      s_tests_fail++;                                                                              \
    }                                                                                              \
  } while (0)

/** Assert that the captured terminal output contains / lacks @p needle. */
#define ASSERT_OUT(needle)     assert_out((needle), true, (int)__LINE__)
#define ASSERT_NOT_OUT(needle) assert_out((needle), false, (int)__LINE__)

#define RUN_SUITE(suite)                                                                           \
  do {                                                                                             \
    int before = s_tests_fail;                                                                     \
    suite();                                                                                       \
    int failed = s_tests_fail - before;                                                            \
    printf("  %s: %s\n", failed ? "FAIL" : "PASS", #suite);                                        \
  } while (0)

/*=======================================================================================
 * Loopback Transport and Clock
 *=======================================================================================*/

#define TEST_POOL_SIZE 48

/* Checks of optional features are compiled only when the feature is (CLI_ENABLE_*): the
 * suite runs in the default build and with any feature compiled out. */

static char s_in[8192];
static size_t s_in_len;
static size_t s_in_pos;
static char s_out[131072];
static size_t s_out_len;
static uint32_t s_now;

static cli_struct_t s_cli;
static cli_cmd_struct_t s_pool[TEST_POOL_SIZE];

static cli_transport_ret_t t_available(void *ctx) {
  (void)ctx;
  return (s_in_pos < s_in_len) ? 1 : 0;
}

static cli_transport_ret_t t_read(void *ctx) {
  (void)ctx;
  return (cli_transport_ret_t)(unsigned char)s_in[s_in_pos++];
}

static bool s_write_fails;
static cli_transport_buflen_t s_write_chunk; /* 0 = accept everything; else max per call */

static cli_transport_ret_t t_write(void *ctx, const uint8_t *buf, cli_transport_buflen_t len) {
  (void)ctx;
  if (s_write_fails) {
    return -1;
  }
  if (s_write_chunk > 0 && len > s_write_chunk) {
    len = s_write_chunk; /* short write: the engine must send the rest itself */
  }
  if (s_out_len + (size_t)len < sizeof(s_out)) {
    memcpy(&s_out[s_out_len], buf, (size_t)len);
    s_out_len += (size_t)len;
    s_out[s_out_len] = '\0';
  }
  return (cli_transport_ret_t)len;
}

static uint32_t t_now(void *ctx) {
  (void)ctx;
  return s_now;
}

static void out_clear(void) {
  s_out_len = 0;
  s_out[0] = '\0';
}

static int out_count(const char *needle) {
  int n = 0;
  for (const char *p = strstr(s_out, needle); p != NULL; p = strstr(p + 1, needle)) {
    n++;
  }
  return n;
}

static void assert_out(const char *needle, bool want, int line) {
  bool has = (strstr(s_out, needle) != NULL);
  s_asserts++;
  if (has != want) {
    fprintf(stderr, "  FAIL [%s] line %d: output %s \"%s\"\n    output was: \"", s_cur_test, line,
            want ? "lacks" : "unexpectedly has", needle);
    for (size_t i = 0; i < s_out_len; i++) {
      unsigned char c = (unsigned char)s_out[i];
      if (c == '\r') {
        fputs("\\r", stderr);
      } else if (c == '\n') {
        fputs("\\n", stderr);
      } else if (c < 0x20 || c == 0x7F) {
        fprintf(stderr, "<%02X>", c);
      } else {
        fputc(c, stderr);
      }
    }
    fputs("\"\n", stderr);
    s_tests_fail++;
  }
}

/** Tick the engine until it has consumed all queued input (or stops). */
static int8_t run(void) {
  int8_t rc = CLI_OK;
  for (int i = 0; i < 16 && rc == CLI_OK; i++) {
    rc = cli_session_engine(&s_cli);
  }
  return rc;
}

/** Queue @p s as typed input and process it. */
static int8_t type(const char *s) {
  size_t n = strlen(s);
  memcpy(&s_in[s_in_len], s, n);
  s_in_len += n;
  return run();
}

/*=======================================================================================
 * Test Commands
 *=======================================================================================*/

static int s_cb_count;

static int8_t cmd_echo(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  cli_print(cli, "ARGS");
  for (cli_argc_t i = 1; i < argc; i++) {
    cli_print(cli, " [%s]", argv[i]);
  }
  cli_println(cli, "");
  return CLI_OK;
}

static int8_t cmd_ask(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_print(cli, "Are you sure? ");
  return CLI_OK;
}

static int8_t cmd_name(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  s_cb_count++;
  cli_println(cli, "RAN:%s", cmd);
  return CLI_OK;
}

static int8_t cmd_calc(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  return cli_set_mode(cli, CLI_MODE_USER_BASE);
}

static int8_t cmd_arith(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  return cli_set_mode(cli, (cli_mode_t)(CLI_MODE_USER_BASE + 1));
}

static int8_t cmd_nest(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  int8_t inner = cli_exec(cli, "echo inner");
  cli_println(cli, "OUTER:%s rc=%d", (argc > 1) ? argv[1] : "-", (int)inner);
  return CLI_OK;
}

static int8_t cmd_loop(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  return cli_exec(cli, "loop"); /* runs itself: must be stopped by the nesting limit */
}

static int8_t cmd_nestpipe(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                           const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  return cli_exec(cli, "echo x | include x");
}

static int8_t cmd_longline(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                           const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  /* 1500 characters without a newline, each cli_print() within the format buffer. */
  static const char x100[] =
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      "xxxxxxxxxxxxxxxxxxxx";
  int piece = (CLI_MAX_OUTPUT_BUF - 1 < 100) ? CLI_MAX_OUTPUT_BUF - 1 : 100;
  for (int left = 1500; left > 0; left -= piece) {
    cli_print(cli, "%.*s", (left < piece) ? left : piece, x100);
  }
  cli_println(cli, "");
  return CLI_OK;
}

static int8_t cmd_warn(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_print(cli, "partial");
  (void)cli_error(cli, "bad thing");
  return CLI_OK;
}

static int8_t cmd_relogin(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                          const char *argv[]) {
  (void)cmd;
  (void)argc;
  cli_request_auth(cli); /* empties the line editor while this line is still executing */
  cli_println(cli, "ARG0:%s", argv[0]);
  return CLI_OK;
}

static cli_cmd_handle_t s_h_show;
static cli_cmd_handle_t s_h_calc;
static cli_cmd_handle_t s_h_secret;

static void register_test_commands(void) {
  cli_cmd_handle_t h;

  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "echo", cmd_echo, CLI_PRIV_USER, CLI_MODE_ANY,
                        "Echo arguments");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "ask", cmd_ask, CLI_PRIV_USER, CLI_MODE_ANY,
                        "Partial line output");
  s_h_show =
      cli_add_command(&s_cli, CLI_CMD_ROOT, "show", NULL, CLI_PRIV_USER, CLI_MODE_ANY, "Show");
  (void)cli_add_command(&s_cli, s_h_show, "version", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY,
                        "Version");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "showall", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY,
                        "Show everything");
  s_h_secret = cli_add_command(&s_cli, CLI_CMD_ROOT, "secret", cmd_name, CLI_PRIV_USER,
                               CLI_MODE_ANY, "Hidden");
  cli_hide_command(&s_cli, s_h_secret);

  s_h_calc = cli_add_command(&s_cli, CLI_CMD_ROOT, "calc", cmd_calc, CLI_PRIV_USER, CLI_MODE_EXEC,
                             "Calculator menu");
  h = cli_add_command(&s_cli, s_h_calc, "arith", cmd_arith, CLI_PRIV_USER, CLI_MODE_ANY,
                      "Arithmetic menu");
  (void)cli_add_command(&s_cli, h, "add", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "Add");

  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "alpha", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "A");
#if CLI_ENABLE_ALIASES
  (void)cli_cmd_add_alias(&s_cli, h, "zeta1");
#endif
  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "alpine", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "B");
#if CLI_ENABLE_ALIASES
  (void)cli_cmd_add_alias(&s_cli, h, "zeta2");
#endif

  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "nest", cmd_nest, CLI_PRIV_USER, CLI_MODE_ANY, "");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "loop", cmd_loop, CLI_PRIV_USER, CLI_MODE_ANY, "");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "nestpipe", cmd_nestpipe, CLI_PRIV_USER,
                        CLI_MODE_ANY, "");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "longline", cmd_longline, CLI_PRIV_USER,
                        CLI_MODE_ANY, "");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "warn", cmd_warn, CLI_PRIV_USER, CLI_MODE_ANY, "");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "relogin", cmd_relogin, CLI_PRIV_USER, CLI_MODE_ANY,
                        "");
}

/** Create a fresh session (same pool every time, like the example servers). */
static void session_new(void) {
  cli_transport_struct_t tp;
  cli_platform_struct_t pf;

  memset(&tp, 0, sizeof(tp));
  memset(&pf, 0, sizeof(pf));
  tp.available = t_available;
  tp.read = t_read;
  tp.write = t_write;
  pf.now_sec = t_now;

  s_in_len = 0;
  s_in_pos = 0;
  s_now = 1000U;
  s_cb_count = 0;
  s_write_fails = false;
  s_write_chunk = 0;
  out_clear();

  cli_init(&s_cli, "rtr", &tp, &pf, s_pool, TEST_POOL_SIZE);
  cli_add_builtin_cmds(&s_cli);
  register_test_commands();
  cli_set_idle_timeout(&s_cli, 0U);
}

static void session_start(void) {
  cli_start(&s_cli);
  (void)run();
  out_clear();
}

/*=======================================================================================
 * Command Dispatch
 *=======================================================================================*/

static void test_help_shows_text(void) {
  TEST_START("help_shows_text");
  session_new();
  session_start();

  type("help\r");
#if CLI_ENABLE_COMMAND_HELP
  ASSERT_OUT("  exit                Exit session");
  ASSERT_OUT("Show commands");
  ASSERT_NOT_OUT("secret"); /* hidden */
#else
  ASSERT_OUT("  exit\r\n");
  ASSERT_NOT_OUT("secret"); /* hidden */
#endif
  TEST_END();
}

static void test_subcommand_runs_once(void) {
  TEST_START("subcommand_runs_once");
  session_new();
  session_start();

  type("usage commands\r");
  ASSERT_OUT("Common usage:");
  ASSERT_NOT_OUT("Usage information.");

  out_clear();
  type("usage bogus\r");
  ASSERT_OUT("% Unknown command: bogus");
  ASSERT_NOT_OUT("Usage information.");
  ASSERT_EQ(cli_exec(&s_cli, "usage bogus"), CLI_ERR);
  TEST_END();
}

static void test_exact_match_ignores_case(void) {
  TEST_START("exact_match_ignores_case");
  session_new();
  session_start();

  /* "show" and "showall" share a prefix: an exact name must win in any case. */
  type("SHOW version\r");
  ASSERT_OUT("RAN:version");
  ASSERT_NOT_OUT("Ambiguous");
  TEST_END();
}

static void test_hidden_command_exact_only(void) {
  TEST_START("hidden_command_exact_only");
  session_new();
  session_start();

  type("secret\r");
  ASSERT_OUT("RAN:secret");
  out_clear();
  type("secr\r");
  ASSERT_OUT("% Unknown command: secr");
  TEST_END();
}

static void test_print_partial_line(void) {
  TEST_START("print_partial_line");
  session_new();
  session_start();

  type("ask\r");
  ASSERT_OUT("Are you sure? ");

#if CLI_ENABLE_PIPE_FILTER
  /* Also with a per-line consumer installed (line-buffered path). */
  out_clear();
  type("ask | include sure\r");
  ASSERT_OUT("Are you sure?");
#endif
  TEST_END();
}

/*=======================================================================================
 * Navigation
 *=======================================================================================*/

static void test_exit_unwinds_modes(void) {
  TEST_START("exit_unwinds_modes");
  session_new();
  session_start();

  type("enable\r");
  type("configure\r");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_CONFIG);
  out_clear();
  type("exit\r");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_ENABLE);
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_PRIVILEGED);
  ASSERT_OUT("rtr# ");
  out_clear();
  type("exit\r");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_EXEC);
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
  ASSERT_OUT("rtr> ");
  ASSERT_EQ(type("exit\r"), CLI_ERR_QUIT);
  ASSERT_EQ(cli_get_session_state(&s_cli), CLI_SESSION_STOP);
  TEST_END();
}

static void test_end_and_disable(void) {
  TEST_START("end_and_disable");
  session_new();
  session_start();

  type("enable\r");
  type("configure\r");
  type("end\r");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_ENABLE);
  type("exit\r");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_EXEC);

  type("enable\r");
  type("configure\r");
  type("end\r");
  type("disable\r");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_EXEC);
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
  ASSERT_EQ(type("exit\r"), CLI_ERR_QUIT);
  TEST_END();
}

static void test_user_mode_submenus(void) {
  TEST_START("user_mode_submenus");
  session_new();
  session_start();

  type("calc\r");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_USER_BASE);
  ASSERT_OUT("calc> ");
  type("arith\r");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_USER_BASE + 1);
  out_clear();
  type("add\r");
  ASSERT_OUT("RAN:add");
  out_clear();
  type("exit\r");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_USER_BASE);
  ASSERT_OUT("calc> ");
  out_clear();
  type("exit\r");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_EXEC);
  ASSERT_OUT("rtr> ");
  ASSERT_EQ(type("exit\r"), CLI_ERR_QUIT);
  TEST_END();
}

static void test_navigation_depth_limit(void) {
  TEST_START("navigation_depth_limit");
  session_new();

  for (int i = 0; i < CLI_MAX_CONTEXT_DEPTH; i++) {
    ASSERT_EQ(cli_set_mode(&s_cli, (cli_mode_t)(100 + i)), CLI_OK);
  }
  ASSERT_EQ(cli_set_mode(&s_cli, 999), CLI_ERR_NOMEM);
  ASSERT_EQ(cli_get_mode(&s_cli), 100 + CLI_MAX_CONTEXT_DEPTH - 1);
  /* Returning to an enclosing mode pops back to it. */
  ASSERT_EQ(cli_set_mode(&s_cli, 101), CLI_OK);
  ASSERT_EQ(s_cli.nav_depth, 2);
  TEST_END();
}

#if CLI_ENABLE_MODE_NAMES
static void test_mode_name_for_user_mode(void) {
  TEST_START("mode_name_for_user_mode");
  session_new();
  session_start();

  ASSERT_EQ(cli_set_mode_name(&s_cli, CLI_MODE_USER_BASE + 5, "lab"), CLI_OK);
  ASSERT_EQ(cli_set_mode(&s_cli, CLI_MODE_USER_BASE + 5), CLI_OK);
  type("\r");
  ASSERT_OUT("rtr(lab)> ");
  ASSERT_EQ(cli_set_mode_name(&s_cli, CLI_MODE_USER_BASE + 5, ""), CLI_OK);
  out_clear();
  type("\r");
  ASSERT_OUT("rtr> ");
  TEST_END();
}
#endif

static void test_ctrl_d_follows_quit_policy(void) {
  TEST_START("ctrl_d_follows_quit_policy");
  session_new();
  cli_set_quit_policy(&s_cli, CLI_QUIT_POLICY_IGNORE);
  session_start();

  ASSERT_EQ(type("\x04"), CLI_OK);
  ASSERT_EQ(cli_get_session_state(&s_cli), CLI_SESSION_RUN);
  ASSERT_OUT("rtr> ");

  cli_set_quit_policy(&s_cli, CLI_QUIT_POLICY_CLOSE_SESSION);
  ASSERT_EQ(type("\x04"), CLI_ERR_QUIT);
  TEST_END();
}

/*=======================================================================================
 * Command Pool
 *=======================================================================================*/

static void test_negative_handles_rejected(void) {
  TEST_START("negative_handles_rejected");
  session_new();
  cli_poolsize_t count = s_cli.cmd_pool_count;

  ASSERT_EQ(cli_remove_command(&s_cli, CLI_CMD_ROOT), CLI_ERR);
  ASSERT_EQ(cli_remove_command_recursive(&s_cli, CLI_CMD_ROOT), CLI_ERR);
  ASSERT_EQ(cli_remove_command(&s_cli, -7), CLI_ERR);
  cli_hide_command(&s_cli, CLI_CMD_ROOT);
  cli_hide_command(&s_cli, TEST_POOL_SIZE + 3);
#if CLI_ENABLE_ALIASES
  ASSERT_EQ(cli_cmd_add_alias(&s_cli, CLI_CMD_ROOT, "x"), CLI_ERR);
  ASSERT_EQ(cli_add_command_duplicate(&s_cli, CLI_CMD_ROOT, "x"), CLI_CMD_INVALID);
#endif
  ASSERT_EQ(s_cli.cmd_pool_count, count);
  TEST_END();
}

static void test_add_command_validation(void) {
  TEST_START("add_command_validation");
  session_new();
  cli_poolsize_t count = s_cli.cmd_pool_count;
  char long_name[CLI_MAX_NAME_LEN + 1];

  memset(long_name, 'n', sizeof(long_name) - 1U);
  long_name[sizeof(long_name) - 1U] = '\0';

  ASSERT_EQ(cli_add_command(&s_cli, CLI_CMD_ROOT, "", cmd_name, 0, 0, "x"), CLI_CMD_INVALID);
  ASSERT_EQ(cli_add_command(&s_cli, CLI_CMD_ROOT, "a b", cmd_name, 0, 0, "x"), CLI_CMD_INVALID);
  ASSERT_EQ(cli_add_command(&s_cli, CLI_CMD_ROOT, "a?", cmd_name, 0, 0, "x"), CLI_CMD_INVALID);
  ASSERT_EQ(cli_add_command(&s_cli, CLI_CMD_ROOT, "a|b", cmd_name, 0, 0, "x"), CLI_CMD_INVALID);
  ASSERT_EQ(cli_add_command(&s_cli, CLI_CMD_ROOT, long_name, cmd_name, 0, 0, "x"),
            CLI_CMD_INVALID);
  ASSERT_EQ(cli_add_command(&s_cli, CLI_CMD_INVALID, "orphan", cmd_name, 0, 0, "x"),
            CLI_CMD_INVALID);
  ASSERT_EQ(cli_add_command(&s_cli, TEST_POOL_SIZE - 1, "orphan", cmd_name, 0, 0, "x"),
            CLI_CMD_INVALID);
#if CLI_ENABLE_ALIASES
  ASSERT_EQ(cli_cmd_add_alias(&s_cli, s_h_show, "bad alias"), CLI_ERR);
#endif
  ASSERT_EQ(s_cli.cmd_pool_count, count);
  TEST_END();
}

static void test_reinit_clears_pool(void) {
  TEST_START("reinit_clears_pool");
  session_new();
  cli_poolsize_t count = s_cli.cmd_pool_count;

  session_new(); /* same pool, like a server accepting the next client */
  ASSERT_EQ(s_cli.cmd_pool_count, count);
  session_start();
  type("help\r");
  ASSERT_EQ(out_count("  exit"), 1);
  TEST_END();
}

static void test_remove_detaches_children(void) {
  TEST_START("remove_detaches_children");
  session_new();
  session_start();

  cli_cmd_handle_t net =
      cli_add_command(&s_cli, CLI_CMD_ROOT, "net", NULL, CLI_PRIV_USER, CLI_MODE_ANY, "Net");
  (void)cli_add_command(&s_cli, net, "ping", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "Ping");
  ASSERT_EQ(cli_remove_command(&s_cli, net), CLI_OK);

  /* The freed slot is reused; the old child must not reappear under the new node. */
  cli_cmd_handle_t foo =
      cli_add_command(&s_cli, CLI_CMD_ROOT, "foo", cmd_echo, CLI_PRIV_USER, CLI_MODE_ANY, "Foo");
  ASSERT_EQ(foo, net);
  type("foo ping\r");
  ASSERT_NOT_OUT("RAN:ping");
  ASSERT_OUT("ARGS [ping]");

  /* Removing the submenu the user is in moves them back out. */
  type("calc\r");
  ASSERT_EQ(s_cli.context, s_h_calc);
  ASSERT_EQ(cli_remove_command_recursive(&s_cli, s_h_calc), CLI_OK);
  ASSERT_EQ(s_cli.context, CLI_CMD_ROOT);
  out_clear();
  type("\r");
  ASSERT_OUT("rtr> ");
  TEST_END();
}

/*=======================================================================================
 * Line Editor
 *=======================================================================================*/

static void test_csi_with_parameters(void) {
  TEST_START("csi_with_parameters");
  session_new();
  session_start();

  /* Ctrl+Right (ESC[1;5C) and a private-mode sequence must not insert text. */
  type("hel\x1b[1;5Cp\x1b[?25h\r");
  ASSERT_OUT("  exit");
  ASSERT_NOT_OUT("5C");
  ASSERT_NOT_OUT("25h");

  /* Ctrl+Left moves the cursor like Left. */
  out_clear();
  type("hep\x1b[1;5Dl\r");
  ASSERT_OUT("  exit");
  TEST_END();
}

#if CLI_ENABLE_TAB_COMPLETION
static void test_tab_completes_at_cursor(void) {
  TEST_START("tab_completes_at_cursor");
  session_new();
  session_start();

  type("show ve extra");
  type("\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D"); /* cursor right after "ve" */
  type("\t");
  ASSERT_STR_EQ(s_cli.input, "show version  extra");
  out_clear();
  type("\r");
  ASSERT_OUT("RAN:version");
  TEST_END();
}
#endif

#if CLI_ENABLE_TAB_COMPLETION && CLI_ENABLE_ALIASES
static void test_tab_common_prefix_of_aliases(void) {
  TEST_START("tab_common_prefix_of_aliases");
  session_new();
  session_start();

  type("ze\t");
  ASSERT_STR_EQ(s_cli.input, "zeta");
  type("1\t");
  ASSERT_STR_EQ(s_cli.input, "zeta1 ");
  TEST_END();
}
#endif

static void test_question_mark(void) {
  TEST_START("question_mark");
  session_new();
  session_start();

  type("show ?");
  ASSERT_OUT("version");
  ASSERT_STR_EQ(s_cli.input, "show ");

  /* Inside quotes '?' is ordinary text. */
  type("\x15"); /* Ctrl+U */
  out_clear();
  type("echo \"a?b\"\r");
  ASSERT_OUT("ARGS [a?b]");
  TEST_END();
}

/*=======================================================================================
 * Tokenizer, Execution API, Filters
 *=======================================================================================*/

static void test_quoted_arguments(void) {
  TEST_START("quoted_arguments");
  session_new();
  session_start();

  type("echo \"a b\" c \"x\\\"y\" \"\"\r");
  ASSERT_OUT("ARGS [a b] [c] [x\"y] []");

  out_clear();
  type("echo \"open\r");
  ASSERT_OUT("% Unterminated quoted string.");
  ASSERT_NOT_OUT("ARGS");

  char many[256] = "echo";
  for (int i = 0; i < CLI_MAX_ARGS + 2; i++) {
    strcat(many, " a");
  }
  strcat(many, "\r");
  out_clear();
  type(many);
  ASSERT_OUT("% Too many arguments.");
  ASSERT_NOT_OUT("ARGS");
  TEST_END();
}

static void test_exec_api(void) {
  TEST_START("exec_api");
  session_new();

  char too_long[CLI_MAX_INPUT_LEN + 8];
  memset(too_long, 'x', sizeof(too_long) - 1U);
  memcpy(too_long, "echo ", 5);
  too_long[sizeof(too_long) - 1U] = '\0';
  ASSERT_EQ(cli_exec(&s_cli, too_long), CLI_ERR_NOMEM);
  ASSERT_NOT_OUT("ARGS");

  const char *const argv[] = {"echo", "a b", "x|y", "", "q\"uote", "back\\slash"};
  ASSERT_EQ(cli_exec_argv(&s_cli, argv, 6), CLI_OK);
  ASSERT_OUT("ARGS [a b] [x|y] [] [q\"uote] [back\\slash]");
  TEST_END();
}

#if CLI_ENABLE_PRINT_TRANSPORT_CB
static int s_transport_lines;
static int s_transport_chars;
static char s_last_line[128];

static void capture_line(cli_struct_t *cli, const char *line) {
  (void)cli;
  s_transport_lines++;
  s_transport_chars += (int)strlen(line);
  snprintf(s_last_line, sizeof(s_last_line), "%s", line);
}
#endif

#if CLI_ENABLE_PIPE_FILTER && CLI_ENABLE_COMMAND_HELP
static void test_pipe_filters(void) {
  TEST_START("pipe_filters");
  session_new();
  session_start();

  type("help | include Exit\r");
  ASSERT_OUT("Exit session");
  ASSERT_NOT_OUT("Show commands");

  out_clear();
  type("help | inc \"Quit session\"\r");
  ASSERT_OUT("Quit session");
  ASSERT_NOT_OUT("Exit session");

  out_clear();
  type("usage keys | exclude Ctrl\r");
  ASSERT_OUT("Tab");
  ASSERT_NOT_OUT("Ctrl-W");

  out_clear();
  type("help | bogus x\r");
  ASSERT_OUT("% Invalid filter: bogus");
  ASSERT_NOT_OUT("Exit session");

  out_clear();
  type("help | include\r");
  ASSERT_OUT("% Missing filter pattern.");

  out_clear();
  type("echo \"a|b\"\r");
  ASSERT_OUT("ARGS [a|b]");

#if CLI_ENABLE_PRINT_TRANSPORT_CB
  /* The count total reaches a per-line consumer too. */
  s_transport_lines = 0;
  cli_set_print_transport_cb(&s_cli, capture_line);
  type("usage keys | count\r");
  ASSERT_STR_EQ(s_last_line, "Count: 8");
  cli_set_print_transport_cb(&s_cli, NULL);
#endif
  TEST_END();
}
#endif

static int s_v_kind;

static int capture_print_v(cli_struct_t *cli, cli_print_kind_enum_t kind, const char *fmt,
                           va_list ap) {
  char buf[64];
  (void)cli;
  s_v_kind = (int)kind;
  return vsnprintf(buf, sizeof(buf), fmt, ap);
}

static void test_print_cb_v_kind(void) {
  TEST_START("print_cb_v_kind");
  session_new();
  cli_set_print_cb_v(&s_cli, capture_print_v);

  ASSERT_EQ(cli_println(&s_cli, "x%d", 12), 3);
  ASSERT_EQ(s_v_kind, CLI_PRINT_KIND_PRINTLN);
  (void)cli_print(&s_cli, "y");
  ASSERT_EQ(s_v_kind, CLI_PRINT_KIND_PRINT);
  (void)cli_error(&s_cli, "z");
  ASSERT_EQ(s_v_kind, CLI_PRINT_KIND_ERROR);
  TEST_END();
}

/*=======================================================================================
 * History
 *=======================================================================================*/

#if CLI_ENABLE_HISTORY
static void test_history_ring_evicts_oldest(void) {
  TEST_START("history_ring_evicts_oldest");
  session_new();
  session_start();

  char line[32];
  for (int i = 0; i < 400; i++) {
    snprintf(line, sizeof(line), "c%d\r", i);
    type(line);
  }
  ASSERT_EQ(s_cli.history_count, CLI_MAX_HISTORY);

  /* Long entries force wrap-around: the newest commands must survive intact. */
  char long_line[400];
  for (int i = 0; i < 20; i++) {
    memset(long_line, 'x', 300);
    snprintf(&long_line[300], sizeof(long_line) - 300U, "%02d\r", i);
    type(long_line);
  }
  ASSERT_TRUE(s_cli.history_count >= CLI_HISTORY_BUF_SIZE / 304 - 1);

  out_clear();
  type("history\r");
  ASSERT_OUT("x19");
  ASSERT_OUT("x18");
  ASSERT_OUT("history");
  TEST_END();
}
#endif

#if CLI_ENABLE_HISTORY
static void test_history_cleared_on_restart(void) {
  TEST_START("history_cleared_on_restart");
  session_new();
  session_start();

  type("show version\r");
  cli_restart_session(&s_cli);
  (void)run();
  out_clear();
  type("\x1b[A");
  ASSERT_NOT_OUT("show version");
  ASSERT_EQ(s_cli.history_count, 0);
  TEST_END();
}
#endif

/*=======================================================================================
 * Authentication
 *=======================================================================================*/

#if CLI_ENABLE_AUTH
static void test_enable_prompt_waits_for_password(void) {
  TEST_START("enable_prompt_waits_for_password");
  session_new();
  cli_set_enable_secret(&s_cli, "s3cret");
  session_start();

  type("enable\r");
  ASSERT_OUT("Password: ");
  ASSERT_NOT_OUT("rtr> ");
  out_clear();
  type("s3cret\r");
  ASSERT_OUT("rtr# ");
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_PRIVILEGED);
  type("exit\r");
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
  TEST_END();
}
#endif

#if CLI_ENABLE_AUTH
static void test_password_keys_not_echoed(void) {
  TEST_START("password_keys_not_echoed");
  session_new();
  cli_set_enable_secret(&s_cli, "s3cret");
  session_start();

  type("show version\r");
  type("enable\r");
  out_clear();
  /* Home, Right x2, Delete, Up: none of these may reveal the secret or history. */
  type("hunter\x1b[H\x1b[C\x1b[C\x1b[3~\x1b[A\x1bOA");
  ASSERT_EQ((long long)s_out_len, 0);
  ASSERT_STR_EQ(s_cli.input, "hunter");
  TEST_END();
}
#endif

#if CLI_ENABLE_AUTH
static void test_login_prompt_hides_history(void) {
  TEST_START("login_prompt_hides_history");
  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_PRIVILEGED);
  cli_require_authorization(&s_cli, true);
  session_start();

  type("admin\rpw\r");
  type("show version\r");
  cli_request_auth(&s_cli);
  out_clear();
  type("\x1b[A");
  ASSERT_NOT_OUT("show version");
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
  TEST_END();
}
#endif

#if CLI_ENABLE_AUTH
static void test_enable_attempt_limit(void) {
  TEST_START("enable_attempt_limit");
  session_new();
  cli_set_enable_secret(&s_cli, "s3cret");
  session_start();

  type("enable\rx\r");
  type("enable\rx\r");
  ASSERT_EQ(type("enable\rx\r"), CLI_ERR_AUTH);
  ASSERT_OUT("Closing Session");

#if CLI_ENABLE_TIME_SOURCE
  /* Lockout: input during the lockout is discarded, the shell returns afterwards. */
  session_new();
  cli_set_enable_secret(&s_cli, "s3cret");
  cli_set_auth_failure_mode(&s_cli, CLI_AUTH_FAILURE_MODE_LOCKOUT);
  cli_set_auth_lockout_duration(&s_cli, 30U);
  session_start();
  /* "show version" arrives in the same burst that starts the lockout: discarded too. */
  type("enable\rx\renable\rx\renable\rx\rshow version\r");
  ASSERT_OUT("Lock-Out Session");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_LOCKOUT);
  ASSERT_EQ(s_in_pos, s_in_len);
  type("show version\r");
  s_now += 31U;
  out_clear();
  (void)run();
  ASSERT_OUT("rtr> ");
  ASSERT_NOT_OUT("RAN:version");
  type("show version\r");
  ASSERT_OUT("RAN:version");
#endif

  /* Lockout requested but no clock: the session is closed instead. */
  session_new();
  cli_set_enable_secret(&s_cli, "s3cret");
  cli_set_auth_failure_mode(&s_cli, CLI_AUTH_FAILURE_MODE_LOCKOUT);
  cli_set_time_source(&s_cli, NULL, NULL);
  session_start();
  type("enable\rx\renable\rx\r");
  ASSERT_EQ(type("enable\rx\r"), CLI_ERR_AUTH);
  TEST_END();
}
#endif

#if CLI_ENABLE_AUTH
static cli_struct_t *s_cb_cli;

static int8_t auth_cb(cli_struct_t *cli, const char *user, const char *pass) {
  int8_t rc = CLI_ERR_AUTH;
  s_cb_cli = cli;
  if (strcmp(pass, "pw") == 0) {
    if (strcmp(user, "root") == 0) {
      cli_set_privilege(cli, CLI_PRIV_PRIVILEGED);
    }
    rc = CLI_OK;
  } else {
    cli_set_privilege(cli, CLI_PRIV_SUPERADMIN); /* must not survive a rejection */
  }
  return rc;
}
#endif

#if CLI_ENABLE_AUTH
static void test_auth_callback_sets_privilege(void) {
  TEST_START("auth_callback_sets_privilege");
  session_new();
  cli_set_authorization_cb(&s_cli, auth_cb);
  cli_require_authorization(&s_cli, true);
  session_start();

  type("root\rbad\r");
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
  type("root\rpw\r");
  ASSERT_TRUE(s_cb_cli == &s_cli);
  ASSERT_OUT("rtr# ");
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_ENABLE);
  ASSERT_EQ(type("exit\r"), CLI_ERR_QUIT); /* privileged exec is the login floor */

  session_new();
  cli_set_authorization_cb(&s_cli, auth_cb);
  cli_require_authorization(&s_cli, true);
  session_start();
  type("bob\rpw\r");
  ASSERT_OUT("rtr> ");
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
  TEST_END();
}
#endif

/*=======================================================================================
 * Idle Timeout
 *=======================================================================================*/

#if CLI_ENABLE_IDLE_TIMEOUT
static int s_idle_calls;

static int8_t idle_cb(cli_struct_t *cli) {
  (void)cli;
  s_idle_calls++;
  return CLI_OK;
}
#endif

#if CLI_ENABLE_IDLE_TIMEOUT
static void test_idle_callback_always_fires(void) {
  TEST_START("idle_callback_always_fires");
  session_new();
  session_start();
  s_idle_calls = 0;

  /* Default policy is RESET_SESSION and no login is required. */
  cli_set_idle_timeout_cb(&s_cli, 5U, idle_cb);
  s_now += 6U;
  (void)run();
  ASSERT_EQ(s_idle_calls, 1);
  (void)run();
  ASSERT_EQ(s_idle_calls, 1); /* re-armed, not re-fired */
  s_now += 6U;
  (void)run();
  ASSERT_EQ(s_idle_calls, 2);
  TEST_END();
}
#endif

#if CLI_ENABLE_IDLE_TIMEOUT
static void test_idle_reset_fires_once(void) {
  TEST_START("idle_reset_fires_once");
  session_new();
  session_start();

  type("enable\r");
  cli_set_idle_timeout(&s_cli, 5U);
  s_now += 6U;
  (void)run();
  ASSERT_OUT("Idle timeout. Session restarted.");
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
  s_now += 100U;
  (void)run();
  ASSERT_EQ(out_count("Idle timeout"), 1);

  type("\r"); /* activity re-arms */
  s_now += 6U;
  (void)run();
  ASSERT_EQ(out_count("Idle timeout"), 2);
  TEST_END();
}
#endif

/*=======================================================================================
 * NULL Safety
 *=======================================================================================*/

/*=======================================================================================
 * Round 2: nesting, line endings, output, registration
 *=======================================================================================*/

static void test_nested_exec(void) {
  TEST_START("nested_exec");
  session_new();
  session_start();

  /* The handler's own argv survives a nested cli_exec() and the inner output appears. */
  type("nest outerarg\r");
  ASSERT_OUT("ARGS [inner]");
  ASSERT_OUT("OUTER:outerarg rc=0");

#if CLI_ENABLE_PIPE_FILTER
  /* The outer pipe filter applies to nested output. */
  out_clear();
  type("nest outerarg | include OUTER\r");
  ASSERT_OUT("OUTER:outerarg");
  ASSERT_NOT_OUT("ARGS [inner]");

  /* A nested line may not carry its own filter. */
  out_clear();
  type("nestpipe\r");
  ASSERT_OUT("% Pipe filters cannot be used in a nested command.");
#endif

  /* Self-recursion is stopped by the nesting limit instead of overflowing the stack. */
  out_clear();
  type("loop\r");
  ASSERT_OUT("% Command nesting too deep.");
  ASSERT_EQ(cli_get_session_state(&s_cli), CLI_SESSION_RUN);
  TEST_END();
}

static void test_exec_preserves_editor(void) {
  TEST_START("exec_preserves_editor");
  session_new();
#if CLI_ENABLE_AUTH
  cli_set_enable_secret(&s_cli, "hunter");
  session_start();
#else
  session_start();
#endif

  /* A line being typed survives a programmatic command. */
  type("show ve");
  ASSERT_EQ(cli_exec(&s_cli, "echo hi"), CLI_OK);
  ASSERT_OUT("ARGS [hi]");
  ASSERT_STR_EQ(s_cli.input, "show ve");
  type("\x15"); /* Ctrl+U */

#if CLI_ENABLE_AUTH && CLI_ENABLE_HISTORY
  /* So does a half-typed password. */
  type("enable\r");
  type("hun");
  ASSERT_EQ(cli_exec(&s_cli, "echo x"), CLI_OK);
  type("ter\r");
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_PRIVILEGED);

  /* Programmatic commands are not added to the user's history. */
  ASSERT_EQ(s_cli.history_count, 1); /* only the typed "enable" */
#endif
  TEST_END();
}

static void test_line_ending_pairs(void) {
  TEST_START("line_ending_pairs");
  session_new();
  session_start();

  type("echo a\n\recho b\r\necho c\r\r");
  ASSERT_OUT("ARGS [a]");
  ASSERT_OUT("ARGS [b]");
  ASSERT_OUT("ARGS [c]");
  /* LF CR and CR LF are one line ending each; CR CR is two (one empty line). */
  ASSERT_EQ(out_count("rtr> "), 4);
  TEST_END();
}

static void test_output_line_handling(void) {
  TEST_START("output_line_handling");
  session_new();
  session_start();

  /* A partial last line is terminated before the prompt in every output mode. */
  type("ask\r");
  ASSERT_OUT("Are you sure? \r\nrtr> ");

  /* An error is shown after the output produced before it. */
  out_clear();
  type("warn\r");
  ASSERT_OUT("partial\r\n% bad thing\r\n");

#if CLI_ENABLE_PRINT_TRANSPORT_CB
  /* Lines longer than the output buffer are delivered in pieces, nothing is lost. */
  s_transport_lines = 0;
  s_transport_chars = 0;
  cli_set_print_transport_cb(&s_cli, capture_line);
  type("longline\r");
  cli_set_print_transport_cb(&s_cli, NULL);
  ASSERT_EQ(s_transport_chars, 1500);
  ASSERT_EQ(s_transport_lines, (1500 + CLI_MAX_OUTPUT_BUF - 2) / (CLI_MAX_OUTPUT_BUF - 1));
#endif
  TEST_END();
}

static void test_short_writes(void) {
  TEST_START("short_writes");
  session_new();
  session_start();

  s_write_chunk = 3;
  type("help\r");
#if CLI_ENABLE_COMMAND_HELP
  ASSERT_OUT("  exit                Exit session");
  ASSERT_EQ(cli_get_session_state(&s_cli), CLI_SESSION_RUN);
#else
  ASSERT_OUT("  exit\r\n");
  ASSERT_EQ(cli_get_session_state(&s_cli), CLI_SESSION_RUN);
#endif
  TEST_END();
}

static void test_registration_rules(void) {
  TEST_START("registration_rules");
  session_new();

  /* Same name (any case) in overlapping modes is rejected; disjoint modes are fine. */
  ASSERT_EQ(cli_add_command(&s_cli, CLI_CMD_ROOT, "ECHO", cmd_echo, 0, CLI_MODE_ANY, ""),
            CLI_CMD_INVALID);
  ASSERT_TRUE(cli_add_command(&s_cli, CLI_CMD_ROOT, "cfg", cmd_name, 0, CLI_MODE_CONFIG, "") >= 0);
  ASSERT_TRUE(cli_add_command(&s_cli, CLI_CMD_ROOT, "cfg", cmd_name, 0, CLI_MODE_EXEC, "") >= 0);
  ASSERT_EQ(cli_add_command(&s_cli, CLI_CMD_ROOT, "cfg", cmd_name, 0, CLI_MODE_ANY, ""),
            CLI_CMD_INVALID);
#if CLI_ENABLE_ALIASES
  /* An alias may not shadow a sibling name either. */
  ASSERT_EQ(cli_cmd_add_alias(&s_cli, s_h_show, "showall"), CLI_ERR);
  ASSERT_EQ(cli_cmd_add_alias(&s_cli, s_h_show, "SHOW"), CLI_ERR);
#endif

#if CLI_ENABLE_AUTH
  /* Over-long enable secrets are refused instead of being silently shortened. */
  char long_secret[CLI_MAX_ENABLE_SECRET_LEN + 4];
  memset(long_secret, 's', sizeof(long_secret) - 1U);
  long_secret[sizeof(long_secret) - 1U] = '\0';
  ASSERT_EQ(cli_set_enable_secret(&s_cli, "ok"), CLI_OK);
  ASSERT_EQ(cli_set_enable_secret(&s_cli, long_secret), CLI_ERR);
  ASSERT_STR_EQ(s_cli.enable_privilege_secret, "ok");
#endif
  TEST_END();
}

static void test_help_and_completion_ui(void) {
  TEST_START("help_and_completion_ui");
  session_new();
  session_start();

  /* "word?" lists matching commands at this level, not the children of one of them. */
  type("sh?");
#if CLI_ENABLE_COMMAND_HELP
  ASSERT_OUT("show ");
  ASSERT_OUT("showall");
#else
  ASSERT_OUT("  show\r\n");
  ASSERT_OUT("showall");
#endif
  ASSERT_NOT_OUT("version");
  type("\x15");

#if CLI_ENABLE_TAB_COMPLETION
  /* Completion rewrites a plainly typed word in the keyword's case. */
  type("SH\t");
  ASSERT_STR_EQ(s_cli.input, "show");
  type("\x15");
#endif

  /* Usage text only mentions what exists. */
  out_clear();
  type("usage commands\r");
#if CLI_ENABLE_ALIASES
  ASSERT_OUT("clear / cls");
#else
  ASSERT_OUT("  clear  ");
#endif
  ASSERT_NOT_OUT("help show");

  /* Ctrl+L on a dumb terminal does not emit ANSI codes. */
  cli_set_ansi_supported(&s_cli, false);
  out_clear();
  type("\x0c");
  ASSERT_NOT_OUT("\x1b[2J");
  ASSERT_OUT("rtr> ");
  TEST_END();
}

static void test_remove_relocates_descendants(void) {
  TEST_START("remove_relocates_descendants");
  session_new();
  session_start();

  type("calc\r");
  type("arith\r");
  ASSERT_TRUE(s_cli.context != CLI_CMD_ROOT && s_cli.context != s_h_calc);
  /* Non-recursive removal of 'calc' while inside its child submenu 'arith'. */
  ASSERT_EQ(cli_remove_command(&s_cli, s_h_calc), CLI_OK);
  ASSERT_EQ(s_cli.context, CLI_CMD_ROOT);
  for (uint8_t i = 0U; i < s_cli.nav_depth; i++) {
    ASSERT_EQ(s_cli.nav_stack[i].context, CLI_CMD_ROOT);
  }
  TEST_END();
}

static void test_engine_rejects_bad_transport(void) {
  TEST_START("engine_rejects_bad_transport");
  ASSERT_EQ(cli_session_engine(NULL), CLI_ERR);

  session_new();
  s_cli.transport.read = NULL; /* e.g. a transport init that failed */
  cli_start(&s_cli);
  ASSERT_EQ(cli_session_engine(&s_cli), CLI_ERR);
  ASSERT_EQ(cli_get_session_state(&s_cli), CLI_SESSION_STOP);
  TEST_END();
}

static void test_write_error_ends_session(void) {
  TEST_START("write_error_ends_session");
  session_new();
  session_start();

  s_write_fails = true; /* peer went away */
  ASSERT_EQ(type("help\r"), CLI_ERR);
  ASSERT_EQ(cli_get_session_state(&s_cli), CLI_SESSION_STOP);
  TEST_END();
}

/*=======================================================================================
 * Round 3
 *=======================================================================================*/

static void test_restart_keeps_line_ending(void) {
  TEST_START("restart_keeps_line_ending");
  session_new();
  cli_set_quit_policy(&s_cli, CLI_QUIT_POLICY_RESET_SESSION);
  session_start();

  /* The LF of "quit\r\n" belongs to the same line: exactly one prompt follows. */
  ASSERT_EQ(type("quit\r\n"), CLI_OK);
  ASSERT_EQ(out_count("rtr> "), 1);
  TEST_END();
}

static void test_help_after_arguments(void) {
  TEST_START("help_after_arguments");
  session_new();
  session_start();

  /* Words after a command without subcommands are arguments, not unknown commands. */
  ASSERT_EQ(type("echo 1 ?"), CLI_OK);
  ASSERT_OUT("<cr>");
  ASSERT_NOT_OUT("Unknown");
#if CLI_ENABLE_TAB_COMPLETION
  out_clear();
  ASSERT_EQ(type("\x15"
                 "echo 1 x\t"),
            CLI_OK);
  ASSERT_OUT("\a");
#endif
  TEST_END();
}

static void test_submenu_offers_root_commands(void) {
  TEST_START("submenu_offers_root_commands");
  session_new();
  session_start();

  ASSERT_EQ(type("calc\r"), CLI_OK);
  ASSERT_TRUE(s_cli.context == s_h_calc);
  out_clear();

#if CLI_ENABLE_TAB_COMPLETION
  /* 'ec' matches nothing in the submenu; execution would run the root 'echo'. */
  ASSERT_EQ(type("ec\t"), CLI_OK);
  ASSERT_OUT("echo ");
  out_clear();
#endif
  ASSERT_EQ(type("\x15?"), CLI_OK);
  ASSERT_OUT("arith");
  ASSERT_OUT("echo");
  out_clear();
  ASSERT_EQ(type("\x15help\r"), CLI_OK);
  ASSERT_OUT("arith");
  ASSERT_OUT("echo");
  out_clear();
  ASSERT_EQ(type("help echo\r"), CLI_OK);
  ASSERT_NOT_OUT("Unknown");
  TEST_END();
}

static void test_start_begins_fresh_session(void) {
  TEST_START("start_begins_fresh_session");
  session_new();
  session_start();

  ASSERT_EQ(type("enable\r"), CLI_OK);
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_PRIVILEGED);
  ASSERT_EQ(type("quit\r"), CLI_ERR_QUIT);

  /* A new session on the same struct starts unprivileged with empty history. */
  cli_start(&s_cli);
  ASSERT_EQ(run(), CLI_OK);
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
#if CLI_ENABLE_HISTORY
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_EXEC);
  ASSERT_EQ(s_cli.history_count, 0);
#else
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_EXEC);
#endif

  /* A write failure of the previous peer does not end the next session. */
  s_write_fails = true;
  ASSERT_EQ(type("help\r"), CLI_ERR);
  s_write_fails = false;
  cli_start(&s_cli);
  ASSERT_EQ(run(), CLI_OK);
  ASSERT_EQ(cli_get_session_state(&s_cli), CLI_SESSION_RUN);
  TEST_END();
}

static void test_set_mode_restores_privilege(void) {
  TEST_START("set_mode_restores_privilege");
  session_new();
  session_start();

  ASSERT_EQ(cli_set_mode(&s_cli, CLI_MODE_ANY), CLI_ERR);
  ASSERT_EQ(type("enable\r"), CLI_OK);
  ASSERT_EQ(type("configure\r"), CLI_OK);
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_CONFIG);

  /* Returning to exec mode leaves the level 'enable' entered, with its privilege. */
  ASSERT_EQ(cli_set_mode(&s_cli, CLI_MODE_EXEC), CLI_OK);
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_EXEC);
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
  ASSERT_EQ(s_cli.nav_depth, 0);
  TEST_END();
}

static void test_rx_budget_per_tick(void) {
  TEST_START("rx_budget_per_tick");
  session_new();
  session_start();

  for (size_t i = 0; i < (size_t)CLI_MAX_RX_BYTES_PER_TICK + 100U; i++) {
    s_in[s_in_len++] = 'a';
  }
  ASSERT_EQ(cli_session_engine(&s_cli), CLI_OK);
  ASSERT_EQ(s_in_pos, (size_t)CLI_MAX_RX_BYTES_PER_TICK);
  ASSERT_EQ(cli_session_engine(&s_cli), CLI_OK);
  ASSERT_EQ(s_in_pos, s_in_len);
  TEST_END();
}

static void test_no_commands_after_write_error(void) {
  TEST_START("no_commands_after_write_error");
  session_new();
  session_start();

  s_write_fails = true;
  ASSERT_EQ(type("showall\rshowall\r"), CLI_ERR);
  ASSERT_EQ(s_cb_count, 0);
  TEST_END();
}

#if CLI_ENABLE_AUTH
static void test_auth_prompt_keeps_running_args(void) {
  TEST_START("auth_prompt_keeps_running_args");
  session_new();
  session_start();

  ASSERT_EQ(type("relogin\r"), CLI_OK);
  ASSERT_OUT("ARG0:relogin");
  ASSERT_OUT("Username: ");
  ASSERT_EQ(s_cli.input_len, 0);
  ASSERT_EQ(s_cli.input[0], '\0');

  /* The same through cli_exec() from outside a command, with a half-typed line. */
  session_new();
  session_start();
  ASSERT_EQ(type("ech"), CLI_OK);
  ASSERT_EQ(cli_exec(&s_cli, "relogin"), CLI_OK);
  ASSERT_OUT("ARG0:relogin");
  ASSERT_EQ(s_cli.input_len, 0);
  ASSERT_EQ(s_cli.input[0], '\0');
  TEST_END();
}
#endif

#if CLI_ENABLE_IDLE_TIMEOUT && CLI_ENABLE_AUTH
static void test_idle_untouched_session_not_reset(void) {
  TEST_START("idle_untouched_session_not_reset");

  /* Waiting at the first login prompt: nothing to reset. */
  session_new();
  cli_require_authorization(&s_cli, true);
  cli_set_idle_timeout(&s_cli, 5U);
  session_start();
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_USERNAME);
  s_now += 100U;
  (void)run();
  ASSERT_NOT_OUT("Idle timeout");

  /* Once something was typed, the reset applies (half-typed credentials are dropped). */
  type("adm");
  s_now += 6U;
  (void)run();
  ASSERT_EQ(out_count("Idle timeout. Session restarted."), 1);

  /* The LF of a CR LF that restarted the session is not new activity. */
  session_new();
  cli_set_quit_policy(&s_cli, CLI_QUIT_POLICY_RESET_SESSION);
  cli_set_idle_timeout(&s_cli, 5U);
  session_start();
  type("quit\r\n");
  s_now += 100U;
  (void)run();
  ASSERT_NOT_OUT("Idle timeout");

  /* Closing still counts from the start: an unused connection is closed. */
  session_new();
  cli_set_idle_timeout(&s_cli, 5U);
  cli_set_idle_timeout_mode(&s_cli, CLI_IDLE_TIMEOUT_POLICY_CLOSE);
  session_start();
  s_now += 6U;
  ASSERT_EQ(run(), CLI_ERR_QUIT);
  ASSERT_OUT("Idle timeout.");
  TEST_END();
}
#endif

/* Round 4 regressions ------------------------------------------------------------------*/

#if CLI_ENABLE_AUTH
static void test_overlong_credentials_rejected(void) {
  char user[CLI_MAX_USERNAME_LEN];
  char pass[CLI_MAX_PASSWORD_LEN];
  char line[CLI_MAX_USERNAME_LEN + CLI_MAX_PASSWORD_LEN + 8];

  TEST_START("overlong_credentials_rejected");
  /* Longest valid username / password: typing one more character must not be cut back to a
   * matching value. */
  memset(user, 'u', sizeof(user) - 1U);
  user[sizeof(user) - 1U] = '\0';
  memset(pass, 'p', sizeof(pass) - 1U);
  pass[sizeof(pass) - 1U] = '\0';

  session_new();
  ASSERT_EQ(cli_add_user(&s_cli, user, pass, CLI_PRIV_USER), CLI_OK);
  cli_require_authorization(&s_cli, true);
  session_start();
  (void)snprintf(line, sizeof(line), "%s\r%sX\r", user, pass);
  type(line);
  ASSERT_OUT("Authentication failed");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_USERNAME);
  (void)snprintf(line, sizeof(line), "%sX\r%s\r", user, pass);
  type(line);
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_USERNAME);
  (void)snprintf(line, sizeof(line), "%s\r%s\r", user, pass);
  type(line);
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_AUTHENTICATED);

  /* Enable secret of the maximum length, typed with an extra character. */
  memset(pass, 's', sizeof(pass) - 1U);
  session_new();
  ASSERT_EQ(cli_set_enable_secret(&s_cli, pass), CLI_OK);
  session_start();
  (void)snprintf(line, sizeof(line), "enable\r%sX\r", pass);
  type(line);
  ASSERT_OUT("Access denied");
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
  TEST_END();
}
#endif

#if CLI_ENABLE_AUTH && CLI_ENABLE_IDLE_TIMEOUT
static void test_lockout_survives_idle_timeout(void) {
  TEST_START("lockout_survives_idle_timeout");
  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_USER);
  cli_require_authorization(&s_cli, true);
  cli_set_auth_failure_mode(&s_cli, CLI_AUTH_FAILURE_MODE_LOCKOUT);
  cli_set_auth_lockout_duration(&s_cli, 30U);
  cli_set_idle_timeout(&s_cli, 10U);
  session_start();

  type("x\rx\rx\rx\rx\rx\r");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_LOCKOUT);
  s_now += 11U; /* idle period over, lockout not */
  out_clear();
  (void)run();
  ASSERT_NOT_OUT("Idle timeout");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_LOCKOUT);

  s_now += 20U; /* lockout over: login prompt, and a fresh idle period */
  (void)run();
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_USERNAME);
  ASSERT_NOT_OUT("Idle timeout");
  type("admin\rpw\r");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_AUTHENTICATED);
  TEST_END();
}
#endif

static void test_empty_word_is_no_command(void) {
  TEST_START("empty_word_is_no_command");
  session_new();
  session_start();

  /* "show" has a single subcommand: an empty word must not select it. */
  type("show \"\"\r");
  ASSERT_EQ(s_cb_count, 0);
  ASSERT_OUT("Unknown command");
  out_clear();
  type("help show \"\"\r");
  ASSERT_OUT("Unknown command");
  TEST_END();
}

#if CLI_ENABLE_AUTH && CLI_ENABLE_HISTORY
static void test_relogin_not_in_new_history(void) {
  TEST_START("relogin_not_in_new_history");
  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_USER);
  cli_require_authorization(&s_cli, true);
  cli_set_quit_policy(&s_cli, CLI_QUIT_POLICY_RESET_SESSION);
  session_start();

  type("admin\rpw\rshow version\rrelogin\radmin\rpw\r");
  ASSERT_EQ(s_cli.history_count, 0);
  type("show version\rquit\r");
  (void)run();
  type("admin\rpw\r");
  ASSERT_EQ(s_cli.history_count, 0);
  TEST_END();
}
#endif

/* Round 5 regressions ------------------------------------------------------------------*/

#if CLI_ENABLE_PERIODIC_CALLBACK
static int8_t periodic_restart_cb(cli_struct_t *cli) {
  cli_restart_session(cli);
  return CLI_OK;
}
#endif

static void test_restart_holds_pending_input(void) {
  TEST_START("restart_holds_pending_input");

#if CLI_ENABLE_AUTH && CLI_ENABLE_IDLE_TIMEOUT
  /* Idle reset in the tick that also brings a keystroke: it waits for the login prompt
   * instead of reaching the command editor (no command list before login). */
  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_USER);
  cli_require_authorization(&s_cli, true);
  cli_set_idle_timeout(&s_cli, 5U);
  session_start();
  type("admin\rpw\r");
  s_now += 10U;
  out_clear();
  type("?");
  ASSERT_OUT("Idle timeout. Session restarted.");
  ASSERT_NOT_OUT("Show commands");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_USERNAME);
  ASSERT_STR_EQ(s_cli.input, "?");
#endif

#if CLI_ENABLE_IDLE_TIMEOUT
  /* Without login: the whole line appears after the new prompt and runs as shown. */
  session_new();
  cli_set_idle_timeout(&s_cli, 5U);
  session_start();
  type("x");
  s_now += 10U;
  out_clear();
  type("echo abc\r"); /* the restart dropped the typed "x" */
  ASSERT_OUT("rtr> echo abc");
  ASSERT_OUT("ARGS [abc]");
#endif

#if CLI_ENABLE_PERIODIC_CALLBACK
  /* Same for a periodic callback that restarts the session. */
  session_new();
  cli_set_periodic_cb(&s_cli, periodic_restart_cb);
  cli_set_periodic_interval(&s_cli, 1U);
  session_start();
  s_now += 2U;
  out_clear();
  type("echo abc\r");
  ASSERT_OUT("rtr> echo abc");
  ASSERT_OUT("ARGS [abc]");
#endif
  TEST_END();
}

#if CLI_ENABLE_AUTH
static int8_t cmd_enable_twice(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                               const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  (void)cli_exec(cli, "enable");
  return cli_exec(cli, "enable"); /* a password prompt is already pending: refused */
}

#if CLI_ENABLE_TIME_SOURCE
static char s_enable_user[CLI_MAX_USERNAME_LEN + 1];

static int8_t enable_user_cb(cli_struct_t *cli, const char *user, const char *pass) {
  (void)cli;
  (void)snprintf(s_enable_user, sizeof(s_enable_user), "%s", user);
  return (strcmp(pass, "s3") == 0) ? CLI_OK : CLI_ERR_AUTH;
}
#endif
#endif

#if CLI_ENABLE_AUTH
static void test_enable_state_handling(void) {
  TEST_START("enable_state_handling");

  /* A second 'enable' while the password prompt is pending must not become the state to
   * return to: after the right secret the shell works normally again. */
  session_new();
  ASSERT_EQ(cli_set_enable_secret(&s_cli, "s3"), CLI_OK);
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "dbl", cmd_enable_twice, CLI_PRIV_USER,
                        CLI_MODE_ANY, "");
  session_start();
  type("dbl\r");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_ENABLE_MODE_PASSWORD);
  type("s3\r");
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_PRIVILEGED);
  ASSERT_FALSE(s_cli.auth_state == CLI_AUTH_ENABLE_MODE_PASSWORD);
  out_clear();
  type("echo x\r");
  ASSERT_OUT("ARGS [x]");

#if CLI_ENABLE_TIME_SOURCE
  /* The end of an enable lockout keeps the logged-in user's name. */
  session_new();
  (void)cli_add_user(&s_cli, "alice", "pw", CLI_PRIV_USER);
  cli_require_authorization(&s_cli, true);
  cli_set_enable_privilege_cb(&s_cli, enable_user_cb);
  cli_set_auth_failure_mode(&s_cli, CLI_AUTH_FAILURE_MODE_LOCKOUT);
  cli_set_auth_lockout_duration(&s_cli, 30U);
  session_start();
  type("alice\rpw\r");
  type("enable\rx\renable\rx\renable\rx\r");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_LOCKOUT);
  s_now += 31U;
  (void)run();
  s_enable_user[0] = '\0';
  type("enable\rs3\r");
  ASSERT_STR_EQ(s_enable_user, "alice");
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_PRIVILEGED);
#endif
  TEST_END();
}
#endif

#if CLI_ENABLE_HISTORY
static void test_history_browse_ends_on_submit(void) {
  TEST_START("history_browse_ends_on_submit");
  session_new();
  session_start();

  type("echo aa\r");
  type("echo bb\r");
  type("\x1b[A\r"); /* re-run the newest entry: a duplicate, not stored again */
  type("\x1b[A");
  ASSERT_STR_EQ(s_cli.input, "echo bb");

  /* Browsing, then submitting an empty line, also ends the browse. */
  type("\x1b[A\x15\r");
  type("\x1b[A");
  ASSERT_STR_EQ(s_cli.input, "echo bb");
  TEST_END();
}
#endif

static int8_t quit_hook_partial(cli_struct_t *cli) {
  cli_print(cli, "Really quit?");
  return CLI_ERR;
}

static void test_ctrl_d_output_like_quit(void) {
  TEST_START("ctrl_d_output_like_quit");
  session_new();
  cli_set_quit_cb(&s_cli, quit_hook_partial);
  session_start();

  type("\x04");
  ASSERT_OUT("Really quit?\r\nrtr> ");
  TEST_END();
}

static void test_exec_from_outside_redraws(void) {
  TEST_START("exec_from_outside_redraws");
  session_new();
  session_start();

  /* A callback runs a command while the user is typing: its output goes below the line,
   * then the prompt and the typed text are shown again. */
  type("ec");
  out_clear();
  ASSERT_EQ(cli_exec(&s_cli, "echo hi"), CLI_OK);
  ASSERT_OUT("\r\nARGS [hi]\r\n");
  ASSERT_OUT("rtr> ec");
  type("ho x\r");
  ASSERT_OUT("ARGS [x]");
  TEST_END();
}

static void test_escape_parameter_saturates(void) {
  TEST_START("escape_parameter_saturates");
  session_new();
  session_start();

  type("\x1b[999999");
  ASSERT_EQ(s_cli.esc_param, 9999);
  type("~x");
  ASSERT_STR_EQ(s_cli.input, "x");
  TEST_END();
}

static void test_registration_reports_failures(void) {
  cli_transport_struct_t tp;
  cli_cmd_struct_t small_pool[4];

  TEST_START("registration_reports_failures");
  session_new();

#if CLI_ENABLE_MODE_NAMES
  /* A mode name that does not fit is refused; the old one stays. */
  ASSERT_EQ(cli_set_mode_name(&s_cli, CLI_MODE_USER_BASE, "lab"), CLI_OK);
  ASSERT_EQ(cli_set_mode_name(&s_cli, CLI_MODE_USER_BASE,
                              "a-mode-name-that-is-much-longer-than-the-buffer"),
            CLI_ERR);
#endif

  /* Built-ins that do not fit the pool are reported. */
  memset(&tp, 0, sizeof(tp));
  tp.available = t_available;
  tp.read = t_read;
  tp.write = t_write;
  cli_init(&s_cli, "rtr", &tp, NULL, small_pool, 4);
  ASSERT_EQ(cli_add_builtin_cmds(&s_cli), CLI_ERR);
  session_new();
  ASSERT_EQ(s_cli.cmd_pool_count > 0, true);
  TEST_END();
}

/* Round 6 regressions ------------------------------------------------------------------*/

#if CLI_ENABLE_AUTH && CLI_ENABLE_IDLE_TIMEOUT
static int8_t idle_relogin_cb(cli_struct_t *cli) {
  cli_restart_session(cli);
  cli_request_auth(cli); /* during the restart: must not be lost */
  return CLI_OK;
}
#endif

#if CLI_ENABLE_AUTH && CLI_ENABLE_IDLE_TIMEOUT
static void test_relogin_requested_during_restart(void) {
  TEST_START("relogin_requested_during_restart");
  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_USER);
  cli_set_idle_timeout_cb(&s_cli, 5U, idle_relogin_cb);
  session_start();

  type("x");
  s_now += 10U;
  out_clear();
  (void)run();
  ASSERT_OUT("Username: ");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_USERNAME);
  out_clear();
  type("echo hi\r"); /* a username, not a command */
  ASSERT_NOT_OUT("ARGS [hi]");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_PASSWORD);

  /* A new connection (cli_start) forgets a request made for the previous one. */
  session_new();
  cli_set_idle_timeout_cb(&s_cli, 5U, idle_relogin_cb);
  session_start();
  type("x");
  s_now += 10U;
  (void)run();
  cli_start(&s_cli);
  (void)run();
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_NONE);
  TEST_END();
}
#endif

static void test_unreachable_children_ignored(void) {
  cli_cmd_handle_t h;

  TEST_START("unreachable_children_ignored");
  session_new();
  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "info", cmd_echo, CLI_PRIV_USER, CLI_MODE_ANY,
                      "Info");
  (void)cli_add_command(&s_cli, h, "secret", cmd_name, CLI_PRIV_PRIVILEGED, CLI_MODE_ANY, "");
  session_start();

  /* For a user the privileged child does not exist: 'info' takes arguments, is no submenu. */
  type("info abc\r");
  ASSERT_OUT("ARGS [abc]");
  type("info\r");
  ASSERT_EQ(s_cli.context, CLI_CMD_ROOT);
  out_clear();
  type("info ?");
  ASSERT_NOT_OUT("secret");
  TEST_END();
}

static int8_t cmd_quit_inside(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                              const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  (void)cli_exec(cli, "quit");
  return CLI_OK; /* the session is stopped all the same */
}

static void test_engine_reports_stop_at_once(void) {
  const char *line = "qi\r";

  TEST_START("engine_reports_stop_at_once");
  session_new();
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "qi", cmd_quit_inside, CLI_PRIV_USER, CLI_MODE_ANY,
                        "");
  session_start();

  memcpy(&s_in[s_in_len], line, strlen(line));
  s_in_len += strlen(line);
  int8_t rc = cli_session_engine(&s_cli); /* one tick: ASSERT_EQ evaluates twice */
  ASSERT_EQ(rc, CLI_ERR_QUIT);
  TEST_END();
}

#if CLI_ENABLE_AUTH
static int8_t cmd_lock(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_request_auth(cli);
  cli_println(cli, "Session locked.");
  return CLI_OK;
}
#endif

#if CLI_ENABLE_AUTH
static void test_credential_prompt_placement(void) {
  TEST_START("credential_prompt_placement");

  /* From a command: the prompt follows the command's output. */
  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_USER);
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "lock", cmd_lock, CLI_PRIV_USER, CLI_MODE_ANY, "");
  session_start();
  type("lock\r");
  ASSERT_OUT("Session locked.\r\nUsername: ");

  /* From outside any command while the user types: on a fresh line. */
  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_USER);
  session_start();
  type("ech");
  out_clear();
  cli_request_auth(&s_cli);
  ASSERT_TRUE(strncmp(s_out, "\r\nUsername: ", 12U) == 0);

  /* cli_exec() of such a command at the login prompt: the prompt is shown once. */
  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_USER);
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "lock", cmd_lock, CLI_PRIV_USER, CLI_MODE_ANY, "");
  cli_require_authorization(&s_cli, true);
  session_start();
  out_clear();
  (void)cli_exec(&s_cli, "lock");
  ASSERT_EQ(out_count("Username: "), 1);

  /* The enable password prompt follows the command's output too. */
  session_new();
  ASSERT_EQ(cli_set_enable_secret(&s_cli, "s3"), CLI_OK);
  session_start();
  out_clear();
  type("enable\r");
  ASSERT_OUT("enable\r\nPassword: ");
  ASSERT_EQ(out_count("Password: "), 1);
  TEST_END();
}
#endif

#if CLI_ENABLE_AUTH
static void test_partial_secret_erased(void) {
  TEST_START("partial_secret_erased");

  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_USER);
  cli_require_authorization(&s_cli, true);
  session_start();
  type("admin\rhunter");
  ASSERT_EQ(s_cli.input_len, 6);
  cli_restart_session(&s_cli);
  ASSERT_TRUE(s_cli.input[0] == '\0' && s_cli.input[1] == '\0' && s_cli.input[5] == '\0');

  session_new();
  ASSERT_EQ(cli_set_enable_secret(&s_cli, "s3cret"), CLI_OK);
  session_start();
  type("enable\rs3cr");
  cli_start(&s_cli);
  ASSERT_TRUE(s_cli.input[0] == '\0' && s_cli.input[1] == '\0' && s_cli.input[3] == '\0');
  TEST_END();
}
#endif

/* Round 7 regressions ------------------------------------------------------------------*/

#define TEST_MODE_IF (CLI_MODE_USER_BASE + 7)

static int8_t cmd_interface(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                            const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  return cli_set_mode(cli, TEST_MODE_IF); /* its subcommands live in TEST_MODE_IF */
}

static void test_submenu_entered_after_mode_switch(void) {
  cli_cmd_handle_t h;

  TEST_START("submenu_entered_after_mode_switch");
  session_new();
  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "interface", cmd_interface, CLI_PRIV_USER,
                      CLI_MODE_EXEC, "Interface");
  (void)cli_add_command(&s_cli, h, "shutdown", cmd_name, CLI_PRIV_USER, TEST_MODE_IF, "Shut");
  session_start();

  type("interface\r");
  ASSERT_EQ(s_cli.context, h);
  ASSERT_EQ(cli_get_mode(&s_cli), TEST_MODE_IF);
  out_clear();
  type("shutdown\r");
  ASSERT_OUT("RAN:shutdown");
  type("exit\r");
  ASSERT_EQ(s_cli.context, CLI_CMD_ROOT);
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_EXEC);
  TEST_END();
}

static void test_new_mode_starts_at_root(void) {
  cli_cmd_handle_t net;

  TEST_START("new_mode_starts_at_root");
  session_new();
  /* A submenu that stays in exec mode. */
  net = cli_add_command(&s_cli, CLI_CMD_ROOT, "net", cmd_name, CLI_PRIV_USER, CLI_MODE_EXEC, "");
  (void)cli_add_command(&s_cli, net, "ping", cmd_name, CLI_PRIV_USER, CLI_MODE_EXEC, "");
  session_start();

  type("net\r");
  ASSERT_EQ(s_cli.context, net);
  out_clear();
  type("enable\r"); /* no secret: privileged exec, which has nothing to do with 'net' */
  ASSERT_EQ(s_cli.context, CLI_CMD_ROOT);
  ASSERT_OUT("rtr# ");
  type("exit\r"); /* back where 'enable' was typed */
  ASSERT_EQ(s_cli.context, net);
  ASSERT_EQ(cli_get_privilege(&s_cli), CLI_PRIV_USER);
  TEST_END();
}

static void test_unusable_container_hidden(void) {
  cli_cmd_handle_t h;

  TEST_START("unusable_container_hidden");
  session_new();
  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "debug", NULL, CLI_PRIV_USER, CLI_MODE_ANY, "Debug");
  (void)cli_add_command(&s_cli, h, "memory", cmd_name, CLI_PRIV_PRIVILEGED, CLI_MODE_ANY, "");
  session_start();

  type("debug memory\r");
  ASSERT_OUT("% Unknown command: debug");
  out_clear();
  type("help\r");
  ASSERT_NOT_OUT("debug");

  /* Once a subcommand is usable, the container is too. */
  type("enable\r");
  out_clear();
  type("debug memory\r");
  ASSERT_OUT("RAN:memory");
  TEST_END();
}

#if CLI_ENABLE_AUTH
static int8_t enable_relogin_cb(cli_struct_t *cli, const char *user, const char *pass) {
  (void)user;
  if (strcmp(pass, "s3") != 0) {
    cli_request_auth(cli); /* a wrong secret logs the user out */
    return CLI_ERR_AUTH;
  }
  return CLI_OK;
}

static void test_enable_callback_relogin(void) {
  TEST_START("enable_callback_relogin");
  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_USER);
  cli_require_authorization(&s_cli, true);
  cli_set_enable_privilege_cb(&s_cli, enable_relogin_cb);
  session_start();

  type("admin\rpw\r");
  out_clear();
  type("enable\rwrong\r");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_USERNAME);
  ASSERT_EQ(out_count("Username: "), 1);
  ASSERT_NOT_OUT("Access denied");
  out_clear();
  type("echo hi\r"); /* a username now, not a command */
  ASSERT_NOT_OUT("ARGS [hi]");
  TEST_END();
}
#endif

/* Round 8 regressions ------------------------------------------------------------------*/

static size_t s_flushed_len; /* s_out_len at the last transport flush */

static cli_transport_ret_t t_flush(void *ctx) {
  (void)ctx;
  s_flushed_len = s_out_len;
  return 0;
}

static void test_output_flushed_every_tick(void) {
  TEST_START("output_flushed_every_tick");
  session_new();
  s_cli.transport.flush = t_flush;
  s_flushed_len = 0;
  session_start();

  type("hel"); /* echo only: no prompt follows */
  ASSERT_EQ(s_flushed_len, s_out_len);
#if CLI_ENABLE_TAB_COMPLETION
  type("\t");
  ASSERT_OUT("p ");
  ASSERT_EQ(s_flushed_len, s_out_len);
#endif
  type("\r");
  out_clear();
  s_flushed_len = 0;
  (void)cli_exec(&s_cli, "echo x"); /* from the main loop, outside any tick */
  ASSERT_OUT("ARGS [x]");
  ASSERT_EQ(s_flushed_len, s_out_len);
  TEST_END();
}

static int8_t cmd_bye(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_print(cli, "line one\n");
  cli_print(cli, "Goodbye"); /* no newline: the command's output ends it */
  cli_restart_session(cli);
  return CLI_OK;
}

static void test_restart_keeps_command_output(void) {
  TEST_START("restart_keeps_command_output");
  session_new();
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "bye", cmd_bye, CLI_PRIV_USER, CLI_MODE_ANY, "");
  session_start();

  type("bye\r");
  ASSERT_OUT("line one\r\nGoodbye\r\n");
#if CLI_ENABLE_PIPE_FILTER
  out_clear();
  type("bye | count\r");
  ASSERT_OUT("Count: 2");
  out_clear();
  type("bye | include Good\r");
  ASSERT_OUT("Goodbye\r\n");
  ASSERT_NOT_OUT("line one");
#endif

  /* Outside any command the new session starts below the line being typed. */
  type("sho");
  out_clear();
  cli_restart_session(&s_cli);
  (void)run();
  ASSERT_TRUE(strncmp(s_out, "\r\n", 2) == 0);
  ASSERT_OUT("rtr> ");
  TEST_END();
}

static int8_t cmd_logout(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                         const char *argv[]) {
  (void)cli;
  (void)cmd;
  (void)argc;
  (void)argv;
  return CLI_ERR_QUIT; /* ends the session */
}

static void test_exec_quit_ends_session(void) {
  TEST_START("exec_quit_ends_session");
  session_new();
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "logout", cmd_logout, CLI_PRIV_USER, CLI_MODE_ANY,
                        "");
  session_start();

  ASSERT_EQ(cli_exec(&s_cli, "logout"), CLI_ERR_QUIT);
  ASSERT_EQ(cli_get_session_state(&s_cli), CLI_SESSION_STOP);
  ASSERT_NOT_OUT("rtr> ");
  ASSERT_EQ(cli_session_engine(&s_cli), CLI_ERR_QUIT);
  TEST_END();
}

static int8_t cmd_su(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_set_privilege(cli, (cli_priv_t)15);
  return CLI_OK;
}

static void test_enable_keeps_higher_privilege(void) {
  TEST_START("enable_keeps_higher_privilege");
  session_new();
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "su", cmd_su, CLI_PRIV_USER, CLI_MODE_EXEC, "");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "admin", cmd_name, (cli_priv_t)15, CLI_MODE_ANY, "");
  session_start();

  type("su\renable\r");
  ASSERT_EQ(cli_get_privilege(&s_cli), (cli_priv_t)15);
  ASSERT_EQ(cli_get_mode(&s_cli), CLI_MODE_ENABLE);
  out_clear();
  type("admin\r");
  ASSERT_OUT("RAN:admin");
  TEST_END();
}

#if CLI_ENABLE_AUTH
static int8_t restart_cb(cli_struct_t *cli, const char *user, const char *pass) {
  (void)user;
  (void)pass;
  cli_restart_session(cli);
  return CLI_ERR_AUTH;
}

static int8_t login_relogin_cb(cli_struct_t *cli, const char *user, const char *pass) {
  (void)user;
  (void)pass;
  cli_request_auth(cli); /* asks for a new login instead of granting this one */
  return CLI_OK;
}

static void test_credential_callback_restart(void) {
  TEST_START("credential_callback_restart");
  session_new();
  cli_set_enable_privilege_cb(&s_cli, restart_cb);
  session_start();

  type("enable\rx\r");
  ASSERT_EQ(out_count("rtr> "), 1); /* only the new session's prompt */
  ASSERT_NOT_OUT("Access denied");
  ASSERT_EQ(cli_get_session_state(&s_cli), CLI_SESSION_RUN);

  session_new();
  cli_require_authorization(&s_cli, true);
  cli_set_authorization_cb(&s_cli, restart_cb);
  session_start();
  type("admin\rpw\r");
  ASSERT_EQ(out_count("Username: "), 1);
  ASSERT_NOT_OUT("Authentication failed");
  ASSERT_EQ(s_cli.auth_attempts, 0);

  session_new();
  cli_require_authorization(&s_cli, true);
  cli_set_authorization_cb(&s_cli, login_relogin_cb);
  session_start();
  type("admin\rpw\r");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_USERNAME);
  ASSERT_EQ(out_count("Username: "), 1);
  ASSERT_NOT_OUT("rtr> ");
  TEST_END();
}
#endif

static int8_t cmd_nop(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cli;
  (void)cmd;
  (void)argc;
  (void)argv;
  return CLI_OK;
}

#if CLI_ENABLE_PRINT_TRANSPORT_CB
static char s_lines[256];

static void t_line(cli_struct_t *cli, const char *line) {
  (void)cli;
  size_t len = strlen(s_lines);
  (void)snprintf(&s_lines[len], sizeof(s_lines) - len, "<%s>", line);
}
#endif

static void test_async_partial_line_kept_apart(void) {
  TEST_START("async_partial_line_kept_apart");
  session_new();
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "nop", cmd_nop, CLI_PRIV_USER, CLI_MODE_ANY, "");
  session_start();

  (void)cli_print(&s_cli, "[tick]"); /* outside any command, e.g. from a callback */
  type("nop\r");
  ASSERT_NOT_OUT("\r\n\r\n");
  ASSERT_OUT("[tick]nop\r\nrtr> ");

#if CLI_ENABLE_PRINT_TRANSPORT_CB
  s_lines[0] = '\0';
  cli_set_print_transport_cb(&s_cli, t_line);
  (void)cli_print(&s_cli, "[tick]");
  type("echo hi\r");
  ASSERT_STR_EQ(s_lines, "<[tick]><ARGS [hi]>");
#endif
  TEST_END();
}

#if CLI_ENABLE_HISTORY
static void test_history_lists_whole_entries(void) {
  /* Longest line that fits next to the 'history' command itself. */
  enum {
    ENTRY_LEN = (CLI_MAX_INPUT_LEN - 1 < CLI_HISTORY_BUF_SIZE - 9) ? CLI_MAX_INPUT_LEN - 1
                                                                   : CLI_HISTORY_BUF_SIZE - 9
  };
  static char entry[ENTRY_LEN + 2];

  TEST_START("history_lists_whole_entries");
  session_new();
  session_start();

  memset(entry, 'a', ENTRY_LEN);
  entry[0] = 'x';
  entry[1] = ' ';
  entry[ENTRY_LEN] = '\r';
  entry[ENTRY_LEN + 1] = '\0';
  type(entry);
  out_clear();
  type("history\r");
  entry[ENTRY_LEN] = '\0';
  ASSERT_OUT(entry); /* not cut at CLI_MAX_OUTPUT_BUF */
  TEST_END();
}
#endif

static void test_print_contracts(void) {
  TEST_START("print_contracts");
  session_new();
  session_start();

  ASSERT_EQ(cli_println(&s_cli, "abc"), 5); /* includes the \r\n */
  ASSERT_EQ(s_out_len, 5U);
#if CLI_ENABLE_PRINT_TRANSPORT_CB
  s_lines[0] = '\0';
  cli_set_print_transport_cb(&s_cli, t_line);
  type("warn\r");
  ASSERT_STR_EQ(s_lines, "<partial><% bad thing>");
#endif
  TEST_END();
}

#if CLI_ENABLE_ALIASES
static void test_duplicate_of_submenu_rejected(void) {
  cli_cmd_handle_t leaf;

  TEST_START("duplicate_of_submenu_rejected");
  session_new();
  ASSERT_EQ(cli_add_command_duplicate(&s_cli, s_h_show, "sh2"), CLI_CMD_INVALID);
  leaf = cli_add_command(&s_cli, CLI_CMD_ROOT, "ping", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "");
  ASSERT_TRUE(cli_add_command_duplicate(&s_cli, leaf, "pong") >= 0);
  session_start();
  type("pong\r");
  ASSERT_OUT("RAN:pong"); /* the duplicate's handler is called with its own name */
  TEST_END();
}
#endif

/* Round 9 regressions ------------------------------------------------------------------*/

#if CLI_ENABLE_TIME_SOURCE
static uint32_t s_now2; /* a second clock */

static uint32_t t_now2(void *ctx) {
  (void)ctx;
  return s_now2;
}
#endif

#if CLI_ENABLE_PERIODIC_CALLBACK
static int s_periodic_calls;

static int8_t periodic_count_cb(cli_struct_t *cli) {
  (void)cli;
  s_periodic_calls++;
  return CLI_OK;
}

static void test_time_source_change_keeps_periodic(void) {
  TEST_START("time_source_change_keeps_periodic");
  session_new();
  cli_set_periodic_cb(&s_cli, periodic_count_cb);
  cli_set_periodic_interval(&s_cli, 60U);
  session_start();
  s_periodic_calls = 0;

  s_now += 10U; /* 10 s of the 60 s period have passed */
  (void)run();
  s_now2 = 5U;
  cli_set_time_source(&s_cli, t_now2, NULL);
  (void)run();
  ASSERT_EQ(s_periodic_calls, 0); /* the new clock's reading is not "time passed" */
  s_now2 += 49U;
  (void)run();
  ASSERT_EQ(s_periodic_calls, 0);
  s_now2 += 1U; /* 60 s in all */
  (void)run();
  ASSERT_EQ(s_periodic_calls, 1);
  TEST_END();
}
#endif

#if CLI_ENABLE_AUTH && CLI_ENABLE_TIME_SOURCE
static void test_time_source_change_keeps_lockout(void) {
  TEST_START("time_source_change_keeps_lockout");
  session_new();
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_USER);
  cli_require_authorization(&s_cli, true);
  cli_set_auth_failure_mode(&s_cli, CLI_AUTH_FAILURE_MODE_LOCKOUT);
  cli_set_auth_lockout_duration(&s_cli, 300U);
  session_start();

  type("x\rx\rx\rx\rx\rx\r");
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_LOCKOUT);
  s_now += 10U;
  s_now2 = 5U;
  cli_set_time_source(&s_cli, t_now2, NULL);
  (void)run();
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_LOCKOUT);
  s_now2 += 289U; /* 299 s in all */
  (void)run();
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_LOCKOUT);
  s_now2 += 1U;
  (void)run();
  ASSERT_EQ(s_cli.auth_state, CLI_AUTH_USERNAME);
  TEST_END();
}
#endif

#if CLI_ENABLE_ALIASES
static void test_duplicate_sees_all_children(void) {
  cli_cmd_handle_t h;

  TEST_START("duplicate_sees_all_children");
  session_new();
  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "show2", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "");
  (void)cli_add_command(&s_cli, h, "running", cmd_name, CLI_PRIV_PRIVILEGED, CLI_MODE_ANY, "");
  ASSERT_EQ(cli_add_command_duplicate(&s_cli, h, "display"), CLI_CMD_INVALID);
  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "iface", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "");
  (void)cli_add_command(&s_cli, h, "mtu", cmd_name, CLI_PRIV_USER, CLI_MODE_CONFIG, "");
  ASSERT_EQ(cli_add_command_duplicate(&s_cli, h, "intf"), CLI_CMD_INVALID);
  TEST_END();
}
#endif

static void test_error_after_callback_output(void) {
  TEST_START("error_after_callback_output");
  session_new();
  session_start();

  /* Default transport: a partial line, then the error on a line of its own. */
  (void)cli_print(&s_cli, "temp=42");
  ASSERT_EQ(cli_error(&s_cli, "abc"), 7); /* "% abc\r\n" */
  ASSERT_OUT("temp=42\r\n% abc\r\n");
#if CLI_ENABLE_PRINT_TRANSPORT_CB
  s_lines[0] = '\0';
  cli_set_print_transport_cb(&s_cli, t_line);
  (void)cli_print(&s_cli, "temp=");
  (void)cli_print(&s_cli, "42");
  (void)cli_error(&s_cli, "sensor fault");
  ASSERT_STR_EQ(s_lines, "<temp=42><% sensor fault>");
#endif
  TEST_END();
}

#if CLI_ENABLE_IDLE_TIMEOUT
static void test_idle_reset_message_placement(void) {
  TEST_START("idle_reset_message_placement");
  session_new();
  cli_set_idle_timeout(&s_cli, 5U);
  cli_set_idle_timeout_mode(&s_cli, CLI_IDLE_TIMEOUT_POLICY_RESET_SESSION);
  session_start();

  type("ab");
  out_clear();
  s_now += 5U;
  (void)run();
  ASSERT_OUT("\r\n% Idle timeout. Session restarted.\r\nrtr> ");
  ASSERT_NOT_OUT("\r\n\r\n");
  TEST_END();
}
#endif

#if CLI_ENABLE_TAB_COMPLETION
static void test_tab_inside_word_completes_word(void) {
  TEST_START("tab_inside_word_completes_word");
  session_new();
  session_start();

  type("ech");
  type("\x1b[D\x1b[D"); /* cursor between "e" and "ch" */
  type("\t");
  ASSERT_STR_EQ(s_cli.input, "echo ");
  ASSERT_EQ(s_cli.cursor, 5);

  type("\x15"); /* Ctrl-U: empty line */
  type("show vers extra");
  type("\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D"); /* inside "vers" */
  type("\t");
  ASSERT_STR_EQ(s_cli.input, "show version  extra");
  TEST_END();
}
#endif

#if CLI_ENABLE_PIPE_FILTER
static void test_help_after_pipe(void) {
  TEST_START("help_after_pipe");
  session_new();
  session_start();

  type("show | ?"); /* 'show' has subcommands: the filter part is described */
  ASSERT_NOT_OUT("Unknown command");
  ASSERT_OUT("  grep ");
  ASSERT_OUT("  include ");
  ASSERT_OUT("  count ");
  type("\x15");
  out_clear();
  type("echo x | ex?");
  ASSERT_OUT("  exclude ");
  ASSERT_NOT_OUT("  grep ");
  type("\x15");
  out_clear();
  type("echo x | inc ?");
  ASSERT_OUT("  <pattern> ");
  type("\x15");
  out_clear();
  type("echo x | count ?");
  ASSERT_OUT("  <cr>");
  type("\x15");
  out_clear();
  type("echo x | foo ?");
  ASSERT_OUT("% Invalid filter");
  TEST_END();
}
#endif

#if CLI_ENABLE_PIPE_FILTER && CLI_ENABLE_TAB_COMPLETION
static void test_tab_after_pipe(void) {
  TEST_START("tab_after_pipe");
  session_new();
  session_start();

  type("echo x | inc\t");
  ASSERT_STR_EQ(s_cli.input, "echo x | include ");
  type("\x15");
  type("echo x | CO\t");
  ASSERT_STR_EQ(s_cli.input, "echo x | count ");
  type("\x15");
  out_clear();
  type("echo x |\t"); /* every keyword is listed */
  ASSERT_OUT("  begin ");
  ASSERT_OUT("  exclude ");
  type("\x15");
  type("echo x | include fo\t"); /* the pattern is free text: nothing is completed */
  ASSERT_STR_EQ(s_cli.input, "echo x | include fo");
  TEST_END();
}
#endif

static void test_null_safety(void) {
  TEST_START("null_safety");
  session_new();

  ASSERT_EQ(cli_get_privilege(NULL), CLI_PRIV_USER);
  ASSERT_EQ(cli_get_mode(NULL), -1);
  ASSERT_EQ(cli_print(NULL, "x"), -1);
  cli_print_flush(NULL);
  cli_set_hostname(&s_cli, NULL);
  ASSERT_STR_EQ(s_cli.hostname, "");
  cli_set_banner(&s_cli, NULL);
  ASSERT_EQ(cli_add_user(&s_cli, NULL, "pw", 0), CLI_ERR);
  ASSERT_EQ(cli_add_user(&s_cli, "", "pw", 0), CLI_ERR);
  cli_init(NULL, "x", NULL, NULL, s_pool, TEST_POOL_SIZE);
  TEST_END();
}

/*=======================================================================================
 * Main
 *=======================================================================================*/

int main(void) {
  printf("=== cli engine tests ===\n\n");

  printf("--- dispatch ---\n");
  RUN_SUITE(test_help_shows_text);
  RUN_SUITE(test_subcommand_runs_once);
  RUN_SUITE(test_exact_match_ignores_case);
  RUN_SUITE(test_hidden_command_exact_only);
  RUN_SUITE(test_print_partial_line);

  printf("\n--- navigation ---\n");
  RUN_SUITE(test_exit_unwinds_modes);
  RUN_SUITE(test_end_and_disable);
  RUN_SUITE(test_user_mode_submenus);
  RUN_SUITE(test_navigation_depth_limit);
#if CLI_ENABLE_MODE_NAMES
  RUN_SUITE(test_mode_name_for_user_mode);
#endif
  RUN_SUITE(test_ctrl_d_follows_quit_policy);

  printf("\n--- command pool ---\n");
  RUN_SUITE(test_negative_handles_rejected);
  RUN_SUITE(test_add_command_validation);
  RUN_SUITE(test_reinit_clears_pool);
  RUN_SUITE(test_remove_detaches_children);

  printf("\n--- line editor ---\n");
  RUN_SUITE(test_csi_with_parameters);
#if CLI_ENABLE_TAB_COMPLETION
  RUN_SUITE(test_tab_completes_at_cursor);
#endif
#if CLI_ENABLE_TAB_COMPLETION && CLI_ENABLE_ALIASES
  RUN_SUITE(test_tab_common_prefix_of_aliases);
#endif
  RUN_SUITE(test_question_mark);

  printf("\n--- tokenizer / exec / filters ---\n");
  RUN_SUITE(test_quoted_arguments);
  RUN_SUITE(test_exec_api);
#if CLI_ENABLE_PIPE_FILTER && CLI_ENABLE_COMMAND_HELP
  RUN_SUITE(test_pipe_filters);
#endif
  RUN_SUITE(test_print_cb_v_kind);

  printf("\n--- history ---\n");
#if CLI_ENABLE_HISTORY
  RUN_SUITE(test_history_ring_evicts_oldest);
#endif
#if CLI_ENABLE_HISTORY
  RUN_SUITE(test_history_cleared_on_restart);
#endif

  printf("\n--- authentication ---\n");
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_enable_prompt_waits_for_password);
#endif
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_password_keys_not_echoed);
#endif
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_login_prompt_hides_history);
#endif
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_enable_attempt_limit);
#endif
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_auth_callback_sets_privilege);
#endif

  printf("\n--- idle timeout ---\n");
#if CLI_ENABLE_IDLE_TIMEOUT
  RUN_SUITE(test_idle_callback_always_fires);
#endif
#if CLI_ENABLE_IDLE_TIMEOUT
  RUN_SUITE(test_idle_reset_fires_once);
#endif

  printf("\n--- round 2 ---\n");
  RUN_SUITE(test_nested_exec);
  RUN_SUITE(test_exec_preserves_editor);
  RUN_SUITE(test_line_ending_pairs);
  RUN_SUITE(test_output_line_handling);
  RUN_SUITE(test_short_writes);
  RUN_SUITE(test_registration_rules);
  RUN_SUITE(test_help_and_completion_ui);
  RUN_SUITE(test_remove_relocates_descendants);
  RUN_SUITE(test_engine_rejects_bad_transport);

  printf("\n--- round 3 ---\n");
  RUN_SUITE(test_restart_keeps_line_ending);
  RUN_SUITE(test_help_after_arguments);
  RUN_SUITE(test_submenu_offers_root_commands);
  RUN_SUITE(test_start_begins_fresh_session);
  RUN_SUITE(test_set_mode_restores_privilege);
  RUN_SUITE(test_rx_budget_per_tick);
  RUN_SUITE(test_no_commands_after_write_error);
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_auth_prompt_keeps_running_args);
#endif
#if CLI_ENABLE_IDLE_TIMEOUT && CLI_ENABLE_AUTH
  RUN_SUITE(test_idle_untouched_session_not_reset);
#endif
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_overlong_credentials_rejected);
#endif
#if CLI_ENABLE_AUTH && CLI_ENABLE_IDLE_TIMEOUT
  RUN_SUITE(test_lockout_survives_idle_timeout);
#endif
  RUN_SUITE(test_empty_word_is_no_command);
#if CLI_ENABLE_AUTH && CLI_ENABLE_HISTORY
  RUN_SUITE(test_relogin_not_in_new_history);
#endif
  RUN_SUITE(test_restart_holds_pending_input);
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_enable_state_handling);
#endif
#if CLI_ENABLE_HISTORY
  RUN_SUITE(test_history_browse_ends_on_submit);
#endif
  RUN_SUITE(test_ctrl_d_output_like_quit);
  RUN_SUITE(test_exec_from_outside_redraws);
  RUN_SUITE(test_escape_parameter_saturates);
  RUN_SUITE(test_registration_reports_failures);
#if CLI_ENABLE_AUTH && CLI_ENABLE_IDLE_TIMEOUT
  RUN_SUITE(test_relogin_requested_during_restart);
#endif
  RUN_SUITE(test_unreachable_children_ignored);
  RUN_SUITE(test_engine_reports_stop_at_once);
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_credential_prompt_placement);
#endif
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_partial_secret_erased);
#endif
  RUN_SUITE(test_submenu_entered_after_mode_switch);
  RUN_SUITE(test_new_mode_starts_at_root);
  RUN_SUITE(test_unusable_container_hidden);
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_enable_callback_relogin);
#endif
  RUN_SUITE(test_output_flushed_every_tick);
  RUN_SUITE(test_restart_keeps_command_output);
  RUN_SUITE(test_exec_quit_ends_session);
  RUN_SUITE(test_enable_keeps_higher_privilege);
#if CLI_ENABLE_AUTH
  RUN_SUITE(test_credential_callback_restart);
#endif
  RUN_SUITE(test_async_partial_line_kept_apart);
#if CLI_ENABLE_HISTORY
  RUN_SUITE(test_history_lists_whole_entries);
#endif
  RUN_SUITE(test_print_contracts);
#if CLI_ENABLE_ALIASES
  RUN_SUITE(test_duplicate_of_submenu_rejected);
#endif
#if CLI_ENABLE_PERIODIC_CALLBACK
  RUN_SUITE(test_time_source_change_keeps_periodic);
#endif
#if CLI_ENABLE_AUTH && CLI_ENABLE_TIME_SOURCE
  RUN_SUITE(test_time_source_change_keeps_lockout);
#endif
#if CLI_ENABLE_ALIASES
  RUN_SUITE(test_duplicate_sees_all_children);
#endif
  RUN_SUITE(test_error_after_callback_output);
#if CLI_ENABLE_IDLE_TIMEOUT
  RUN_SUITE(test_idle_reset_message_placement);
#endif
#if CLI_ENABLE_TAB_COMPLETION
  RUN_SUITE(test_tab_inside_word_completes_word);
#endif
#if CLI_ENABLE_PIPE_FILTER
  RUN_SUITE(test_help_after_pipe);
#endif
#if CLI_ENABLE_PIPE_FILTER && CLI_ENABLE_TAB_COMPLETION
  RUN_SUITE(test_tab_after_pipe);
#endif

  printf("\n--- robustness ---\n");
  RUN_SUITE(test_write_error_ends_session);
  RUN_SUITE(test_null_safety);

  printf("\n=== Results: %d tests, %d asserts, %d failures ===\n", s_tests_run, s_asserts,
         s_tests_fail);

  return s_tests_fail > 0 ? 1 : 0;
}
