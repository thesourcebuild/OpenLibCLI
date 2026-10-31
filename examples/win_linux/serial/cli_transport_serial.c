/**
 * @file cli_transport_serial.c
 * @brief Host serial stdin/stdout transport implementation.
 *
 * Implements the host stdin/stdout serial transport and the POSIX
 * file-descriptor transport used for host stream endpoints.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "cli_transport_serial.h"

#if ENV_IS_WINDOWS
#include <conio.h>
#elif ENV_IS_UNIX_LIKE
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#endif

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
 * External Data Variables
 *=======================================================================================*/

/* No externally visible data variables. */

/*=======================================================================================
 * Private Variables
 *=======================================================================================*/

#if ENV_IS_WINDOWS
static uint8_t s_serial_key_buf[4];
static uint8_t s_serial_key_len = 0;
static uint8_t s_serial_key_idx = 0;
#endif


/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

static cli_transport_ret_t cli_serial_transport_available(void *raw_ctx);
static cli_transport_ret_t cli_serial_transport_read(void *raw_ctx);
static cli_transport_ret_t cli_serial_transport_write(void *raw_ctx, const uint8_t *buf,
                                                      cli_transport_buflen_t len);
static cli_transport_ret_t cli_serial_transport_flush(void *raw_ctx);

#if ENV_IS_WINDOWS
static void serial_key_set(const char *seq);
static void serial_translate_key(void);
#endif

#if ENV_IS_UNIX_LIKE
static cli_transport_ret_t fd_available_raw(int32_t fd);
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
  transport->available = cli_serial_transport_available;
  transport->read = cli_serial_transport_read;
  transport->write = cli_serial_transport_write;
  transport->flush = cli_serial_transport_flush;
  transport->ctx = NULL;
  transport->kind = CLI_TRANSPORT_SERIAL;
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

static cli_transport_ret_t cli_serial_transport_available(void *raw_ctx) {
  cli_transport_ret_t result;

  (void)raw_ctx;

#if ENV_IS_WINDOWS
  /* Translate pending keystrokes until one produces input (unknown keys produce none). */
  while (s_serial_key_idx >= s_serial_key_len && _kbhit() != 0) {
    serial_translate_key();
  }
  result = (s_serial_key_idx < s_serial_key_len) ? 1 : 0;
#elif ENV_IS_UNIX_LIKE
  result = fd_available_raw(STDIN_FILENO);
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
 * @return Byte value 0..255, or @c -1 on error.
 */
static cli_transport_ret_t cli_serial_transport_read(void *raw_ctx) {
  cli_transport_ret_t result;

  (void)raw_ctx;

#if ENV_IS_WINDOWS
  if (s_serial_key_idx >= s_serial_key_len) {
    (void)cli_serial_transport_available(raw_ctx);
  }
  if (s_serial_key_idx < s_serial_key_len) {
    result = s_serial_key_buf[s_serial_key_idx];
    s_serial_key_idx++;
  } else {
    result = -1;
  }
#elif ENV_IS_UNIX_LIKE
  uint8_t byte;
  ssize_t rc;
  do {
    rc = read(STDIN_FILENO, &byte, 1);
  } while (rc < 0 && errno == EINTR);
  result = (rc == 1) ? byte : -1;
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
  (void)raw_ctx;

#if ENV_IS_WINDOWS
  for (cli_transport_buflen_t i = 0; i < len; i++) {
    _putch((int32_t)buf[i]);
  }
  return len;
#elif ENV_IS_UNIX_LIKE
  return fd_write_raw(STDOUT_FILENO, buf, len);
#else
  (void)buf;
  (void)len;
  return -1;
#endif
}

/**
 * @brief Flush the host serial output stream when supported.
 *
 * @param[in] raw_ctx  Unused.
 *
 * @return Always @c 0.
 */
static cli_transport_ret_t cli_serial_transport_flush(void *raw_ctx) {
  (void)raw_ctx;

#if ENV_IS_UNIX_LIKE
  fsync(STDOUT_FILENO);
#endif

  return 0;
}

#if ENV_IS_WINDOWS
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
 * @brief Read one console keystroke and translate it into CLI input bytes.
 *
 * Extended keys (arrows, Home, End, Delete) become the VT100 sequences the CLI
 * engine understands; other extended keys (F1..F12, Insert, PgUp, ...) produce
 * no input. @c _getch() reports an extended key as a 0x00 / 0xE0 prefix with the
 * scan code already queued behind it; a 0xE0 with nothing queued is an ordinary
 * character of the console code page (e.g. 'α' in CP437), not a prefix.
 */
static void serial_translate_key(void) {
  int ch = _getch();

  s_serial_key_len = 0U;
  s_serial_key_idx = 0U;

  if (ch == 0x00 || (ch == 0xE0 && _kbhit() != 0)) {
    switch (_getch()) {
    case 0x48: /* Up */
      serial_key_set("\x1b[A");
      break;
    case 0x50: /* Down */
      serial_key_set("\x1b[B");
      break;
    case 0x4D: /* Right */
      serial_key_set("\x1b[C");
      break;
    case 0x4B: /* Left */
      serial_key_set("\x1b[D");
      break;
    case 0x47: /* Home */
      serial_key_set("\x1b[H");
      break;
    case 0x4F: /* End */
      serial_key_set("\x1b[F");
      break;
    case 0x53: /* Delete (not Ctrl+D, which quits on an empty line) */
      serial_key_set("\x1b[3~");
      break;
    default:
      /* Function / navigation key without a CLI meaning: no input. */
      break;
    }
  } else {
    s_serial_key_buf[0] = (uint8_t)ch;
    s_serial_key_len = 1U;
  }
}
#endif

#if ENV_IS_UNIX_LIKE
/**
 * @brief Report whether a POSIX file descriptor has input ready.
 *
 * @param[in] fd  File descriptor.
 *
 * @return 1 if readable, 0 if not (or interrupted by a signal), -1 on error.
 */
static cli_transport_ret_t fd_available_raw(int32_t fd) {
  /* poll() works for any descriptor number; select() is undefined at fd >= FD_SETSIZE. */
  struct pollfd pfd;

  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;

  int ready = poll(&pfd, 1, 0);
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

  while (sent < (size_t)len) {
    ssize_t n = write(fd, &buf[sent], (size_t)len - sent);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      return -1;
    }
    sent += (size_t)n;
  }

  return (cli_transport_ret_t)len;
}

/**
 * @brief Transport read callback for the POSIX fd transport.
 *
 * @param[in]  ctx  Internal fd-pair context.
 * @param[out] buf  Destination buffer.
 * @param[in]  len  Maximum number of bytes to read.
 *
 * @return Bytes read, or @c -1 on error.
 */
static cli_transport_ret_t cli_fd_transport_available(void *ctx) {
  const cli_fd_ctx_struct_t *fds = (const cli_fd_ctx_struct_t *)ctx;
  return fd_available_raw(fds->read_fd);
}

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
