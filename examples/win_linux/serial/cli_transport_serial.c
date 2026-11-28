/**
 * @file cli_transport_serial.c
 * @brief Host serial stdin/stdout transport implementation.
 *
 * Implements the host stdin/stdout serial transport and the POSIX
 * file-descriptor transport used for host stream endpoints.
 *
 * @copyright Copyright (c) 2026 thesourcebuild and Muhammad Hassaan Shah
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "cli_transport_serial.h"

#if ENV_IS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif ENV_IS_UNIX_LIKE
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <unistd.h>
#endif

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/** @brief How often a pipe is checked for input while @c cli_serial_wait() waits (a pipe
 *         handle cannot be waited on). */
#define SERIAL_PIPE_POLL_MS 10U
#endif

/*=======================================================================================
 * Private Macros
 *=======================================================================================*/

/* No private function-like macros. */

/*=======================================================================================
 * Private Types
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/** @brief What stdin is, which decides how it is read and waited for. */
typedef enum {
  SERIAL_INPUT_CONSOLE = 0, /**< A console: key events, read with ReadConsoleInputA(). */
  SERIAL_INPUT_PIPE,        /**< A pipe: bytes, checked with PeekNamedPipe(). */
  SERIAL_INPUT_STREAM       /**< A file or another device: a read returns data, or the end. */
} serial_input_kind_enum_t;
#endif

/*=======================================================================================
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Private Variables
 *=======================================================================================*/

#if ENV_IS_WINDOWS
static HANDLE s_serial_in = INVALID_HANDLE_VALUE;  /**< stdin. */
static HANDLE s_serial_out = INVALID_HANDLE_VALUE; /**< stdout. */
static serial_input_kind_enum_t s_serial_in_kind = SERIAL_INPUT_STREAM;
static uint8_t s_serial_key_buf[4];  /**< Bytes of the console key being delivered. */
static uint8_t s_serial_key_len = 0; /**< Bytes in s_serial_key_buf. */
static uint8_t s_serial_key_idx = 0; /**< Next byte of s_serial_key_buf to read. */
static WORD s_serial_key_repeat = 0; /**< Further times the key is delivered (held down). */
#endif

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

static cli_transport_ret_t cli_serial_transport_available(void *raw_ctx);
static cli_transport_ret_t cli_serial_transport_read(void *raw_ctx);
static cli_transport_ret_t cli_serial_transport_write(void *raw_ctx, const uint8_t *buf,
                                                      cli_transport_buflen_t len);

#if ENV_IS_WINDOWS
static cli_transport_ret_t serial_console_available(void);
static void serial_key_set(const char *seq);
static void serial_translate_key(const KEY_EVENT_RECORD *key);
#endif

#if ENV_IS_UNIX_LIKE
static cli_transport_ret_t fd_available_raw(int32_t fd, uint32_t timeout_ms);
static cli_transport_ret_t fd_read_raw(int32_t fd);
static cli_transport_ret_t fd_write_raw(int32_t fd, const uint8_t *buf, cli_transport_buflen_t len);
static cli_transport_ret_t cli_fd_transport_available(void *ctx);
static cli_transport_ret_t cli_fd_transport_read(void *ctx);
static cli_transport_ret_t cli_fd_transport_write(void *ctx, const uint8_t *buf,
                                                  cli_transport_buflen_t len);
#endif

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

void cli_serial_init(cli_transport_struct_t *transport) {
#if ENV_IS_WINDOWS
  DWORD console_mode;

  /* stdin and stdout as the process got them: a console, or redirected to a file or pipe. */
  s_serial_in = GetStdHandle(STD_INPUT_HANDLE);
  s_serial_out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (GetConsoleMode(s_serial_in, &console_mode) != FALSE) {
    s_serial_in_kind = SERIAL_INPUT_CONSOLE;
  } else if (GetFileType(s_serial_in) == FILE_TYPE_PIPE) {
    s_serial_in_kind = SERIAL_INPUT_PIPE;
  } else {
    s_serial_in_kind = SERIAL_INPUT_STREAM;
  }
  s_serial_key_len = 0U;
  s_serial_key_idx = 0U;
  s_serial_key_repeat = 0U;
#endif

  transport->available = cli_serial_transport_available;
  transport->read = cli_serial_transport_read;
  transport->write = cli_serial_transport_write;
  /* write() / WriteFile() are unbuffered: nothing to flush (fsync() on a tty fails, and on a
   * redirected stdout it would force a disk sync after every prompt). */
  transport->flush = NULL;
  transport->ctx = NULL;
  transport->kind = CLI_TRANSPORT_SERIAL;
}

int32_t cli_serial_wait(uint32_t timeout_ms) {
  int32_t result = (int32_t)cli_serial_transport_available(NULL);

#if ENV_IS_WINDOWS
  DWORD start = GetTickCount();
  DWORD elapsed = 0U;

  /* A console handle is signalled while input events wait; one that brings no input (focus,
   * mouse, a resize) is discarded by the check, and the wait goes on. A pipe handle is
   * never signalled: it is checked again every few milliseconds. */
  while (result == 0 && elapsed < timeout_ms) {
    if (s_serial_in_kind == SERIAL_INPUT_CONSOLE) {
      if (WaitForSingleObject(s_serial_in, (DWORD)(timeout_ms - elapsed)) == WAIT_FAILED) {
        result = -1;
      }
    } else {
      Sleep(SERIAL_PIPE_POLL_MS);
    }
    if (result == 0) {
      result = (int32_t)cli_serial_transport_available(NULL);
    }
    elapsed = GetTickCount() - start;
  }
#elif ENV_IS_UNIX_LIKE
  if (result == 0) {
    result = (int32_t)fd_available_raw(STDIN_FILENO, timeout_ms);
  }
#else
  (void)timeout_ms;
#endif

  return result;
}

#if ENV_IS_UNIX_LIKE
void cli_fd_init(cli_fd_ctx_struct_t *ctx, cli_transport_struct_t *transport, int32_t read_fd,
                 int32_t write_fd) {
  /* The caller's context carries the descriptors, so any number of fd transports can be
   * active at once and initialisation cannot fail. */
  ctx->read_fd = read_fd;
  ctx->write_fd = write_fd;

  transport->available = cli_fd_transport_available;
  transport->read = cli_fd_transport_read;
  transport->write = cli_fd_transport_write;
  transport->flush = NULL;
  transport->ctx = ctx;
  transport->kind = CLI_TRANSPORT_SERIAL;
}

void cli_fd_deinit(cli_transport_struct_t *transport) {
  if (transport && transport->ctx) {
    cli_fd_ctx_struct_t *ctx = (cli_fd_ctx_struct_t *)transport->ctx;
    ctx->read_fd = -1;
    ctx->write_fd = -1;

    transport->read = NULL;
    transport->available = NULL;
    transport->write = NULL;
    transport->flush = NULL;
    transport->ctx = NULL;
    transport->kind = CLI_TRANSPORT_UNKNOWN;
  }
}
#endif

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

/**
 * @brief Report whether input from the host serial input stream is ready, without waiting.
 *
 * @param[in] raw_ctx  Unused.
 *
 * @return 1 if a byte can be read, 0 if not, or @c -1 on error / at the end of the input.
 */
static cli_transport_ret_t cli_serial_transport_available(void *raw_ctx) {
  cli_transport_ret_t result;

  (void)raw_ctx;

#if ENV_IS_WINDOWS
  if (s_serial_in_kind == SERIAL_INPUT_CONSOLE) {
    if (s_serial_key_idx < s_serial_key_len) {
      result = 1; /* the rest of a key */
    } else {
      result = serial_console_available();
    }
  } else if (s_serial_in_kind == SERIAL_INPUT_PIPE) {
    DWORD waiting = 0U;

    if (PeekNamedPipe(s_serial_in, NULL, 0U, NULL, &waiting, NULL) == FALSE) {
      result = -1; /* the writer closed it (the end of the input), or an error */
    } else {
      result = (waiting > 0U) ? 1 : 0;
    }
  } else {
    result = 1; /* a read returns data, or reports the end */
  }
#elif ENV_IS_UNIX_LIKE
  result = fd_available_raw(STDIN_FILENO, 0U);
#else
  result = -1;
#endif

  return result;
}

/**
 * @brief Read one byte from the host serial input stream.
 *
 * @param[in] raw_ctx  Unused.
 *
 * @return Byte value 0..255, or @c -1 on error / at the end of the input.
 */
static cli_transport_ret_t cli_serial_transport_read(void *raw_ctx) {
  cli_transport_ret_t result;

  (void)raw_ctx;

#if ENV_IS_WINDOWS
  if (s_serial_in_kind == SERIAL_INPUT_CONSOLE) {
    if (s_serial_key_idx >= s_serial_key_len) {
      (void)serial_console_available();
    }
    if (s_serial_key_idx < s_serial_key_len) {
      result = s_serial_key_buf[s_serial_key_idx];
      s_serial_key_idx++;
      if (s_serial_key_idx >= s_serial_key_len && s_serial_key_repeat > 0U) {
        s_serial_key_idx = 0U; /* the key again: it is held down */
        s_serial_key_repeat--;
      }
    } else {
      result = -1;
    }
  } else {
    uint8_t byte;
    DWORD got = 0U;

    if (ReadFile(s_serial_in, &byte, 1U, &got, NULL) != FALSE && got == 1U) {
      result = byte;
    } else {
      result = -1; /* the end of the input, or an error */
    }
  }
#elif ENV_IS_UNIX_LIKE
  result = fd_read_raw(STDIN_FILENO);
#else
  result = -1;
#endif

  return result;
}

/**
 * @brief Write bytes to the host serial output stream.
 *
 * @param[in] raw_ctx  Unused.
 * @param[in] buf      Source data.
 * @param[in] len      Number of bytes to transmit.
 *
 * @return Number of bytes written, or @c -1 on error.
 */
static cli_transport_ret_t cli_serial_transport_write(void *raw_ctx, const uint8_t *buf,
                                                      cli_transport_buflen_t len) {
  cli_transport_ret_t result;

  (void)raw_ctx;

#if ENV_IS_WINDOWS
  cli_transport_buflen_t sent = 0;

  /* stdout as given: a console (VT sequences are interpreted, see the server), a file or a
   * pipe. A short write is continued; a failed one (a closed pipe) is reported. */
  result = (cli_transport_ret_t)len;
  while (sent < len && result >= 0) {
    DWORD done = 0U;

    if (WriteFile(s_serial_out, &buf[sent], (DWORD)(len - sent), &done, NULL) != FALSE &&
        done > 0U) {
      sent += (cli_transport_buflen_t)done;
    } else {
      result = -1;
    }
  }
#elif ENV_IS_UNIX_LIKE
  result = fd_write_raw(STDOUT_FILENO, buf, len);
#else
  (void)buf;
  (void)len;
  result = -1;
#endif

  return result;
}

#if ENV_IS_WINDOWS
/**
 * @brief Translate the console's waiting input events until one produces input.
 *
 * @return 1 when input bytes are ready, 0 when no waiting event produced any, -1 on error.
 */
static cli_transport_ret_t serial_console_available(void) {
  cli_transport_ret_t result = 0;
  bool more = true;

  while (result == 0 && more) {
    DWORD waiting = 0U;

    if (GetNumberOfConsoleInputEvents(s_serial_in, &waiting) == FALSE) {
      result = -1;
    } else if (waiting == 0U) {
      more = false;
    } else {
      INPUT_RECORD rec;
      DWORD got = 0U;

      if (ReadConsoleInputA(s_serial_in, &rec, 1U, &got) == FALSE) {
        result = -1;
      } else if (got == 1U && rec.EventType == KEY_EVENT) {
        serial_translate_key(&rec.Event.KeyEvent);
        result = (s_serial_key_len > 0U) ? 1 : 0;
      } else {
        /* Not a key (focus, mouse, a resize): no input. */
      }
    }
  }

  return result;
}

/**
 * @brief Queue a translated key sequence for @c cli_serial_transport_read().
 *
 * @param[in] seq  NUL-terminated byte sequence (at most 4 bytes).
 */
static void serial_key_set(const char *seq) {
  uint8_t n = 0U;

  while (seq[n] != '\0' && n < (uint8_t)sizeof(s_serial_key_buf)) {
    s_serial_key_buf[n] = (uint8_t)seq[n];
    n++;
  }
  s_serial_key_len = n;
  s_serial_key_idx = 0U;
}

/**
 * @brief Translate one console key event into CLI input bytes.
 *
 * Keys are told apart by their virtual-key code, so a character is never taken for a key
 * (0xE0 is 'à' in code page 1252, 'α' in 437). The arrows, Home, End and Delete become the
 * VT100 sequences the CLI engine understands; a key that types a character (in the
 * console's input code page) or a control code (Enter, Tab, Backspace, Esc, Ctrl+letter)
 * gives that byte; other keys (function keys, Shift, ...) and key releases give none. A
 * key held down is delivered as often as the console repeated it.
 *
 * @param[in] key  Key event read from the console.
 */
static void serial_translate_key(const KEY_EVENT_RECORD *key) {
  s_serial_key_len = 0U;
  s_serial_key_idx = 0U;
  s_serial_key_repeat = 0U;

  if (key->bKeyDown != FALSE) {
    switch (key->wVirtualKeyCode) {
    case VK_UP:
      serial_key_set("\x1b[A");
      break;
    case VK_DOWN:
      serial_key_set("\x1b[B");
      break;
    case VK_RIGHT:
      serial_key_set("\x1b[C");
      break;
    case VK_LEFT:
      serial_key_set("\x1b[D");
      break;
    case VK_HOME:
      serial_key_set("\x1b[H");
      break;
    case VK_END:
      serial_key_set("\x1b[F");
      break;
    case VK_DELETE: /* not Ctrl+D, which quits on an empty line */
      serial_key_set("\x1b[3~");
      break;
    default:
      if (key->uChar.AsciiChar != '\0') {
        s_serial_key_buf[0] = (uint8_t)key->uChar.AsciiChar;
        s_serial_key_len = 1U;
      }
      break;
    }
    if (s_serial_key_len > 0U && key->wRepeatCount > 1U) {
      s_serial_key_repeat = (WORD)(key->wRepeatCount - 1U);
    }
  }
}
#endif

#if ENV_IS_UNIX_LIKE
/**
 * @brief Report whether a POSIX file descriptor has input ready, waiting up to
 *        @p timeout_ms for it.
 *
 * @param[in] fd          File descriptor.
 * @param[in] timeout_ms  Longest wait; 0 does not wait.
 *
 * @return 1 if readable, 0 if not (or interrupted by a signal), -1 on error.
 */
static cli_transport_ret_t fd_available_raw(int32_t fd, uint32_t timeout_ms) {
  /* poll() works for any descriptor number; select() is undefined at fd >= FD_SETSIZE. */
  struct pollfd pfd;
  int timeout = (timeout_ms > (uint32_t)INT_MAX) ? INT_MAX : (int)timeout_ms;

  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;

  int ready = poll(&pfd, 1, timeout);
  if (ready < 0 && errno == EINTR) {
    ready = 0;
  } else if (ready > 0 && (pfd.revents & POLLNVAL) != 0) {
    ready = -1;
  } else {
    /* POLLIN or POLLHUP: read() reports data or end of file. */
  }
  return (ready > 0) ? 1 : (ready < 0 ? -1 : 0);
}

/**
 * @brief Read one byte from a POSIX file descriptor.
 *
 * @param[in] fd  File descriptor.
 *
 * @return Byte value 0..255, or @c -1 on error / end of file.
 */
static cli_transport_ret_t fd_read_raw(int32_t fd) {
  uint8_t byte;
  ssize_t rc;
  do {
    rc = read(fd, &byte, 1);
  } while (rc < 0 && errno == EINTR);
  return (rc == 1) ? byte : -1;
}

/**
 * @brief Write a whole buffer to a POSIX file descriptor.
 *
 * Partial writes and signal interruptions are retried.
 *
 * @param[in] fd   File descriptor.
 * @param[in] buf  Source buffer.
 * @param[in] len  Number of bytes to write.
 *
 * @return @p len on success, or @c -1 on error.
 */
static cli_transport_ret_t fd_write_raw(int32_t fd, const uint8_t *buf,
                                        cli_transport_buflen_t len) {
  size_t sent = 0;
  cli_transport_ret_t result = (cli_transport_ret_t)len;

  while (sent < (size_t)len && result >= 0) {
    ssize_t n = write(fd, &buf[sent], (size_t)len - sent);
    if (n > 0) {
      sent += (size_t)n;
    } else if (n < 0 && errno == EINTR) {
      /* Interrupted by a signal: write the rest again. */
    } else {
      result = -1;
    }
  }

  return result;
}

/**
 * @brief Transport available callback for the POSIX fd transport.
 *
 * @param[in] ctx  Internal fd-pair context.
 *
 * @return 1 if a byte can be read, 0 if not, or @c -1 on error.
 */
static cli_transport_ret_t cli_fd_transport_available(void *ctx) {
  const cli_fd_ctx_struct_t *fds = (const cli_fd_ctx_struct_t *)ctx;
  return fd_available_raw(fds->read_fd, 0U);
}

/**
 * @brief Transport read callback for the POSIX fd transport.
 *
 * @param[in] ctx  Internal fd-pair context.
 *
 * @return Byte value 0..255, or @c -1 on error / end of file.
 */
static cli_transport_ret_t cli_fd_transport_read(void *ctx) {
  const cli_fd_ctx_struct_t *fds = (const cli_fd_ctx_struct_t *)ctx;
  return fd_read_raw(fds->read_fd);
}

/**
 * @brief Transport write callback for the POSIX fd transport.
 *
 * @param[in] ctx  Internal fd-pair context.
 * @param[in] buf  Source buffer.
 * @param[in] len  Number of bytes to write.
 *
 * @return Bytes written, or @c -1 on error.
 */
static cli_transport_ret_t cli_fd_transport_write(void *ctx, const uint8_t *buf,
                                                  cli_transport_buflen_t len) {
  const cli_fd_ctx_struct_t *fds = (const cli_fd_ctx_struct_t *)ctx;
  return fd_write_raw(fds->write_fd, buf, len);
}
#endif

/*################################### END OF FILE ######################################*/
