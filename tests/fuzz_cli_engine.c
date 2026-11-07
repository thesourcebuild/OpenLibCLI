/**
 * @file fuzz_cli_engine.c
 * @brief Randomised stress test of the OpenLibCLI engine (cli/cli.c).
 *
 * Self-contained, deterministic (seeded) — no external framework required.
 *
 * One session receives random keystrokes, biased toward the bytes that drive the
 * line editor (escape sequences, control keys, quotes, '|', '?', Tab, command
 * words), mixed with random API calls (cli_exec(), session restarts, login
 * requests, output printed outside any command), clock jumps and transport write
 * failures. After every engine tick it checks:
 *   - the editor invariants (cursor and length in range, a terminated line, no
 *     command or pipe filter left active, a live submenu context);
 *   - that the terminal, modelled from everything the engine wrote, shows exactly
 *     the prompt and the line being edited, with the cursor at the editor's
 *     position.
 * Build it with sanitizers (-fsanitize=address,undefined) to also catch memory
 * errors. A failure prints its seed and iteration and the last input and output.
 *
 * Usage: fuzz_cli_engine [first_seed [seeds [iterations]]]
 *        (default: seeds 1-16, 20000 iterations; each seed runs with an ANSI and a
 *        dumb terminal)
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "cli.h"

/*=======================================================================================
 * State
 *=======================================================================================*/

#define FUZZ_POOL_SIZE 48

static cli_struct_t s_cli;
static cli_cmd_struct_t s_pool[FUZZ_POOL_SIZE];

static unsigned long long s_seed;
static unsigned long s_iter;
static uint64_t s_rng;

/* Input queued as typed, and the recent input / output for a failure report. */
static unsigned char s_in[4096];
static size_t s_in_len;
static size_t s_in_pos;
static unsigned char s_last_in[256];
static size_t s_last_in_n;
static char s_last_out[2048];
static size_t s_last_out_n;

static uint32_t s_now;
static bool s_write_fails;
static bool s_require_auth;

/* Terminal model: the line the cursor is on. */
static char s_screen[4096];
static int s_screen_len;
static int s_screen_col;
static int s_screen_esc; /* 0 normal, 1 after ESC, 2 inside a CSI sequence */

/* Output printed outside any command, or a renamed host, changes the prompt line without
 * redrawing it (by design); the line is checked again once the engine starts a new one. */
static bool s_screen_stale;
static bool s_in_engine;

/*=======================================================================================
 * Random Numbers
 *=======================================================================================*/

static uint32_t rnd(void) {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 7;
  s_rng ^= s_rng << 17;
  return (uint32_t)(s_rng >> 16);
}

static uint32_t below(uint32_t n) {
  return rnd() % n;
}

/*=======================================================================================
 * Terminal Model and Loopback Transport
 *=======================================================================================*/

static void screen_put(char c) {
  if (s_screen_esc == 1) {
    s_screen_esc = (c == '[') ? 2 : 0;
  } else if (s_screen_esc == 2) {
    if (c >= 0x40 && c <= 0x7E) {
      if (c == 'K') {
        s_screen_len = s_screen_col; /* erase to the end of the line */
      } else if (c == 'J') {
        s_screen_len = 0; /* erase the screen */
      } else if (c == 'H') {
        s_screen_col = 0; /* home */
      }
      s_screen_esc = 0;
    }
  } else if (c == '\x1b') {
    s_screen_esc = 1;
  } else if (c == '\r') {
    s_screen_col = 0;
  } else if (c == '\n') {
    s_screen_len = 0; /* a new, empty line */
    if (s_in_engine) {
      s_screen_stale = false;
    }
  } else if (c == '\b') {
    if (s_screen_col > 0) {
      s_screen_col--;
    }
  } else if (c == '\a') {
    /* Bell. */
  } else if ((unsigned char)c >= 0x20 && s_screen_col < (int)sizeof(s_screen) - 1) {
    while (s_screen_len < s_screen_col) {
      s_screen[s_screen_len++] = ' ';
    }
    s_screen[s_screen_col++] = c;
    if (s_screen_col > s_screen_len) {
      s_screen_len = s_screen_col;
    }
  }
}

static cli_transport_ret_t t_available(void *ctx) {
  (void)ctx;
  return (s_in_pos < s_in_len) ? 1 : 0;
}

static cli_transport_ret_t t_read(void *ctx) {
  unsigned char b;

  (void)ctx;
  b = s_in[s_in_pos++];
  s_last_in[s_last_in_n++ % sizeof(s_last_in)] = b;
  return (cli_transport_ret_t)b;
}

static cli_transport_ret_t t_write(void *ctx, const uint8_t *buf, cli_transport_buflen_t len) {
  cli_transport_ret_t rc = -1; /* a dead peer */

  (void)ctx;
  if (!s_write_fails) {
    for (cli_transport_buflen_t i = 0; i < len; i++) {
      screen_put((char)buf[i]);
      s_last_out[s_last_out_n++ % sizeof(s_last_out)] = (char)buf[i];
    }
    rc = (cli_transport_ret_t)len;
  }
  return rc;
}

static uint32_t t_now(void *ctx) {
  (void)ctx;
  return s_now;
}

/*=======================================================================================
 * Commands
 *=======================================================================================*/

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
  cli_print(cli, "Sure? "); /* a partial line */
  return CLI_OK;
}

static int8_t cmd_multi(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_print(cli, "a\nb\r\nc");
  (void)cli_error(cli, "oops %d", 3);
  cli_print(cli, "tail");
  return CLI_OK;
}

static int8_t cmd_name(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  cli_println(cli, "RAN:%s", cmd);
  return CLI_OK;
}

static int8_t cmd_mode(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  return cli_set_mode(cli, CLI_MODE_USER_BASE);
}

static int8_t cmd_nest(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  (void)cli_exec(cli, "echo inner \"q q\"");
  (void)cli_exec(cli, "ask");
  return CLI_OK;
}

static int8_t cmd_restart(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                          const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_print(cli, "bye");
  cli_restart_session(cli);
  return CLI_OK;
}

static int8_t cmd_reauth(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                         const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  cli_request_auth(cli);
  return CLI_OK;
}

static void add_submenu(cli_struct_t *cli) {
  cli_cmd_handle_t h =
      cli_add_command(cli, CLI_CMD_ROOT, "sub", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "Sub");
  if (h >= 0) {
    (void)cli_add_command(cli, h, "leaf", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "Leaf");
  }
}

static int8_t cmd_addsub(cli_struct_t *cli, const char *cmd, cli_argc_t argc,
                         const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  add_submenu(cli);
  return CLI_OK;
}

static int8_t cmd_rmsub(cli_struct_t *cli, const char *cmd, cli_argc_t argc, const char *argv[]) {
  (void)cmd;
  (void)argc;
  (void)argv;
  /* Removes the submenu the session may be in. */
  for (cli_cmd_handle_t h = 0; h < cli->cmd_pool_size; h++) {
    if ((cli->cmd_pool[h].flags & CLI_CMD_FLAG_IN_USE) != 0U &&
        strcmp(cli->cmd_pool[h].name, "sub") == 0) {
      (void)cli_remove_command(cli, h);
      break;
    }
  }
  return CLI_OK;
}

/*=======================================================================================
 * Session Setup and Random Input
 *=======================================================================================*/

static void session_setup(bool ansi) {
  cli_transport_struct_t tp;
  cli_platform_struct_t pf;
  cli_cmd_handle_t h;

  memset(&tp, 0, sizeof(tp));
  memset(&pf, 0, sizeof(pf));
  tp.available = t_available;
  tp.read = t_read;
  tp.write = t_write;
  pf.now_sec = t_now;

  cli_init(&s_cli, "rtr", &tp, &pf, s_pool, FUZZ_POOL_SIZE);
  (void)cli_add_builtin_cmds(&s_cli);
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "echo", cmd_echo, CLI_PRIV_USER, CLI_MODE_ANY,
                        "Echo");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "ask", cmd_ask, CLI_PRIV_USER, CLI_MODE_ANY, "Ask");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "multi", cmd_multi, CLI_PRIV_USER, CLI_MODE_ANY,
                        "Mixed output");
  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "show", NULL, CLI_PRIV_USER, CLI_MODE_ANY, "Show");
  (void)cli_add_command(&s_cli, h, "version", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "Version");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "showall", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY,
                        "Show all");
  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "calc", cmd_mode, CLI_PRIV_USER, CLI_MODE_ANY,
                      "Calculator mode");
  (void)cli_add_command(&s_cli, h, "add", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "Add");
  add_submenu(&s_cli);
  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "alpha", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "A");
#if CLI_ENABLE_ALIASES
  (void)cli_cmd_add_alias(&s_cli, h, "al1");
#endif
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "alpine", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY,
                        "B");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "nest", cmd_nest, CLI_PRIV_USER, CLI_MODE_ANY,
                        "Nested commands");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "restart", cmd_restart, CLI_PRIV_USER,
                        CLI_MODE_ANY, "");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "reauth", cmd_reauth, CLI_PRIV_USER, CLI_MODE_ANY,
                        "");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "rmsub", cmd_rmsub, CLI_PRIV_USER, CLI_MODE_ANY,
                        "");
  (void)cli_add_command(&s_cli, CLI_CMD_ROOT, "addsub", cmd_addsub, CLI_PRIV_USER, CLI_MODE_ANY,
                        "");
  h = cli_add_command(&s_cli, CLI_CMD_ROOT, "hid", cmd_name, CLI_PRIV_USER, CLI_MODE_ANY, "");
  cli_hide_command(&s_cli, h);

#if CLI_ENABLE_AUTH
  (void)cli_add_user(&s_cli, "admin", "pw", CLI_PRIV_PRIVILEGED);
  (void)cli_set_enable_secret(&s_cli, "s3");
  cli_require_authorization(&s_cli, s_require_auth);
  cli_set_auth_failure_mode(&s_cli, (below(2) != 0U) ? CLI_AUTH_FAILURE_MODE_LOCKOUT
                                                    : CLI_AUTH_FAILURE_MODE_CLOSE);
  cli_set_auth_lockout_duration(&s_cli, 5U);
#endif
  cli_set_quit_policy(&s_cli, (cli_quit_policy_enum_t)below(3));
  cli_set_exit_root_policy(&s_cli, (cli_exit_root_policy_enum_t)below(3));
#if CLI_ENABLE_IDLE_TIMEOUT
  cli_set_idle_timeout(&s_cli, (below(2) != 0U) ? 0U : 50U);
#endif
#if CLI_ENABLE_BANNER
  (void)cli_set_banner(&s_cli, "BANNER");
#endif
  cli_set_ansi_supported(&s_cli, ansi);
  cli_start(&s_cli);
}

static const char *const s_words[] = {
    "echo",    "ask",     "multi",  "show",    "version", "showall", "calc",      "add",
    "sub",     "leaf",    "alpha",  "alpine",  "al",      "al1",     "nest",      "restart",
    "reauth",  "rmsub",   "addsub", "hid",     "exit",    "quit",    "enable",    "disable",
    "end",     "configure", "help", "history", "usage",   "commands", "keys",     "clear",
    "cls",     "admin",   "pw",     "s3",      "|",       "include", "inc",       "grep",
    "exclude", "begin",   "count",  "\"",      "\\\"",    "\"a b\"", "x",         "1",
    "2",       "99",      "sh",     "e",
};

static const char *const s_keys[] = {
    "\r",       "\n",      "\r\n",     "\t",      "?",       "\x7f",    "\x08",    "\x01",
    "\x02",     "\x04",    "\x05",     "\x06",    "\x0b",    "\x0c",    "\x0e",    "\x10",
    "\x15",     "\x17",    "\x1b[A",   "\x1b[B",  "\x1b[C",  "\x1b[D",  "\x1b[H",  "\x1b[F",
    "\x1b[3~",  "\x1b[1~", "\x1b[4~",  "\x1bOA",  "\x1bOB",  "\x1bOH",  "\x1b",    "\x1b[",
    "\x1bO",    "\x1b[1;5C", "\x1b[200~", "\x1b[?25h", " ",   "  ",      "\x03",
};

static void queue(const char *s, size_t n) {
  if (s_in_len + n < sizeof(s_in)) {
    memcpy(&s_in[s_in_len], s, n);
    s_in_len += n;
  }
}

static void queue_random_input(void) {
  uint32_t pieces = 1U + below(6);

  for (uint32_t i = 0; i < pieces; i++) {
    uint32_t r = below(100);

    if (r < 45) {
      const char *w = s_words[below((uint32_t)(sizeof(s_words) / sizeof(s_words[0])))];
      queue(w, strlen(w));
      if (below(3) != 0U) {
        queue(" ", 1);
      }
    } else if (r < 85) {
      const char *k = s_keys[below((uint32_t)(sizeof(s_keys) / sizeof(s_keys[0])))];
      queue(k, strlen(k));
    } else {
      char c = (char)below(256); /* any byte, NUL and 8-bit included */
      queue(&c, 1);
    }
  }
}

/*=======================================================================================
 * Checks
 *=======================================================================================*/

static void print_bytes(const char *label, const unsigned char *ring, size_t ring_size,
                        size_t total, size_t max) {
  size_t n = (total < max) ? total : max;

  fprintf(stderr, "  %s: \"", label);
  for (size_t i = total - n; i < total; i++) {
    unsigned char c = ring[i % ring_size];
    if (c >= 0x20 && c < 0x7F) {
      fputc(c, stderr);
    } else {
      fprintf(stderr, "<%02X>", c);
    }
  }
  fputs("\"\n", stderr);
}

static void fail(const char *what) {
  fprintf(stderr, "FAIL seed=%llu iteration=%lu ansi=%d: %s\n", s_seed, s_iter,
          (int)s_cli.ansi_supported, what);
  fprintf(stderr, "  line=\"%s\" len=%d cursor=%d state=%d mode=%d priv=%d context=%d depth=%d\n",
          s_cli.input, (int)s_cli.input_len, (int)s_cli.cursor, (int)s_cli.session_state,
          (int)s_cli.mode, (int)s_cli.privilege, (int)s_cli.context, (int)s_cli.nav_depth);
  fprintf(stderr, "  screen=\"%.*s\" col=%d\n", s_screen_len, s_screen, s_screen_col);
  print_bytes("last input", s_last_in, sizeof(s_last_in), s_last_in_n, 200U);
  print_bytes("last output", (const unsigned char *)s_last_out, sizeof(s_last_out), s_last_out_n,
              600U);
  exit(1);
}

static bool secret_input(void) {
  bool secret = false;

#if CLI_ENABLE_AUTH
  secret = (s_cli.auth_state == CLI_AUTH_PASSWORD ||
            s_cli.auth_state == CLI_AUTH_ENABLE_MODE_PASSWORD);
#endif
  return secret;
}

/* The prompt the engine shows in the current state, or NULL when none is shown. */
static const char *expected_prompt(char *buf, size_t size) {
  const char *prompt = buf;
  const char *mode_name = NULL;
  bool command_prompt = true;

#if CLI_ENABLE_AUTH
  command_prompt = false;
  if (s_cli.auth_state == CLI_AUTH_USERNAME) {
    prompt = "Username: ";
  } else if (secret_input()) {
    prompt = "Password: ";
  } else if (s_cli.auth_state == CLI_AUTH_LOCKOUT) {
    prompt = NULL;
  } else {
    command_prompt = true;
  }
#endif
  if (!command_prompt) {
    /* A login prompt, or none during a lockout. */
  } else if (s_cli.context != CLI_CMD_ROOT) {
    (void)snprintf(buf, size, "%s> ", s_cli.cmd_pool[s_cli.context].name);
  } else {
#if CLI_ENABLE_MODE_NAMES
    for (int i = 0; i < CLI_MAX_MODES; i++) {
      if (s_cli.mode_name_ids[i] == s_cli.mode && s_cli.mode_names[i][0] != '\0') {
        mode_name = s_cli.mode_names[i];
      }
    }
#endif
    if (mode_name != NULL) {
      (void)snprintf(buf, size, "%s(%s)%c ", s_cli.hostname, mode_name,
                     (s_cli.privilege >= CLI_PRIV_PRIVILEGED) ? '#' : '>');
    } else {
      (void)snprintf(buf, size, "%s%c ", s_cli.hostname,
                     (s_cli.privilege >= CLI_PRIV_PRIVILEGED) ? '#' : '>');
    }
  }
  return prompt;
}

static void check_state(void) {
  if (s_cli.cursor < 0 || s_cli.cursor > s_cli.input_len) {
    fail("cursor outside the line");
  }
  if (s_cli.input_len < 0 || s_cli.input_len >= CLI_MAX_INPUT_LEN) {
    fail("line length outside the buffer");
  }
  if (s_cli.input[s_cli.input_len] != '\0' || (int)strlen(s_cli.input) != (int)s_cli.input_len) {
    fail("line not terminated at its length");
  }
  if (s_cli.nav_depth > CLI_MAX_CONTEXT_DEPTH) {
    fail("navigation deeper than CLI_MAX_CONTEXT_DEPTH");
  }
  if (s_cli.exec_depth != 0U) {
    fail("a command is still marked running between ticks");
  }
#if CLI_ENABLE_PIPE_FILTER
  if (s_cli.filter_type != CLI_FILTER_NONE) {
    fail("a pipe filter is still active between ticks");
  }
#endif
#if CLI_HAS_LINE_BUFFER
  if (s_cli.filter_line_buf_len < 0 || s_cli.filter_line_buf_len >= CLI_MAX_OUTPUT_BUF) {
    fail("output line buffer length out of range");
  }
#endif
#if CLI_ENABLE_HISTORY
  if (s_cli.history_count > CLI_MAX_HISTORY || s_cli.history_nav > s_cli.history_count) {
    fail("history counters out of range");
  }
#endif
  if (s_cli.context != CLI_CMD_ROOT &&
      (s_cli.context < 0 || s_cli.context >= FUZZ_POOL_SIZE ||
       (s_cli.cmd_pool[s_cli.context].flags & CLI_CMD_FLAG_IN_USE) == 0U)) {
    fail("submenu context is not a live command");
  }
}

static void check_screen(void) {
  static char shown[sizeof(s_screen)];
  char prompt_buf[256];
  char want[sizeof(s_screen)];
  const char *prompt = NULL;
  int want_len;
  int shown_len = s_screen_len;

  /* Only while the editor waits at a prompt, with nothing drawn outside its control. */
  if (s_cli.session_state == CLI_SESSION_RUN && !s_cli.tx_error && s_screen_esc == 0 &&
      !s_screen_stale) {
    prompt = expected_prompt(prompt_buf, sizeof(prompt_buf));
  }

  if (prompt != NULL) {
    (void)snprintf(want, sizeof(want), "%s%s", prompt, secret_input() ? "" : s_cli.input);
    want_len = (int)strlen(want);
    memcpy(shown, s_screen, (size_t)shown_len);
    shown[shown_len] = '\0';
    /* Spaces that erased characters are not visible: compare without trailing spaces. */
    while (want_len > 0 && want[want_len - 1] == ' ') {
      want[--want_len] = '\0';
    }
    while (shown_len > 0 && shown[shown_len - 1] == ' ') {
      shown[--shown_len] = '\0';
    }
    if (strcmp(shown, want) != 0) {
      fail("screen line differs from prompt + edited line");
    }
    if (!secret_input() && s_screen_col != (int)strlen(prompt) + (int)s_cli.cursor) {
      fail("screen cursor differs from the editor's cursor");
    }
  }
}

/*=======================================================================================
 * Run
 *=======================================================================================*/

static void run_seed(unsigned long long seed, unsigned long iterations, bool ansi) {
  static const char *const exec_lines[] = {
      "echo hi", "ask", "multi", "help", "calc", "exit", "history", "nest",
      "show version | inc V", "quit",
  };

  s_seed = seed;
  s_rng = seed * 2654435761ULL + 1ULL;
  s_in_len = 0;
  s_in_pos = 0;
  s_last_in_n = 0;
  s_last_out_n = 0;
  s_now = 1000U;
  s_write_fails = false;
  s_screen_len = 0;
  s_screen_col = 0;
  s_screen_esc = 0;
  s_screen_stale = false;
  s_in_engine = false;
  s_require_auth = (below(2) != 0U);
  session_setup(ansi);

  for (s_iter = 0; s_iter < iterations; s_iter++) {
    uint32_t r = below(100);
    int8_t rc;

    if (s_in_pos == s_in_len) {
      s_in_len = 0;
      s_in_pos = 0;
    }
    if (r < 88) {
      queue_random_input();
    } else if (r < 92) {
      s_now += below(20);
    } else if (r < 94) {
      (void)cli_exec(&s_cli, exec_lines[below((uint32_t)(sizeof(exec_lines) /
                                                          sizeof(exec_lines[0])))]);
    } else if (r < 95) {
      cli_restart_session(&s_cli);
    } else if (r < 96) {
#if CLI_ENABLE_AUTH
      cli_request_auth(&s_cli);
#endif
    } else if (r < 97) {
      /* Output outside any command, as from a periodic callback. */
      s_screen_stale = true;
      (void)cli_print(&s_cli, "async%d", (int)below(9));
      if (below(2) != 0U) {
        (void)cli_println(&s_cli, "");
      }
    } else if (r < 98) {
      s_screen_stale = true;
      (void)cli_set_hostname(&s_cli, (below(2) != 0U) ? "R1" : "router-long-name");
    } else {
      /* A dead peer for one tick: the session must end. */
      s_write_fails = true;
      queue("x", 1);
    }

    s_in_engine = true;
    rc = cli_session_engine(&s_cli);
    s_in_engine = false;
    if (s_write_fails) {
      s_write_fails = false;
      if (s_cli.tx_error && cli_get_session_state(&s_cli) != CLI_SESSION_STOP) {
        fail("a write failure did not end the session");
      }
    }
    if (rc != CLI_OK || cli_get_session_state(&s_cli) == CLI_SESSION_STOP) {
      /* A new connection: the terminal starts on a fresh line. */
      s_in_len = 0;
      s_in_pos = 0;
      s_screen_len = 0;
      s_screen_col = 0;
      s_screen_esc = 0;
      s_screen_stale = false;
      cli_start(&s_cli);
      cli_set_ansi_supported(&s_cli, ansi);
    } else {
      check_state();
      check_screen();
    }
  }
  cli_done(&s_cli);
}

int main(int argc, char **argv) {
  unsigned long long first = (argc > 1) ? strtoull(argv[1], NULL, 10) : 1ULL;
  unsigned long seeds = (argc > 2) ? strtoul(argv[2], NULL, 10) : 16UL;
  unsigned long iterations = (argc > 3) ? strtoul(argv[3], NULL, 10) : 20000UL;

  printf("=== cli engine fuzz: seeds %llu-%llu, %lu iterations ===\n", first,
         first + seeds - 1ULL, iterations);
  for (unsigned long i = 0; i < seeds; i++) {
    run_seed(first + i, iterations, true);
    run_seed(first + i, iterations, false);
  }
  printf("=== Results: %lu seeds x 2 terminals, 0 failures ===\n", seeds);
  return 0;
}

/*################################### END OF FILE ######################################*/
