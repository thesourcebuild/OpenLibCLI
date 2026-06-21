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
#include <stdio.h>
#include <sys/select.h>
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
static void build_win_pipe_path(char *buf, int32_t buflen, const char *name);
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

  build_win_pipe_path(path, sizeof(path), name);

  HANDLE handle =
      CreateNamedPipeA(path, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                       4096, 4096, 0, NULL);
  if (handle != INVALID_HANDLE_VALUE) {
    if (ConnectNamedPipe(handle, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
      ctx->h_read = (void *)handle;
      ctx->h_write = (void *)handle;
      rc = CLI_PIPE_OK;
    } else {
      CloseHandle(handle);
    }
  }

  return rc;
}

int32_t cli_named_pipe_connect(const char *name, cli_pipe_ctx_struct_t *ctx) {
  char path[PIPE_PATH_MAX];
  HANDLE handle = INVALID_HANDLE_VALUE;
  int32_t rc = CLI_PIPE_ERR;
  int32_t attempts = 0;

  build_win_pipe_path(path, sizeof(path), name);

  while (attempts < 5) {
    handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (handle != INVALID_HANDLE_VALUE) {
      break;
    }

    if (GetLastError() == ERROR_PIPE_BUSY) {
      if (!WaitNamedPipeA(path, 1000)) {
        break;
      }
    }

    attempts++;
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
#else
int32_t cli_named_pipe_listen(const char *name, cli_pipe_ctx_struct_t *ctx) {
  char in_path[FIFO_PATH_MAX];
  char out_path[FIFO_PATH_MAX];
  int32_t rfd = -1;
  int32_t wfd = -1;
  int32_t rc = CLI_PIPE_ERR;

  cli_snprintf(in_path, sizeof(in_path), "%s.in", name);
  cli_snprintf(out_path, sizeof(out_path), "%s.out", name);

  if (mkfifo(in_path, 0666) == 0 || errno == EEXIST) {
    if (mkfifo(out_path, 0666) == 0 || errno == EEXIST) {
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
    } else {
      unlink(in_path);
    }
  }

  if (rc != CLI_PIPE_OK) {
    if (rfd >= 0) {
      close(rfd);
    }
    if (wfd >= 0) {
      close(wfd);
    }
  }

  return rc;
}

int32_t cli_named_pipe_connect(const char *name, cli_pipe_ctx_struct_t *ctx) {
  char in_path[FIFO_PATH_MAX];
  char out_path[FIFO_PATH_MAX];
  int32_t rc = CLI_PIPE_ERR;

  cli_snprintf(in_path, sizeof(in_path), "%s.in", name);
  cli_snprintf(out_path, sizeof(out_path), "%s.out", name);

  int32_t rfd = open(out_path, O_RDONLY);
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
#endif

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/* Windows Pipe Helpers -----------------------------------------------------------------*/

/**
 * @brief Build a fully-qualified Windows named-pipe path.
 *
 * @param[out] buf     Destination buffer.
 * @param[in]  buflen  Size of @p buf in bytes.
 * @param[in]  name    User-supplied pipe name.
 */
static void build_win_pipe_path(char *buf, int32_t buflen, const char *name) {
  if (strncmp(name, "\\\\", 2) == 0) {
    size_t name_len = strlen(name);
    size_t copy_len = (name_len < (size_t)(buflen - 1)) ? name_len : (size_t)(buflen - 1);
    memcpy(buf, name, copy_len);
    buf[copy_len] = '\0';
  } else {
    _snprintf(buf, (size_t)buflen, "\\\\.\\pipe\\%s", name);
  }
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
  fd_set rfds;
  struct timeval tv;

  FD_ZERO(&rfds);
  FD_SET(ctx->read_fd, &rfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  int ready = select(ctx->read_fd + 1, &rfds, NULL, NULL, &tv);
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
  ssize_t count = read(ctx->read_fd, &byte, 1);
  if (count == 1) {
    rc = byte;
  }
#endif

  return rc;
}

/**
 * @brief Write for the pipe transport.
 *
 * @param[in] raw  Pipe transport context.
 * @param[in] buf  Source buffer.
 * @param[in] len  Number of bytes to write.
 *
 * @return Bytes written on success, or @c CLI_PIPE_ERR on failure.
 */
static cli_transport_ret_t cli_pipe_transport_write(void *raw, const uint8_t *buf,
                                                    cli_transport_buflen_t len) {
  cli_pipe_ctx_struct_t *ctx = (cli_pipe_ctx_struct_t *)raw;
  cli_transport_ret_t rc = CLI_PIPE_ERR;

#if ENV_IS_WINDOWS
  DWORD count = 0;
  if (WriteFile((HANDLE)ctx->h_write, buf, (DWORD)len, &count, NULL)) {
    rc = (cli_transport_ret_t)count;
  }
#else
  ssize_t count = write(ctx->write_fd, buf, len);
  if (count >= 0) {
    rc = (cli_transport_ret_t)count;
  }
#endif

  return rc;
}

/*################################### END OF FILE ######################################*/
