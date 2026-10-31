/**
 * @file cli_transport_pipe.c
 * @brief Named and anonymous pipe transport implementation.
 *
 * Implements @c cli_transport_struct_t read and write callbacks for IPC pipes on
 * both Windows and POSIX platforms.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/

#include "cli_transport_pipe.h"

#if defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__) || defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <stdio.h>
#include <string.h>
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/** @brief Size of the internal Windows named-pipe path buffer. */
#define PIPE_PATH_MAX 256
#else
/** @brief Maximum FIFO path length for POSIX named pipes. */
#define FIFO_PATH_MAX 512
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

#if ENV_IS_WINDOWS
static bool build_win_pipe_path(char *buf, int32_t buflen, const char *name);
#else
static bool build_fifo_paths(const char *name, char *in_path, char *out_path, size_t size);
static bool ensure_fifo(const char *path, bool *created);
#endif
static cli_transport_ret_t cli_pipe_transport_available(void *raw);
static cli_transport_ret_t cli_pipe_transport_read(void *raw);
static cli_transport_ret_t cli_pipe_transport_write(void *raw, const uint8_t *buf,
                                                    cli_transport_buflen_t len);

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

#if ENV_IS_WINDOWS
int32_t cli_named_pipe_listen(const char *name, cli_pipe_ctx_struct_t *ctx) {
  char path[PIPE_PATH_MAX];
  int32_t rc = CLI_PIPE_ERR;
  HANDLE handle = INVALID_HANDLE_VALUE;

  if (build_win_pipe_path(path, sizeof(path), name)) {
    handle = CreateNamedPipeA(path, PIPE_ACCESS_DUPLEX,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
                              NULL);
  } else {
    SetLastError(ERROR_FILENAME_EXCED_RANGE); /* a truncated name would be another pipe */
  }
  if (handle != INVALID_HANDLE_VALUE) {
    BOOL connected = FALSE;
    BOOL waiting = TRUE;

    while (waiting) {
      if (ConnectNamedPipe(handle, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
        connected = TRUE;
        waiting = FALSE;
      } else if (GetLastError() == ERROR_NO_DATA) {
        /* A client connected and closed again before we got here: wait for the next. */
        (void)DisconnectNamedPipe(handle);
      } else {
        waiting = FALSE;
      }
    }

    if (connected) {
      ctx->h_read = (void *)handle;
      ctx->h_write = (void *)handle;
      rc = CLI_PIPE_OK;
    } else {
      DWORD error = GetLastError();
      CloseHandle(handle);
      SetLastError(error); /* report the cause, not CloseHandle's result */
    }
  }

  return rc;
}

int32_t cli_named_pipe_connect(const char *name, cli_pipe_ctx_struct_t *ctx) {
  char path[PIPE_PATH_MAX];
  HANDLE handle = INVALID_HANDLE_VALUE;
  int32_t rc = CLI_PIPE_ERR;
  bool trying = build_win_pipe_path(path, sizeof(path), name);
  ULONGLONG deadline = GetTickCount64() + (ULONGLONG)CLI_PIPE_CONNECT_TIMEOUT_MS;

  if (!trying) {
    SetLastError(ERROR_FILENAME_EXCED_RANGE); /* a truncated name would be another pipe */
  }

  /* Documented client pattern: retry while the single instance is busy or not yet
   * re-created, with one overall deadline. */
  while (trying) {
    handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (handle != INVALID_HANDLE_VALUE) {
      trying = false;
    } else {
      DWORD error = GetLastError();
      ULONGLONG now = GetTickCount64();

      if (now >= deadline) {
        trying = false;
      } else if (error == ERROR_PIPE_BUSY) {
        /* Serving another client: wait until the instance is free (or the deadline). */
        (void)WaitNamedPipeA(path, (DWORD)(deadline - now));
      } else if (error == ERROR_FILE_NOT_FOUND) {
        /* Between sessions: the server re-creates the pipe right away. */
        Sleep(50);
      } else {
        trying = false; /* access denied, bad name, ...: retrying cannot help */
      }
    }
  }

  if (handle != INVALID_HANDLE_VALUE) {
    ctx->h_read = (void *)handle;
    ctx->h_write = (void *)handle;
    rc = CLI_PIPE_OK;
  }

  return rc;
}

int32_t cli_anon_pipe_create(cli_pipe_ctx_struct_t *server, cli_pipe_ctx_struct_t *client) {
  HANDLE s2c_r = NULL;
  HANDLE s2c_w = NULL;
  HANDLE c2s_r = NULL;
  HANDLE c2s_w = NULL;
  int32_t rc = CLI_PIPE_ERR;

  if (CreatePipe(&s2c_r, &s2c_w, NULL, 0)) {
    if (CreatePipe(&c2s_r, &c2s_w, NULL, 0)) {
      server->h_read = (void *)c2s_r;
      server->h_write = (void *)s2c_w;
      client->h_read = (void *)s2c_r;
      client->h_write = (void *)c2s_w;
      rc = CLI_PIPE_OK;
    } else {
      CloseHandle(s2c_r);
      CloseHandle(s2c_w);
    }
  }

  return rc;
}

void cli_pipe_init(cli_pipe_ctx_struct_t *ctx, cli_transport_struct_t *transport) {
  transport->available = cli_pipe_transport_available;
  transport->read = cli_pipe_transport_read;
  transport->write = cli_pipe_transport_write;
  transport->flush = NULL;
  transport->ctx = ctx;
  transport->kind = CLI_TRANSPORT_PIPE;
}

void cli_pipe_close(cli_pipe_ctx_struct_t *ctx) {
  if (ctx->h_read != NULL) {
    CloseHandle((HANDLE)ctx->h_read);
    if (ctx->h_write != ctx->h_read) {
      CloseHandle((HANDLE)ctx->h_write);
    }
    ctx->h_read = NULL;
    ctx->h_write = NULL;
  }
}

void cli_named_pipe_disconnect(cli_pipe_ctx_struct_t *ctx) {
  if (ctx->h_read != NULL) {
    /* Documented server shutdown: let the client read everything still buffered (e.g. the
     * goodbye text), then break the connection before the handle goes away. */
    (void)FlushFileBuffers((HANDLE)ctx->h_write);
    (void)DisconnectNamedPipe((HANDLE)ctx->h_read);
    cli_pipe_close(ctx);
  }
}
#else
int32_t cli_named_pipe_listen(const char *name, cli_pipe_ctx_struct_t *ctx) {
  char in_path[FIFO_PATH_MAX];
  char out_path[FIFO_PATH_MAX];
  int32_t rfd = -1;
  int32_t wfd = -1;
  int32_t rc = CLI_PIPE_ERR;
  bool in_created = false;
  bool out_created = false;

  if (build_fifo_paths(name, in_path, out_path, sizeof(in_path)) &&
      ensure_fifo(in_path, &in_created)) {
    if (ensure_fifo(out_path, &out_created)) {
      rfd = open(in_path, O_RDONLY | O_NONBLOCK);
      if (rfd >= 0) {
        wfd = open(out_path, O_WRONLY);
        if (wfd >= 0) {
          int32_t flags = fcntl(rfd, F_GETFL, 0);
          if (flags >= 0) {
            (void)fcntl(rfd, F_SETFL, flags & ~O_NONBLOCK);
          }

          ctx->read_fd = rfd;
          ctx->write_fd = wfd;
          rc = CLI_PIPE_OK;
        }
      }
    }
  }

  if (rc != CLI_PIPE_OK) {
    int saved_errno = errno; /* the cause, for the caller; cleanup must not overwrite it */

    if (rfd >= 0) {
      close(rfd);
    }
    if (wfd >= 0) {
      close(wfd);
    }
    /* Remove only what this call created; pre-existing FIFOs stay in place. */
    if (in_created) {
      unlink(in_path);
    }
    if (out_created) {
      unlink(out_path);
    }
    errno = saved_errno;
  }

  return rc;
}

int32_t cli_named_pipe_connect(const char *name, cli_pipe_ctx_struct_t *ctx) {
  char in_path[FIFO_PATH_MAX];
  char out_path[FIFO_PATH_MAX];
  int32_t rc = CLI_PIPE_ERR;
  int32_t rfd = -1;

  if (build_fifo_paths(name, in_path, out_path, sizeof(in_path))) {
    rfd = open(out_path, O_RDONLY);
  }
  if (rfd >= 0) {
    int32_t wfd = open(in_path, O_WRONLY);
    if (wfd >= 0) {
      ctx->read_fd = rfd;
      ctx->write_fd = wfd;
      rc = CLI_PIPE_OK;
    } else {
      close(rfd);
    }
  }

  return rc;
}

int32_t cli_anon_pipe_create(cli_pipe_ctx_struct_t *server, cli_pipe_ctx_struct_t *client) {
  int32_t s2c[2];
  int32_t c2s[2];
  int32_t rc = CLI_PIPE_ERR;

  if (pipe(s2c) == 0) {
    if (pipe(c2s) == 0) {
      server->read_fd = c2s[0];
      server->write_fd = s2c[1];
      client->read_fd = s2c[0];
      client->write_fd = c2s[1];
      rc = CLI_PIPE_OK;
    } else {
      close(s2c[0]);
      close(s2c[1]);
    }
  }

  return rc;
}

void cli_pipe_init(cli_pipe_ctx_struct_t *ctx, cli_transport_struct_t *transport) {
  transport->available = cli_pipe_transport_available;
  transport->read = cli_pipe_transport_read;
  transport->write = cli_pipe_transport_write;
  transport->flush = NULL;
  transport->ctx = ctx;
  transport->kind = CLI_TRANSPORT_PIPE;
}

void cli_pipe_close(cli_pipe_ctx_struct_t *ctx) {
  if (ctx->read_fd >= 0) {
    close(ctx->read_fd);
    ctx->read_fd = -1;
  }
  if (ctx->write_fd >= 0) {
    close(ctx->write_fd);
    ctx->write_fd = -1;
  }
}

void cli_named_pipe_disconnect(cli_pipe_ctx_struct_t *ctx) {
  /* Closing our <name>.out end first lets the client read everything and then see EOF.
   * The client closes its <name>.out reader before its <name>.in writer (cli_pipe_close()),
   * so once <name>.in reports EOF the client holds neither FIFO any more: the next
   * cli_named_pipe_listen() cannot be satisfied by this client's leftover reader. */
  if (ctx->write_fd >= 0) {
    close(ctx->write_fd);
    ctx->write_fd = -1;
  }
  if (ctx->read_fd >= 0) {
    uint8_t discard[64];
    ssize_t n;

    do {
      n = read(ctx->read_fd, discard, sizeof(discard)); /* input after the session ended */
    } while (n > 0 || (n < 0 && errno == EINTR));
  }
  cli_pipe_close(ctx);
}
#endif

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/* Windows Pipe Helpers -----------------------------------------------------------------*/

/**
 * @brief Build a fully-qualified Windows named-pipe path.
 *
 * @param[out] buf     Destination buffer (always NUL-terminated).
 * @param[in]  buflen  Size of @p buf in bytes.
 * @param[in]  name    User-supplied pipe name, or a full @c \\\\.\\pipe\\... path.
 *
 * @return @c true on success; @c false if the path does not fit (a truncated
 *         name would silently address a different pipe).
 */
static bool build_win_pipe_path(char *buf, int32_t buflen, const char *name) {
  int n;

  if (strncmp(name, "\\\\", 2) == 0) {
    n = cli_snprintf(buf, (size_t)buflen, "%s", name);
  } else {
    n = cli_snprintf(buf, (size_t)buflen, "\\\\.\\pipe\\%s", name);
  }

  return (n >= 0 && n < buflen);
}
#else
/**
 * @brief Build the "<name>.in" / "<name>.out" FIFO paths.
 *
 * @param[in]  name      Base path.
 * @param[out] in_path   Client-to-server FIFO path.
 * @param[out] out_path  Server-to-client FIFO path.
 * @param[in]  size      Size of each output buffer.
 *
 * @return @c true if both paths fit without truncation.
 */
static bool build_fifo_paths(const char *name, char *in_path, char *out_path, size_t size) {
  int n_in = cli_snprintf(in_path, size, "%s.in", name);
  int n_out = cli_snprintf(out_path, size, "%s.out", name);
  bool ok = (n_in >= 0 && (size_t)n_in < size && n_out >= 0 && (size_t)n_out < size);

  if (!ok) {
    errno = ENAMETOOLONG; /* a truncated path would name a different FIFO */
  }
  return ok;
}

/**
 * @brief Create the FIFO @p path, or accept an existing one if it really is a FIFO.
 *
 * @param[in]  path     FIFO path.
 * @param[out] created  Set to @c true when this call created the FIFO.
 *
 * @return @c true if @p path is a usable FIFO; @c false (with @c errno set) otherwise.
 */
static bool ensure_fifo(const char *path, bool *created) {
  bool ok = false;
  struct stat st;

  *created = false;
  if (mkfifo(path, 0666) == 0) {
    *created = true;
    ok = true;
  } else if (errno == EEXIST) {
    /* Never write CLI traffic into (or later delete) a regular file of the same name. */
    if (lstat(path, &st) == 0 && S_ISFIFO(st.st_mode)) {
      ok = true;
    } else {
      errno = EEXIST;
    }
  } else {
    /* mkfifo failed (permissions, missing directory, ...). */
  }

  return ok;
}
#endif

static cli_transport_ret_t cli_pipe_transport_available(void *raw) {
  cli_pipe_ctx_struct_t *ctx = (cli_pipe_ctx_struct_t *)raw;
  cli_transport_ret_t rc = CLI_PIPE_ERR;

#if ENV_IS_WINDOWS
  DWORD available = 0;
  if (PeekNamedPipe((HANDLE)ctx->h_read, NULL, 0, NULL, &available, NULL)) {
    rc = (available > 0U) ? 1 : 0;
  }
#else
  /* poll() works for any descriptor number; select() is undefined at fd >= FD_SETSIZE. */
  struct pollfd pfd;

  pfd.fd = ctx->read_fd;
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
  rc = (ready > 0) ? 1 : (ready < 0 ? CLI_PIPE_ERR : 0);
#endif

  return rc;
}

/**
 * @brief Read one byte for the pipe transport.
 *
 * @param[in] raw  Pipe transport context.
 *
 * @return Byte value 0..255 on success, or @c CLI_PIPE_ERR on failure.
 */
static cli_transport_ret_t cli_pipe_transport_read(void *raw) {
  cli_pipe_ctx_struct_t *ctx = (cli_pipe_ctx_struct_t *)raw;
  uint8_t byte;
  cli_transport_ret_t rc = CLI_PIPE_ERR;

#if ENV_IS_WINDOWS
  DWORD count = 0;
  if (ReadFile((HANDLE)ctx->h_read, &byte, 1, &count, NULL) && count == 1U) {
    rc = byte;
  }
#else
  ssize_t count;
  do {
    count = read(ctx->read_fd, &byte, 1);
  } while (count < 0 && errno == EINTR);
  if (count == 1) {
    rc = byte;
  }
#endif

  return rc;
}

/**
 * @brief Write a whole buffer to the pipe transport.
 *
 * Partial writes and (POSIX) signal interruptions are retried until every
 * byte has been written.
 *
 * @param[in] raw  Pipe transport context.
 * @param[in] buf  Source buffer.
 * @param[in] len  Number of bytes to write.
 *
 * @return @p len on success, or @c CLI_PIPE_ERR on failure.
 */
static cli_transport_ret_t cli_pipe_transport_write(void *raw, const uint8_t *buf,
                                                    cli_transport_buflen_t len) {
  cli_pipe_ctx_struct_t *ctx = (cli_pipe_ctx_struct_t *)raw;
  size_t sent = 0;
  bool ok = true;

  while (ok && sent < (size_t)len) {
#if ENV_IS_WINDOWS
    DWORD count = 0;
    ok = WriteFile((HANDLE)ctx->h_write, &buf[sent], (DWORD)((size_t)len - sent), &count, NULL) &&
         count > 0U;
    sent += (size_t)count;
#else
    ssize_t count = write(ctx->write_fd, &buf[sent], (size_t)len - sent);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    ok = (count > 0);
    if (ok) {
      sent += (size_t)count;
    }
#endif
  }

  return ok ? (cli_transport_ret_t)len : CLI_PIPE_ERR;
}

/*################################### END OF FILE ######################################*/
