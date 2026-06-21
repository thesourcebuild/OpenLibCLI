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
#include <sys/select.h>
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

#if ENV_IS_UNIX_LIKE
/** @brief Internal fd-pair slot used by the POSIX fd transport callbacks. */
typedef struct {
  int32_t rfd; /**< Read file descriptor. */
  int32_t wfd; /**< Write file descriptor. */
  bool in_use; /**< True when this slot is occupied. */
} cli_fd_pair_struct_t;
#endif

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

#if ENV_IS_UNIX_LIKE
/** @brief Static pool of fd-pair contexts, one slot per CLI instance. */
static cli_fd_pair_struct_t s_fd_pool[1];
#endif

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

static cli_transport_ret_t cli_serial_transport_available(void *raw_ctx);
static cli_transport_ret_t cli_serial_transport_read(void *raw_ctx);
static cli_transport_ret_t cli_serial_transport_write(void *raw_ctx, const uint8_t *buf,
                                                      cli_transport_buflen_t len);
static cli_transport_ret_t cli_serial_transport_flush(void *raw_ctx);

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
  int16_t slot = -1;

  for (int16_t i = 0; i < 1; i++) {
    if (!s_fd_pool[i].in_use) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    transport->available = NULL;
    transport->read = NULL;
    transport->write = NULL;
    transport->flush = NULL;
    transport->ctx = NULL;
    transport->kind = CLI_TRANSPORT_SERIAL;
  } else {
    s_fd_pool[slot].rfd = read_fd;
    s_fd_pool[slot].wfd = write_fd;
    s_fd_pool[slot].in_use = true;

    ctx->read_fd = read_fd;
    ctx->write_fd = write_fd;

    transport->available = cli_fd_transport_available;
    transport->read = cli_fd_transport_read;
    transport->write = cli_fd_transport_write;
    transport->flush = NULL;
    transport->ctx = &s_fd_pool[slot];
    transport->kind = CLI_TRANSPORT_SERIAL;
  }
}

void cli_fd_deinit(cli_transport_struct_t *transport) {
  if (transport && transport->ctx) {
    cli_fd_pair_struct_t *slot = (cli_fd_pair_struct_t *)transport->ctx;
    slot->in_use = false;
    slot->rfd = -1;
    slot->wfd = -1;

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
  if (s_serial_key_idx < s_serial_key_len) {
    result = 1;
  } else {
    result = (_kbhit() != 0) ? 1 : 0;
  }
#elif ENV_IS_UNIX_LIKE
  fd_set rfds;
  struct timeval tv;

  FD_ZERO(&rfds);
  FD_SET(STDIN_FILENO, &rfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  int ready = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
  result = (ready > 0) ? 1 : (ready < 0 ? -1 : 0);
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
    if (_kbhit() == 0) {
      result = -1;
    } else {

      int ch = _getch();
      if (ch == 0xE0 || ch == 0x00) {
        int ext = _getch();
        switch (ext) {
        case 0x48: /* Up */
          s_serial_key_buf[0] = 0x1B;
          s_serial_key_buf[1] = '[';
          s_serial_key_buf[2] = 'A';
          s_serial_key_len = 3;
          break;
        case 0x50: /* Down */
          s_serial_key_buf[0] = 0x1B;
          s_serial_key_buf[1] = '[';
          s_serial_key_buf[2] = 'B';
          s_serial_key_len = 3;
          break;
        case 0x4D: /* Right */
          s_serial_key_buf[0] = 0x1B;
          s_serial_key_buf[1] = '[';
          s_serial_key_buf[2] = 'C';
          s_serial_key_len = 3;
          break;
        case 0x4B: /* Left */
          s_serial_key_buf[0] = 0x1B;
          s_serial_key_buf[1] = '[';
          s_serial_key_buf[2] = 'D';
          s_serial_key_len = 3;
          break;
        case 0x47: /* Home */
          s_serial_key_buf[0] = 0x01;
          s_serial_key_len = 1;
          break;   /* Ctrl+A */
        case 0x4F: /* End */
          s_serial_key_buf[0] = 0x05;
          s_serial_key_len = 1;
          break;   /* Ctrl+E */
        case 0x53: /* Delete */
          s_serial_key_buf[0] = 0x04;
          s_serial_key_len = 1;
          break; /* Ctrl+D (delete) */
        default:
          s_serial_key_buf[0] = (uint8_t)ext;
          s_serial_key_len = 1;
          break;
        }
      } else {
        s_serial_key_buf[0] = (uint8_t)ch;
        s_serial_key_len = 1;
      }

      s_serial_key_idx = 0;
      result = s_serial_key_buf[s_serial_key_idx];
      s_serial_key_idx++;
    }
  } else {
    result = s_serial_key_buf[s_serial_key_idx];
    s_serial_key_idx++;
  }
#elif ENV_IS_UNIX_LIKE
  uint8_t byte;
  cli_transport_ret_t rc = read(STDIN_FILENO, &byte, 1);
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
  return write(STDOUT_FILENO, buf, len);
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

#if ENV_IS_UNIX_LIKE
/**
 * @brief Read directly from a POSIX file descriptor.
 *
 * @param[in]  fd   File descriptor.
 * @param[out] buf  Destination buffer.
 * @param[in]  len  Maximum number of bytes to read.
 *
 * @return Bytes read, or @c -1 on error.
 */
static cli_transport_ret_t fd_available_raw(int32_t fd) {
  fd_set rfds;
  struct timeval tv;

  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  int ready = select(fd + 1, &rfds, NULL, NULL, &tv);
  return (ready > 0) ? 1 : (ready < 0 ? -1 : 0);
}

static cli_transport_ret_t fd_read_raw(int32_t fd) {
  uint8_t byte;
  cli_transport_ret_t rc = read(fd, &byte, 1);
  return (rc == 1) ? byte : -1;
}

/**
 * @brief Write directly to a POSIX file descriptor.
 *
 * @param[in] fd   File descriptor.
 * @param[in] buf  Source buffer.
 * @param[in] len  Number of bytes to write.
 *
 * @return Bytes written, or @c -1 on error.
 */
static cli_transport_ret_t fd_write_raw(int32_t fd, const uint8_t *buf,
                                        cli_transport_buflen_t len) {
  return write(fd, buf, len);
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
  cli_fd_pair_struct_t *pair = (cli_fd_pair_struct_t *)ctx;
  return fd_available_raw(pair->rfd);
}

static cli_transport_ret_t cli_fd_transport_read(void *ctx) {
  cli_fd_pair_struct_t *pair = (cli_fd_pair_struct_t *)ctx;
  return fd_read_raw(pair->rfd);
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
  cli_fd_pair_struct_t *pair = (cli_fd_pair_struct_t *)ctx;
  return fd_write_raw(pair->wfd, buf, len);
}
#endif

/*################################### END OF FILE ######################################*/
