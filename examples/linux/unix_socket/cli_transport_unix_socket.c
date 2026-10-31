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
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
#ifdef MSG_NOSIGNAL
#define UNIX_SEND_FLAGS MSG_NOSIGNAL /**< Report EPIPE instead of raising SIGPIPE. */
#else
#define UNIX_SEND_FLAGS 0
#endif
#endif

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
static bool cli_unix_remove_stale(const char *path);
#endif

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
int32_t cli_unix_listen(const char *path) {
  struct sockaddr_un addr;
  int32_t fd = CLI_UNIX_SOCK_ERR;

  if (path == NULL || strlen(path) >= sizeof(addr.sun_path)) {
    errno = ENAMETOOLONG;
  } else if (cli_unix_remove_stale(path)) {
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
      memset(&addr, 0, sizeof(addr));
      addr.sun_family = AF_UNIX;
      memcpy(addr.sun_path, path, strlen(path) + 1U);
      if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 1) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        fd = CLI_UNIX_SOCK_ERR;
      }
    }
  } else {
    /* errno set by cli_unix_remove_stale(). */
  }

  return fd;
}

int32_t cli_unix_accept(int32_t listen_fd) {
  int32_t fd = accept(listen_fd, NULL, NULL);

  if (fd >= 0) {
    struct timeval tv;
    tv.tv_sec = CLI_UNIX_SEND_TIMEOUT_MS / 1000;
    tv.tv_usec = (CLI_UNIX_SEND_TIMEOUT_MS % 1000) * 1000;
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
    int32_t nosig = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif
  }

  return fd;
}

int32_t cli_unix_connect(const char *path) {
  struct sockaddr_un addr;
  int32_t fd = CLI_UNIX_SOCK_ERR;

  if (path == NULL || strlen(path) >= sizeof(addr.sun_path)) {
    errno = ENAMETOOLONG;
  } else {
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
      memset(&addr, 0, sizeof(addr));
      addr.sun_family = AF_UNIX;
      memcpy(addr.sun_path, path, strlen(path) + 1U);
      if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        fd = CLI_UNIX_SOCK_ERR;
      }
    }
  }

  return fd;
}

void cli_unix_socket_init(cli_unix_socket_ctx_struct_t *ctx, cli_transport_struct_t *transport,
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
/**
 * @brief Make @p path free for a new listening socket.
 *
 * A leftover socket file from a crashed server is removed. Anything else is
 * left alone: a non-socket file (e.g. a mistyped path) is never deleted, and a
 * socket that a server is still listening on is in use.
 *
 * The socket is probed with a non-blocking connect: only a refused connection
 * (nobody listening) marks it stale. A full backlog (@c EAGAIN) means a busy
 * server, and any other failure is reported rather than guessed at.
 *
 * @param[in] path  Socket path.
 *
 * @return @c true if @p path does not exist (any more); @c false with @c errno set
 *         (@c ENOTSOCK, @c EADDRINUSE or the probe's error) otherwise.
 */
static bool cli_unix_remove_stale(const char *path) {
  struct stat st;
  bool free_path = true;

  if (lstat(path, &st) == 0) {
    if (!S_ISSOCK(st.st_mode)) {
      errno = ENOTSOCK;
      free_path = false;
    } else {
      struct sockaddr_un addr;
      int32_t probe = socket(AF_UNIX, SOCK_STREAM, 0);

      free_path = false;
      if (probe >= 0) {
        int flags = fcntl(probe, F_GETFL, 0);
        int rc = -1;

        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        memcpy(addr.sun_path, path, strlen(path) + 1U);
        if (flags >= 0 && fcntl(probe, F_SETFL, flags | O_NONBLOCK) == 0) {
          rc = connect(probe, (struct sockaddr *)&addr, sizeof(addr));
        }
        int saved = errno;
        close(probe);

        if (rc == 0 || saved == EAGAIN || saved == EINPROGRESS) {
          errno = EADDRINUSE; /* a server is listening (possibly busy) */
        } else if (saved == ECONNREFUSED) {
          free_path = (unlink(path) == 0 || errno == ENOENT);
        } else {
          errno = saved;
        }
      }
    }
  }

  return free_path;
}

/* Transport Callbacks ------------------------------------------------------------------*/

/**
 * @brief Report whether at least one byte can be read without blocking.
 *
 * @param[in] raw  UNIX socket transport context.
 *
 * @return 1 if readable, 0 if not (or interrupted by a signal), negative on error.
 */
static cli_transport_ret_t cli_unix_socket_transport_available(void *raw) {
  cli_unix_socket_ctx_struct_t *ctx = (cli_unix_socket_ctx_struct_t *)raw;
  struct pollfd pfd;

  /* poll() works for any descriptor number; select() is undefined at fd >= FD_SETSIZE. */
  pfd.fd = ctx->fd;
  pfd.events = POLLIN;
  pfd.revents = 0;

  int ready = poll(&pfd, 1, 0);
  if (ready < 0 && errno == EINTR) {
    ready = 0;
  } else if (ready > 0 && (pfd.revents & POLLNVAL) != 0) {
    ready = -1;
  } else {
    /* POLLIN or POLLHUP: read() reports data or the disconnect. */
  }
  return (ready > 0) ? 1 : (ready < 0 ? CLI_UNIX_SOCK_ERR : 0);
}

/**
 * @brief Read one byte from a connected UNIX socket.
 *
 * @param[in] raw  UNIX socket transport context.
 *
 * @return The byte (0..255), or a negative value on error or disconnect.
 */
static cli_transport_ret_t cli_unix_socket_transport_read(void *raw) {
  cli_unix_socket_ctx_struct_t *ctx = (cli_unix_socket_ctx_struct_t *)raw;
  uint8_t byte;
  ssize_t rc;

  do {
    rc = read(ctx->fd, &byte, 1);
  } while (rc < 0 && errno == EINTR);

  return (rc == 1) ? (cli_transport_ret_t)byte : CLI_UNIX_SOCK_ERR;
}

/**
 * @brief Write a whole buffer to a connected UNIX socket.
 *
 * Retries partial writes and @c EINTR; a closed peer yields an error instead
 * of @c SIGPIPE, and a send that blocks for @c CLI_UNIX_SEND_TIMEOUT_MS (client
 * not reading) fails with @c EAGAIN, which ends the session.
 *
 * @param[in] raw  UNIX socket transport context.
 * @param[in] buf  Source buffer.
 * @param[in] len  Number of bytes to write.
 *
 * @return @p len on success, or a negative value on error.
 */
static cli_transport_ret_t cli_unix_socket_transport_write(void *raw, const uint8_t *buf,
                                                           cli_transport_buflen_t len) {
  cli_unix_socket_ctx_struct_t *ctx = (cli_unix_socket_ctx_struct_t *)raw;
  size_t sent = 0;

  while (sent < (size_t)len) {
    ssize_t n = send(ctx->fd, &buf[sent], (size_t)len - sent, UNIX_SEND_FLAGS);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return CLI_UNIX_SOCK_ERR;
    }
    sent += (size_t)n;
  }

  return (cli_transport_ret_t)len;
}
#endif

/*################################### END OF FILE ######################################*/
