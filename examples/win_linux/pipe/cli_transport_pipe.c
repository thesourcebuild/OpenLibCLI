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
#include <time.h>
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

/** @brief Pause between two connection attempts while the server is busy. */
#define PIPE_CONNECT_RETRY_MS 50U

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
static BOOL win_overlapped_finish(HANDLE handle, OVERLAPPED *ov, BOOL started, DWORD timeout_ms,
                                  DWORD *count);
#else
static bool build_fifo_paths(const char *name, char *in_path, char *out_path, size_t size);
static bool fifo_in_use(const char *in_path);
static bool fifo_create(const char *path);
static uint64_t monotonic_ms(void);
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
  HANDLE event = NULL;

  ctx->h_read = NULL;
  ctx->h_write = NULL;
  ctx->h_event = NULL;

  /* Overlapped, so writes can time out. Unlimited instances: a session ends by closing its
   * instance (see cli_named_pipe_disconnect()), which lingers until that client has read
   * the rest and closed, and the next session's instance must not wait for that. Only one
   * instance listens at a time, so a second client still finds the server busy. */
  if (build_win_pipe_path(path, sizeof(path), name)) {
    handle = CreateNamedPipeA(path, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                              PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, NULL);
  } else {
    SetLastError(ERROR_FILENAME_EXCED_RANGE); /* a truncated name would be another pipe */
  }
  if (handle != INVALID_HANDLE_VALUE) {
    event = CreateEventA(NULL, TRUE, FALSE, NULL);
  }
  if (event != NULL) {
    BOOL connected = FALSE;
    BOOL waiting = TRUE;

    while (waiting) {
      OVERLAPPED ov;
      DWORD unused = 0;

      memset(&ov, 0, sizeof(ov));
      ov.hEvent = event;
      if (ConnectNamedPipe(handle, &ov)) {
        connected = TRUE;
        waiting = FALSE;
      } else if (GetLastError() == ERROR_IO_PENDING) {
        connected = GetOverlappedResult(handle, &ov, &unused, TRUE); /* wait for a client */
        waiting = FALSE;
      } else if (GetLastError() == ERROR_PIPE_CONNECTED) {
        connected = TRUE; /* the client was faster than this call */
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
      ctx->h_event = (void *)event;
      rc = CLI_PIPE_OK;
    }
  }

  if (rc != CLI_PIPE_OK && handle != INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    if (event != NULL) {
      CloseHandle(event);
    }
    CloseHandle(handle);
    SetLastError(error); /* report the cause, not CloseHandle's result */
  }

  return rc;
}

int32_t cli_named_pipe_connect(const char *name, cli_pipe_ctx_struct_t *ctx) {
  char path[PIPE_PATH_MAX];
  HANDLE handle = INVALID_HANDLE_VALUE;
  int32_t rc = CLI_PIPE_ERR;
  bool trying = build_win_pipe_path(path, sizeof(path), name);
  ULONGLONG deadline = GetTickCount64() + (ULONGLONG)CLI_PIPE_CONNECT_TIMEOUT_MS;

  ctx->h_read = NULL;
  ctx->h_write = NULL;
  ctx->h_event = NULL;
  if (!trying) {
    SetLastError(ERROR_FILENAME_EXCED_RANGE); /* a truncated name would be another pipe */
  }

  /* Documented client pattern: retry while the single listening instance is busy or not
   * yet re-created, with one overall deadline. */
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
        /* Serving another client: wait until an instance listens (or the deadline). */
        (void)WaitNamedPipeA(path, (DWORD)(deadline - now));
      } else if (error == ERROR_FILE_NOT_FOUND) {
        /* Between sessions: the server re-creates the pipe right away. */
        Sleep(PIPE_CONNECT_RETRY_MS);
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

  memset(server, 0, sizeof(*server));
  memset(client, 0, sizeof(*client));
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
  if (ctx->h_event != NULL) {
    CloseHandle((HANDLE)ctx->h_event);
    ctx->h_event = NULL;
  }
}

void cli_named_pipe_disconnect(cli_pipe_ctx_struct_t *ctx) {
  /* Closing the instance never blocks (FlushFileBuffers() would, for as long as the client
   * does not read). Output already in the pipe stays readable: the client receives it and
   * then ERROR_BROKEN_PIPE. No DisconnectNamedPipe(): it would discard that output. */
  cli_pipe_close(ctx);
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

  ctx->read_fd = -1;
  ctx->write_fd = -1;

  /* Fresh, owner-only FIFOs for every session (see fifo_create()). */
  if (build_fifo_paths(name, in_path, out_path, sizeof(in_path)) && !fifo_in_use(in_path)) {
    in_created = fifo_create(in_path);
    out_created = in_created && fifo_create(out_path);
  }
  if (out_created) {
    rfd = open(in_path, O_RDONLY | O_NONBLOCK);
  }
  if (rfd >= 0) {
    wfd = open(out_path, O_WRONLY); /* blocks until a client opens <name>.out */
  }
  if (wfd >= 0) {
    int32_t rflags = fcntl(rfd, F_GETFL, 0);
    int32_t wflags = fcntl(wfd, F_GETFL, 0);

    /* Reads only happen after available() reported data. Writes are non-blocking so a
     * client that stops reading is timed out (CLI_PIPE_SEND_TIMEOUT_MS). */
    if (rflags >= 0 && wflags >= 0 && fcntl(rfd, F_SETFL, rflags & ~O_NONBLOCK) == 0 &&
        fcntl(wfd, F_SETFL, wflags | O_NONBLOCK) == 0) {
      ctx->read_fd = rfd;
      ctx->write_fd = wfd;
      rc = CLI_PIPE_OK;
    }
  }

  if (rc == CLI_PIPE_OK) {
    /* The client holds both FIFOs open: take the names away. A later client cannot join
     * this session (it waits until the next listen creates new FIFOs), and this client's
     * leftover ends can never be mistaken for the next session's client. */
    (void)unlink(in_path);
    (void)unlink(out_path);
  } else {
    int saved_errno = errno; /* the cause, for the caller; cleanup must not overwrite it */

    if (rfd >= 0) {
      close(rfd);
    }
    if (wfd >= 0) {
      close(wfd);
    }
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
  int32_t wfd = -1;
  bool trying = build_fifo_paths(name, in_path, out_path, sizeof(in_path));
  uint64_t deadline = monotonic_ms() + (uint64_t)CLI_PIPE_CONNECT_TIMEOUT_MS;

  ctx->read_fd = -1;
  ctx->write_fd = -1;

  /* <name>.in first, without blocking: it opens only while a server is listening (ENOENT
   * while it serves another client, ENXIO when nobody reads the FIFO). The server takes
   * the names away once both ends are open, so <name>.in must be open before <name>.out
   * completes the connection. */
  while (trying) {
    wfd = open(in_path, O_WRONLY | O_NONBLOCK);
    if (wfd >= 0) {
      trying = false;
    } else if ((errno == ENOENT || errno == ENXIO) && monotonic_ms() < deadline) {
      struct timespec pause = {0, (long)PIPE_CONNECT_RETRY_MS * 1000000L};
      (void)nanosleep(&pause, NULL);
    } else {
      trying = false;
    }
  }

  if (wfd >= 0) {
    int32_t flags = fcntl(wfd, F_GETFL, 0);
    int32_t rfd = -1;

    if (flags >= 0 && fcntl(wfd, F_SETFL, flags & ~O_NONBLOCK) == 0) {
      rfd = open(out_path, O_RDONLY); /* the server is waiting for this open */
    }
    if (rfd >= 0) {
      ctx->read_fd = rfd;
      ctx->write_fd = wfd;
      rc = CLI_PIPE_OK;
    } else {
      int saved_errno = errno;
      close(wfd);
      errno = saved_errno;
    }
  }

  return rc;
}

int32_t cli_anon_pipe_create(cli_pipe_ctx_struct_t *server, cli_pipe_ctx_struct_t *client) {
  int32_t s2c[2];
  int32_t c2s[2];
  int32_t rc = CLI_PIPE_ERR;

  server->read_fd = -1;
  server->write_fd = -1;
  client->read_fd = -1;
  client->write_fd = -1;
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
  /* The FIFO names were removed when the session started, so the next listen creates new
   * FIFOs this client cannot reach: closing is enough and never blocks. The client reads
   * what is left, then sees end of file; its further writes fail with EPIPE. */
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

/**
 * @brief Complete an overlapped ReadFile() / WriteFile() on @p handle.
 *
 * @param[in]  handle      Handle the operation was started on.
 * @param[in]  ov          Its OVERLAPPED structure.
 * @param[in]  started     Return value of the ReadFile() / WriteFile() call.
 * @param[in]  timeout_ms  How long a pending operation may take (@c INFINITE allowed);
 *                         it is cancelled when the time is up.
 * @param[out] count       Bytes transferred.
 *
 * @return @c TRUE if the operation completed successfully.
 */
static BOOL win_overlapped_finish(HANDLE handle, OVERLAPPED *ov, BOOL started, DWORD timeout_ms,
                                  DWORD *count) {
  BOOL ok = started;

  *count = 0;
  if (started) {
    ok = GetOverlappedResult(handle, ov, count, FALSE); /* completed at once */
  } else if (GetLastError() == ERROR_IO_PENDING) {
    if (WaitForSingleObject(ov->hEvent, timeout_ms) != WAIT_OBJECT_0) {
      (void)CancelIo(handle);
    }
    /* Waits for the (possibly cancelled) operation; FALSE with ERROR_OPERATION_ABORTED if
     * it was cancelled before completing. */
    ok = GetOverlappedResult(handle, ov, count, TRUE);
  } else {
    /* Failed to start: there is nothing to wait for. */
  }

  return ok;
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
 * @brief Test whether another server is listening on @p in_path.
 *
 * A listening server holds the FIFO open for reading, so a non-blocking open for
 * writing succeeds. A FIFO left behind by a server that is gone has no reader
 * (@c ENXIO) and may be replaced.
 *
 * @param[in] in_path  Client-to-server FIFO path.
 *
 * @return @c true (with @c errno = @c EADDRINUSE) if a server reads it.
 */
static bool fifo_in_use(const char *in_path) {
  struct stat st;
  bool in_use = false;

  if (lstat(in_path, &st) == 0 && S_ISFIFO(st.st_mode)) {
    int32_t probe = open(in_path, O_WRONLY | O_NONBLOCK);
    if (probe >= 0) {
      close(probe);
      errno = EADDRINUSE;
      in_use = true;
    }
  }
  return in_use;
}

/**
 * @brief Create the FIFO @p path, owner-only, replacing a FIFO left behind.
 *
 * A new FIFO per session means nothing still attached to an old one (a client
 * that has not exited yet) can take part in the new session. Mode 0600: other
 * users can neither type into the CLI nor read its output. A path that exists
 * but is not a FIFO is never removed.
 *
 * @param[in] path  FIFO path.
 *
 * @return @c true if this call created @p path; @c false with @c errno set otherwise.
 */
static bool fifo_create(const char *path) {
  struct stat st;
  bool ok = false;

  if (lstat(path, &st) == 0 && !S_ISFIFO(st.st_mode)) {
    errno = EEXIST; /* never write CLI traffic into (or delete) a regular file */
  } else if (unlink(path) != 0 && errno != ENOENT) {
    /* A stale FIFO that cannot be removed (e.g. another user's file in /tmp). */
  } else {
    ok = (mkfifo(path, 0600) == 0);
  }

  return ok;
}

/**
 * @brief Monotonic clock in milliseconds (for connection deadlines).
 *
 * @return Milliseconds since an unspecified start point.
 */
static uint64_t monotonic_ms(void) {
  struct timespec ts;

  (void)clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((uint64_t)ts.tv_sec * 1000U) + ((uint64_t)ts.tv_nsec / 1000000U);
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
  HANDLE handle = (HANDLE)ctx->h_read;
  DWORD count = 0;
  BOOL ok;

  if (ctx->h_event == NULL) {
    ok = ReadFile(handle, &byte, 1, &count, NULL);
  } else {
    OVERLAPPED ov;

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = (HANDLE)ctx->h_event;
    ok = win_overlapped_finish(handle, &ov, ReadFile(handle, &byte, 1, NULL, &ov), INFINITE,
                               &count);
  }
  if (ok && count == 1U) {
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
 * byte has been written. On the server end of a named pipe a write that makes
 * no progress for @c CLI_PIPE_SEND_TIMEOUT_MS (the client stopped reading)
 * fails instead of blocking the server.
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
    HANDLE handle = (HANDLE)ctx->h_write;
    DWORD chunk = (DWORD)((size_t)len - sent);
    DWORD count = 0;

    if (ctx->h_event == NULL) {
      ok = WriteFile(handle, &buf[sent], chunk, &count, NULL) && count > 0U;
    } else {
      OVERLAPPED ov;

      memset(&ov, 0, sizeof(ov));
      ov.hEvent = (HANDLE)ctx->h_event;
      ok = win_overlapped_finish(handle, &ov, WriteFile(handle, &buf[sent], chunk, NULL, &ov),
                                 (DWORD)CLI_PIPE_SEND_TIMEOUT_MS, &count) &&
           count > 0U;
    }
    sent += (size_t)count;
#else
    ssize_t count = write(ctx->write_fd, &buf[sent], (size_t)len - sent);

    if (count > 0) {
      sent += (size_t)count;
    } else if (count < 0 && errno == EINTR) {
      /* Interrupted before anything was written: try again. */
    } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      /* Server end, FIFO full: wait for the client to read, but not forever. */
      struct pollfd pfd;
      int ready;

      pfd.fd = ctx->write_fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      do {
        ready = poll(&pfd, 1, (int)CLI_PIPE_SEND_TIMEOUT_MS);
      } while (ready < 0 && errno == EINTR);
      ok = (ready > 0);
    } else {
      ok = false;
    }
#endif
  }

  return ok ? (cli_transport_ret_t)len : CLI_PIPE_ERR;
}

/*################################### END OF FILE ######################################*/
