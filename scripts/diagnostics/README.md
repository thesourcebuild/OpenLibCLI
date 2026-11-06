# Diagnostics

Tools that measure what OpenLibCLI costs on a small MCU. They are not part of the
library or of any build; run them by hand after a change that may affect RAM or flash.

| File | Purpose |
| --- | --- |
| `avr_mem_stats.ps1` | Builds a minimal AVR serial firmware and reports its RAM and flash use |
| `avr_size_probe.c` | Holds one object of each library type, so `avr-nm` shows their sizes |
| `cli_serial.c`, `cli_serial.h` | Serial CLI application linked by `avr_mem_stats.ps1` (setup, poll, shutdown) |

## avr_mem_stats.ps1

Requirements: Windows PowerShell or PowerShell 7, and the `avr-gcc` of the Arduino AVR
core (installed by the Arduino IDE or `arduino-cli core install arduino:avr`). The
script finds it under `%LOCALAPPDATA%\Arduino15\packages`.

```powershell
./scripts/diagnostics/avr_mem_stats.ps1                     # ATmega328P (Uno): 2 KB SRAM, 32 KB flash
./scripts/diagnostics/avr_mem_stats.ps1 -Mcu atmega2560 -SramBytes 8192 -FlashBytes 262144
./scripts/diagnostics/avr_mem_stats.ps1 -TopAll             # every symbol, not only the largest 20
Get-Help ./scripts/diagnostics/avr_mem_stats.ps1 -Detailed  # all parameters
```

What it does:

1. Compiles `avr_size_probe.c`, `cli_serial.c`, `examples/shared/cli_demo_setup.c` and
   `cli/utils/cli_args_parse.c` with `-Os` and the AVR profile of
   `cli/config/cli_embedded_config.h`. On Arduino (`ARDUINO` defined) `cli_serial.c`
   includes `cli/cli.c` itself, so the engine is compiled as part of it.
2. Links them with a stub transport (no real UART) and `--gc-sections` into
   `build/avr_cli_serial_app.elf`.
3. Prints:
   - the sizes of `cli_struct_t`, `cli_cmd_struct_t` and the command pool;
   - static RAM (`.data` + `.bss`) and flash (`.text` + `.data`) of the firmware, as a
     share of `-SramBytes` / `-FlashBytes`;
   - the largest RAM and flash symbols.

The figures are for the library with the demo's commands. The stack, and the heap that
the library never uses, come on top of the static RAM.
