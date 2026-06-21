#define ARDUINO 10819
#define __AVR__ 1
/* Pool size for AVR sizing probe (matched to cli_embedded_config.h AVR default) */
#ifndef CLI_MAX_COMMANDS
#define CLI_MAX_COMMANDS 12
#endif
#include "cli.h"

char probe_cli_t[sizeof(cli_struct_t)];
char probe_cli_cmd_t[sizeof(cli_cmd_struct_t)];
char probe_cmd_pool[sizeof(cli_cmd_struct_t) * CLI_MAX_COMMANDS];
char probe_instances[sizeof(cli_struct_t)];
