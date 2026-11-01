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

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* struct ucred / SO_PEERCRED; must precede every system header */
#endif

#include "cli_transport_unix_socket.h"

#if ENV_IS_UNIX_LIKE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
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
static bool cli_unix_lock_path(const char *path, int32_t *lock_fd);
static bool cli_unix_remove_stale(const char *path);
#endif

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
int32_t cli_unix_listen(const char *path, cli_unix_listener_struct_t *listener) {
  struct sockaddr_un addr;
  int32_t rc = CLI_UNIX_SOCK_ERR;

  listener->fd = CLI_UNIX_SOCK_ERR;
  listener->lock_fd = CLI_UNIX_SOCK_ERR;

  if (path == NULL || strlen(path) >= sizeof(addr.sun_path)) {
    errno = ENAMETOOLONG;
  } else if (cli_unix_lock_path(path, &listener->lock_fd) && cli_unix_remove_stale(path)) {
    int32_t fd = socket(AF_UNIX, SOCK_STREAM, 0);
    bool bound = false;

    if (fd >= 0) {
      memset(&addr, 0, sizeof(addr));
      addr.sun_family = AF_UNIX;
      memcpy(addr.sun_path, path, strlen(path) + 1U);
      bound = (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
      /* Owner-only before listen(): other users can neither connect to the CLI nor read
       * what it sends. */
      if (bound && chmod(path, 0600) == 0 && listen(fd, 1) == 0) {
        listener->fd = fd;
        rc = CLI_UNIX_SOCK_OK;
      } else {
        int saved = errno;
        close(fd);
        if (bound) {
          (void)unlink(path);
        }
        errno = saved;
      }
    }
  } else {
    /* errno set by cli_unix_lock_path() / cli_unix_remove_stale(). */
  }

  if (rc != CLI_UNIX_SOCK_OK && listener->lock_fd >= 0) {
    int saved = errno;
    close(listener->lock_fd);
    listener->lock_fd = CLI_UNIX_SOCK_ERR;
    errno = saved;
  }

  return rc;
}

void cli_unix_listener_close(cli_unix_listener_struct_t *listener, const char *path) {
  if (listener->fd >= 0) {
    close(listener->fd);
    listener->fd = CLI_UNIX_SOCK_ERR;
    (void)unlink(path); /* while the lock is still held: never another server's socket */
  }
  if (listener->lock_fd >= 0) {
    close(listener->lock_fd); /* releases the lock */
    listener->lock_fd = CLI_UNIX_SOCK_ERR;
  }
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

bool cli_unix_peer_is_self(int32_t fd) {
  bool same = false;

#if defined(__linux__)
  struct ucred cred;
  socklen_t len = (socklen_t)sizeof(cred);

  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
    same = (cred.uid == geteuid());
  }
#else
  /* BSDs (incl. OpenBSD, whose SO_PEERCRED uses another struct) and macOS. */
  uid_t uid;
  gid_t gid;

  if (getpeereid(fd, &uid, &gid) == 0) {
    same = (uid == geteuid());
  }
#endif

  return same;
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
#endif

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

#if ENV_IS_UNIX_LIKE
/**
 * @brief Take the server lock for @p path: an exclusive lock on "<path>.lock".
 *
 * Held for as long as the server listens, so only one server uses a path at a time and
 * a socket file found at @p path while holding it is a leftover that may be removed.
 * The lock file itself stays on disk (removing it would let two servers lock different
 * files of the same name).
 *
 * @param[in]  path     Socket path.
 * @param[out] lock_fd  Receives the descriptor that holds the lock.
 *
 * @return @c true on success; @c false with @c errno = @c EADDRINUSE when another server
 *         holds it, or the cause of the failure.
 */
static bool cli_unix_lock_path(const char *path, int32_t *lock_fd) {
  char lock_path[sizeof(((struct sockaddr_un *)0)->sun_path) + 5U];
  int n = snprintf(lock_path, sizeof(lock_path), "%s.lock", path);
  int32_t fd = -1;
  bool ok = (n > 0 && (size_t)n < sizeof(lock_path));

  if (ok) {
    fd = open(lock_path, O_RDWR | O_CREAT, 0600);
    ok = (fd >= 0);
  }
  if (ok) {
    struct flock fl;

    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK; /* whole file */
    fl.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLK, &fl) != 0) {
      int saved = errno;
      close(fd);
      errno = (saved == EACCES || saved == EAGAIN) ? EADDRINUSE : saved;
      ok = false;
    }
  }
  if (ok) {
    *lock_fd = fd;
  }

  return ok;
}

/**
 * @brief Make @p path free for a new listening socket (called with the server lock held).
 *
 * A socket file there is left over from a server that is gone, since no other server
 * holds the lock: it is removed. Anything else (e.g. a mistyped path naming a regular
 * file) is never deleted.
 *
 * @param[in] path  Socket path.
 *
 * @return @c true if @p path does not exist (any more); @c false with @c errno set
 *         (@c ENOTSOCK, or the error of @c unlink()) otherwise.
 */
static bool cli_unix_remove_stale(const char *path) {
  struct stat st;
  bool free_path = true;

  if (lstat(path, &st) == 0) {
    if (!S_ISSOCK(st.st_mode)) {
      errno = ENOTSOCK;
      free_path = false;
    } else {
      free_path = (unlink(path) == 0 || errno == ENOENT);
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
