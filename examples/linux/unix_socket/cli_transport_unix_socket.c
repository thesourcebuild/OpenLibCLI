/**
 * @file cli_transport_unix_socket.c
 * @brief UNIX domain socket transport implementation.
 *
 * Implements @c cli_transport_struct_t read and write callbacks for @c AF_UNIX
 * stream sockets on POSIX platforms.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "cli_transport_unix_socket.h"

#if ENV_IS_UNIX_LIKE
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
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

/* No private file-scope variables. */

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
static cli_transport_ret_t cli_unix_socket_transport_available(void *raw);
static cli_transport_ret_t cli_unix_socket_transport_read(void *raw);
static cli_transport_ret_t cli_unix_socket_transport_write(void *raw, const uint8_t *buf,
                                                           cli_transport_buflen_t len);
#endif

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
int32_t cli_unix_listen(const char *path) {
  struct sockaddr_un addr;
  int32_t fd = CLI_UNIX_SOCK_ERR;

  if (strlen(path) < sizeof(addr.sun_path)) {
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
      unlink(path);
      memset(&addr, 0, sizeof(addr));
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
      if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 1) < 0) {
        close(fd);
        fd = CLI_UNIX_SOCK_ERR;
      }
    }
  }

  return fd;
}

int32_t cli_unix_accept(int32_t listen_fd) {
  return accept(listen_fd, NULL, NULL);
}

int32_t cli_unix_connect(const char *path) {
  struct sockaddr_un addr;
  int32_t fd = CLI_UNIX_SOCK_ERR;

  if (strlen(path) < sizeof(addr.sun_path)) {
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
      memset(&addr, 0, sizeof(addr));
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
      if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        fd = CLI_UNIX_SOCK_ERR;
      }
    }
  }

  return fd;
}

void cli_unix_socket_init(cli_unix_socket_ctx_t *ctx, cli_transport_struct_t *transport,
                          int32_t fd) {
  ctx->fd = fd;
  transport->available = cli_unix_socket_transport_available;
  transport->read = cli_unix_socket_transport_read;
  transport->write = cli_unix_socket_transport_write;
  transport->flush = NULL;
  transport->ctx = ctx;
  transport->kind = CLI_TRANSPORT_UNIX_SOCKET;
}

void cli_unix_close(int32_t fd) {
  if (fd >= 0) {
    close(fd);
  }
}

void cli_unix_unlink(const char *path) {
  unlink(path);
}
#endif

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
/* Transport Callbacks ------------------------------------------------------------------*/

/**
 * @brief Read from a connected UNIX socket.
 *
 * @param[in]  raw  UNIX socket transport context.
 * @param[out] buf  Destination buffer.
 * @param[in]  len  Maximum bytes to read.
 *
 * @return Bytes read on success, or a negative value on error or disconnect.
 */
static cli_transport_ret_t cli_unix_socket_transport_available(void *raw) {
  cli_unix_socket_ctx_t *ctx = (cli_unix_socket_ctx_t *)raw;
  fd_set rfds;
  struct timeval tv;

  FD_ZERO(&rfds);
  FD_SET(ctx->fd, &rfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  int ready = select(ctx->fd + 1, &rfds, NULL, NULL, &tv);
  return (ready > 0) ? 1 : (ready < 0 ? CLI_UNIX_SOCK_ERR : 0);
}

static cli_transport_ret_t cli_unix_socket_transport_read(void *raw) {
  cli_unix_socket_ctx_t *ctx = (cli_unix_socket_ctx_t *)raw;
  uint8_t byte;
  cli_transport_ret_t rc = read(ctx->fd, &byte, 1);
  return (rc == 1) ? byte : CLI_UNIX_SOCK_ERR;
}

/**
 * @brief Write to a connected UNIX socket.
 *
 * @param[in] raw  UNIX socket transport context.
 * @param[in] buf  Source buffer.
 * @param[in] len  Number of bytes to write.
 *
 * @return Bytes written on success, or a negative value on error.
 */
static cli_transport_ret_t cli_unix_socket_transport_write(void *raw, const uint8_t *buf,
                                                           cli_transport_buflen_t len) {
  cli_unix_socket_ctx_t *ctx = (cli_unix_socket_ctx_t *)raw;
  return write(ctx->fd, buf, len);
}
#endif

/*################################### END OF FILE ######################################*/
