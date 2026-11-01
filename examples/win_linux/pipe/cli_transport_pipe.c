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

#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE /* flock(); must precede every system header */
#endif

#include "cli_transport_pipe.h"

#if defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__) || defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <sddl.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
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

/** @brief Prefix of the mutex that reserves a pipe name for one server. */
#define PIPE_LOCK_PREFIX "Global\\OpenLibCLI.pipe:"

/** @brief Room for an owner-only security descriptor in SDDL (the SID string is <= 184). */
#define PIPE_SDDL_MAX 256

#ifndef PIPE_REJECT_REMOTE_CLIENTS
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008 /* missing from older SDK headers */
#endif
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

#if ENV_IS_WINDOWS
/** @brief A TOKEN_USER with room for the largest SID, correctly aligned. */
typedef union {
  TOKEN_USER user;
  BYTE bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
} win_token_user_union_t;
#endif

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
static bool win_reserve_name(const char *path, void **h_lock);
static bool win_process_user(HANDLE process, win_token_user_union_t *buf, PSID *sid);
static bool win_owner_only_security(SECURITY_ATTRIBUTES *sa);
static bool win_process_is_self(ULONG pid);
static bool win_pipe_server_is_self(HANDLE pipe);
static bool win_pipe_client_is_self(HANDLE pipe);
static BOOL win_overlapped_finish(HANDLE handle, OVERLAPPED *ov, BOOL started, DWORD timeout_ms,
                                  DWORD *count);
#else
static bool build_fifo_paths(const char *name, char *in_path, char *out_path, size_t size);
static bool fifo_reserve_name(const char *name, char *buf, size_t size, int32_t *lock_fd);
static bool fifo_create(const char *path);
static bool fifo_is_private(int32_t fd);
static bool fifo_wait_connected(int32_t rfd, int32_t wfd, uint64_t deadline);
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
  bool reserved_here = false;
  bool ok = build_win_pipe_path(path, sizeof(path), name);

  ctx->h_read = NULL;
  ctx->h_write = NULL;
  ctx->h_event = NULL;
  if (!ok) {
    SetLastError(ERROR_FILENAME_EXCED_RANGE); /* a truncated name would be another pipe */
  } else if (ctx->h_lock == NULL) {
    ok = win_reserve_name(path, &ctx->h_lock);
    reserved_here = ok;
  } else {
    /* Name already reserved by this server's earlier listen. */
  }

  if (ok) {
    SECURITY_ATTRIBUTES sa;
    /* The first listen must create the name: a pipe of this name made by anyone else
     * beforehand would give its own security to every further instance. */
    DWORD first = reserved_here ? (DWORD)FILE_FLAG_FIRST_PIPE_INSTANCE : 0U;

    /* Overlapped, so writes can time out. Only this user, only local clients. Unlimited
     * instances: a session ends by closing its instance (see cli_named_pipe_disconnect()),
     * which lingers until that client has read the rest and closed, and the next session's
     * instance must not wait for that. Only one instance listens at a time, so a second
     * client still finds the server busy. */
    if (win_owner_only_security(&sa)) {
      handle = CreateNamedPipeA(
          path, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | first,
          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
          PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, &sa);
      DWORD error = GetLastError();
      LocalFree(sa.lpSecurityDescriptor);
      /* With FILE_FLAG_FIRST_PIPE_INSTANCE, access denied means the name already exists. */
      SetLastError((handle == INVALID_HANDLE_VALUE && first != 0U && error == ERROR_ACCESS_DENIED)
                       ? ERROR_ALREADY_EXISTS
                       : error);
    }
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

      if (connected && !win_pipe_client_is_self(handle)) {
        /* Not this user. The DACL keeps others out, but the name may have been taken over
         * between two sessions, with that pipe's security: check every client. */
        (void)DisconnectNamedPipe(handle);
        connected = FALSE;
        waiting = TRUE;
      }
    }

    if (connected) {
      ctx->h_read = (void *)handle;
      ctx->h_write = (void *)handle;
      ctx->h_event = (void *)event;
      rc = CLI_PIPE_OK;
    }
  }

  if (rc != CLI_PIPE_OK) {
    DWORD error = GetLastError(); /* report the cause, not the cleanup's result */
    if (event != NULL) {
      CloseHandle(event);
    }
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
    if (reserved_here) {
      CloseHandle((HANDLE)ctx->h_lock);
      ctx->h_lock = NULL;
    }
    SetLastError(error);
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
  ctx->h_lock = NULL;
  if (!trying) {
    SetLastError(ERROR_FILENAME_EXCED_RANGE); /* a truncated name would be another pipe */
  }

  /* Documented client pattern: retry while the single listening instance is busy or not
   * yet re-created, with one overall deadline. The server may identify this client, never
   * act as it (SECURITY_IDENTIFICATION). */
  while (trying) {
    handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                         SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
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
    /* Whoever created a pipe of this name first gets the connection: never send a login
     * to a server that runs as another user. */
    if (win_pipe_server_is_self(handle)) {
      ctx->h_read = (void *)handle;
      ctx->h_write = (void *)handle;
      rc = CLI_PIPE_OK;
    } else {
      CloseHandle(handle);
      SetLastError(ERROR_ACCESS_DENIED);
    }
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
  cli_named_pipe_disconnect(ctx);
  if (ctx->h_lock != NULL) {
    CloseHandle((HANDLE)ctx->h_lock); /* the last handle: the name is free again */
    ctx->h_lock = NULL;
  }
}

void cli_named_pipe_disconnect(cli_pipe_ctx_struct_t *ctx) {
  /* Closing the instance never blocks (FlushFileBuffers() would, for as long as the client
   * does not read). Output already in the pipe stays readable: the client receives it and
   * then ERROR_BROKEN_PIPE. No DisconnectNamedPipe(): it would discard that output. A
   * write end closed on its own (end of a relay's input) is NULL already. */
  if (ctx->h_write != NULL && ctx->h_write != ctx->h_read) {
    CloseHandle((HANDLE)ctx->h_write);
  }
  if (ctx->h_read != NULL) {
    CloseHandle((HANDLE)ctx->h_read);
  }
  ctx->h_read = NULL;
  ctx->h_write = NULL;
  if (ctx->h_event != NULL) {
    CloseHandle((HANDLE)ctx->h_event);
    ctx->h_event = NULL;
  }
}
#else
int32_t cli_named_pipe_listen(const char *name, cli_pipe_ctx_struct_t *ctx) {
  char in_path[FIFO_PATH_MAX];
  char out_path[FIFO_PATH_MAX];
  int32_t rfd = -1;
  int32_t wfd = -1;
  int32_t rc = CLI_PIPE_ERR;
  bool reserved_here = false;
  bool in_created = false;
  bool out_created = false;
  bool ok = true;

  ctx->read_fd = -1;
  ctx->write_fd = -1;

  if (ctx->lock_fd < 0) {
    ok = fifo_reserve_name(name, in_path, sizeof(in_path), &ctx->lock_fd);
    reserved_here = ok;
  }

  /* Holding the name, FIFOs found on disk are leftovers: fresh, owner-only ones replace
   * them for every session (see fifo_create()). */
  if (ok && build_fifo_paths(name, in_path, out_path, sizeof(in_path))) {
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
    if (reserved_here) {
      close(ctx->lock_fd);
      ctx->lock_fd = -1;
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
  int32_t rfd = -1;
  bool trying = build_fifo_paths(name, in_path, out_path, sizeof(in_path));
  uint64_t deadline = monotonic_ms() + (uint64_t)CLI_PIPE_CONNECT_TIMEOUT_MS;

  ctx->read_fd = -1;
  ctx->write_fd = -1;
  ctx->lock_fd = -1;

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
    /* Non-blocking open of <name>.out never waits, and completes the server's open. */
    rfd = open(out_path, O_RDONLY | O_NONBLOCK);
  }
  if (rfd >= 0) {
    int32_t rflags = fcntl(rfd, F_GETFL, 0);
    int32_t wflags = fcntl(wfd, F_GETFL, 0);

    /* Someone else's FIFOs at this path (a shared directory such as /tmp): never send them
     * a username and password. */
    if (!fifo_is_private(wfd) || !fifo_is_private(rfd)) {
      errno = EPERM;
    } else if (fifo_wait_connected(rfd, wfd, deadline) && rflags >= 0 && wflags >= 0 &&
               fcntl(rfd, F_SETFL, rflags & ~O_NONBLOCK) == 0 &&
               fcntl(wfd, F_SETFL, wflags & ~O_NONBLOCK) == 0) {
      ctx->read_fd = rfd;
      ctx->write_fd = wfd;
      rc = CLI_PIPE_OK;
    } else {
      /* errno set by fifo_wait_connected() / fcntl(). */
    }
  }

  if (rc != CLI_PIPE_OK) {
    int saved_errno = errno;
    if (rfd >= 0) {
      close(rfd);
    }
    if (wfd >= 0) {
      close(wfd);
    }
    errno = saved_errno;
  }

  return rc;
}

int32_t cli_anon_pipe_create(cli_pipe_ctx_struct_t *server, cli_pipe_ctx_struct_t *client) {
  int32_t s2c[2];
  int32_t c2s[2];
  int32_t rc = CLI_PIPE_ERR;

  server->read_fd = -1;
  server->write_fd = -1;
  server->lock_fd = -1;
  client->read_fd = -1;
  client->write_fd = -1;
  client->lock_fd = -1;
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
  cli_named_pipe_disconnect(ctx);
  if (ctx->lock_fd >= 0) {
    close(ctx->lock_fd); /* releases the lock: the name is free again */
    ctx->lock_fd = -1;
  }
}

void cli_named_pipe_disconnect(cli_pipe_ctx_struct_t *ctx) {
  /* The FIFO names were removed when the session started, so the next listen creates new
   * FIFOs this client cannot reach: closing is enough and never blocks. The client reads
   * what is left, then sees end of file; its further writes fail with EPIPE. */
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
 * @brief Reserve the pipe @p path for this server with a named mutex.
 *
 * The mutex exists as long as a handle to it is open; a second server finds it and
 * fails. Mutex names are case-sensitive and may not contain '\\', pipe names are
 * neither, so the name is lower-cased and '\\' becomes '/'.
 *
 * @param[in]  path    Full pipe path.
 * @param[out] h_lock  Receives the mutex handle.
 *
 * @return @c true on success; @c false with @c ERROR_ALREADY_EXISTS when another server
 *         holds the name, or the cause of the failure.
 */
static bool win_reserve_name(const char *path, void **h_lock) {
  char lock_name[sizeof(PIPE_LOCK_PREFIX) + PIPE_PATH_MAX];
  size_t pos = sizeof(PIPE_LOCK_PREFIX) - 1U;
  HANDLE mutex;
  bool ok;

  memcpy(lock_name, PIPE_LOCK_PREFIX, pos);
  for (const char *p = path; *p != '\0'; p++) {
    lock_name[pos++] = (*p == '\\') ? '/' : (char)tolower((unsigned char)*p);
  }
  lock_name[pos] = '\0';

  mutex = CreateMutexA(NULL, FALSE, lock_name);
  ok = (mutex != NULL && GetLastError() != ERROR_ALREADY_EXISTS);
  if (ok) {
    *h_lock = (void *)mutex;
  } else {
    DWORD error = (mutex != NULL || GetLastError() == ERROR_ACCESS_DENIED) ? ERROR_ALREADY_EXISTS
                                                                         : GetLastError();
    if (mutex != NULL) {
      CloseHandle(mutex); /* another server's mutex */
    }
    SetLastError(error);
  }

  return ok;
}

/**
 * @brief Read the user account a process runs as.
 *
 * @param[in]  process  Process handle (needs @c PROCESS_QUERY_LIMITED_INFORMATION).
 * @param[out] buf      Storage for the token information.
 * @param[out] sid      Points into @p buf on success.
 *
 * @return @c true on success.
 */
static bool win_process_user(HANDLE process, win_token_user_union_t *buf, PSID *sid) {
  HANDLE token = NULL;
  DWORD len = 0;
  bool ok = (OpenProcessToken(process, TOKEN_QUERY, &token) != FALSE);

  if (ok) {
    ok = (GetTokenInformation(token, TokenUser, buf, (DWORD)sizeof(*buf), &len) != FALSE);
    CloseHandle(token);
  }
  if (ok) {
    *sid = buf->user.User.Sid;
  }

  return ok;
}

/**
 * @brief Security attributes that give only this user (and SYSTEM) access.
 *
 * @param[out] sa  Filled in; free @c sa->lpSecurityDescriptor with @c LocalFree().
 *
 * @return @c true on success.
 */
static bool win_owner_only_security(SECURITY_ATTRIBUTES *sa) {
  win_token_user_union_t user;
  PSID sid = NULL;
  LPSTR sid_text = NULL;
  char sddl[PIPE_SDDL_MAX];
  bool ok = win_process_user(GetCurrentProcess(), &user, &sid) &&
            ConvertSidToStringSidA(sid, &sid_text);

  if (ok) {
    /* Protected DACL: full access for this user and SYSTEM, nobody else. */
    int n = cli_snprintf(sddl, sizeof(sddl), "D:P(A;;GA;;;%s)(A;;GA;;;SY)", sid_text);
    ok = (n > 0 && (size_t)n < sizeof(sddl));
    LocalFree(sid_text);
  }
  if (ok) {
    sa->nLength = (DWORD)sizeof(*sa);
    sa->bInheritHandle = FALSE;
    sa->lpSecurityDescriptor = NULL;
    ok = (ConvertStringSecurityDescriptorToSecurityDescriptorA(
              sddl, SDDL_REVISION_1, &sa->lpSecurityDescriptor, NULL) != FALSE);
  }

  return ok;
}

/**
 * @brief Check that process @p pid runs as this user.
 *
 * Reads the other process's token, which works between processes of the same user and
 * elevation; a process of the same user at a different elevation (an administrator
 * console) may not expose its token and then counts as another user.
 *
 * @param[in] pid  Process id (of the other end of a pipe).
 *
 * @return @c true if that process's user is ours; @c false otherwise or when it cannot
 *         be determined.
 */
static bool win_process_is_self(ULONG pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
  bool same = false;

  if (process != NULL) {
    win_token_user_union_t theirs;
    win_token_user_union_t mine;
    PSID their_sid = NULL;
    PSID my_sid = NULL;

    same = win_process_user(process, &theirs, &their_sid) &&
           win_process_user(GetCurrentProcess(), &mine, &my_sid) && EqualSid(their_sid, my_sid);
    CloseHandle(process);
  }

  return same;
}

/**
 * @brief Check that the server end of @p pipe runs as this user.
 *
 * @param[in] pipe  Connected client handle.
 *
 * @return @c true if the server process's user is ours (see @c win_process_is_self()).
 */
static bool win_pipe_server_is_self(HANDLE pipe) {
  ULONG pid = 0;

  return GetNamedPipeServerProcessId(pipe, &pid) && win_process_is_self(pid);
}

/**
 * @brief Check that the client end of @p pipe runs as this user.
 *
 * @param[in] pipe  Connected server handle.
 *
 * @return @c true if the client process's user is ours (see @c win_process_is_self()).
 */
static bool win_pipe_client_is_self(HANDLE pipe) {
  ULONG pid = 0;

  return GetNamedPipeClientProcessId(pipe, &pid) && win_process_is_self(pid);
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
 * @brief Reserve @p name for this server: an exclusive lock on "<name>.lock".
 *
 * Held until @c cli_pipe_close(), so only one server uses the name and FIFOs found on
 * disk while holding it are leftovers. The lock file stays on disk (removing it would
 * let two servers lock different files of the same name).
 *
 * flock() rather than an fcntl() record lock: it belongs to the open file description, so
 * a second listen in the same process is refused too, the lock survives fork() (e.g. a
 * daemon) and closing an unrelated descriptor of the file does not release it. The
 * descriptor is close-on-exec, and a symbolic link at the lock path is not followed.
 *
 * @param[in]  name     Base path.
 * @param[out] buf      Scratch buffer for the lock file path.
 * @param[in]  size     Size of @p buf.
 * @param[out] lock_fd  Receives the descriptor that holds the lock.
 *
 * @return @c true on success; @c false with @c errno = @c EADDRINUSE when another server
 *         holds it, or the cause of the failure.
 */
static bool fifo_reserve_name(const char *name, char *buf, size_t size, int32_t *lock_fd) {
  int n = cli_snprintf(buf, size, "%s.lock", name);
  int32_t fd = -1;
  bool ok = (n >= 0 && (size_t)n < size);

  if (!ok) {
    errno = ENAMETOOLONG;
  } else {
    fd = open(buf, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    ok = (fd >= 0);
  }
  if (ok && flock(fd, LOCK_EX | LOCK_NB) != 0) {
    int saved = errno;
    close(fd);
    errno = (saved == EWOULDBLOCK) ? EADDRINUSE : saved;
    ok = false;
  }
  if (ok) {
    *lock_fd = fd;
  }

  return ok;
}

/**
 * @brief Create the FIFO @p path, owner-only, replacing a FIFO left behind.
 *
 * Called with the name reserved, so an existing FIFO belongs to no running server. A
 * new FIFO per session means nothing still attached to an old one (a client that has
 * not exited yet) can take part in the new session. Mode 0600: other users can neither
 * type into the CLI nor read its output. A path that exists but is not a FIFO is never
 * removed.
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
 * @brief Check that @p fd is a FIFO of this user that nobody else can open.
 *
 * @param[in] fd  Open descriptor.
 *
 * @return @c true for a FIFO owned by the effective user with no group / other access.
 */
static bool fifo_is_private(int32_t fd) {
  struct stat st;

  return (fstat(fd, &st) == 0 && S_ISFIFO(st.st_mode) && st.st_uid == geteuid() &&
          (st.st_mode & (S_IRWXG | S_IRWXO)) == 0);
}

/**
 * @brief Wait until the session has started: the server's first output (banner or
 *        prompt) arrives on @p rfd.
 *
 * Both FIFOs were opened without blocking, so whether a server is at the other end is
 * only known once it writes. A server that went away closes its read end of
 * <name>.in, which shows as an error on @p wfd (Linux). Where poll() reports nothing
 * for a descriptor watched with no events (macOS), that case ends at the deadline
 * instead; it needs the server to die between opening <name>.in and blocking in its
 * open of <name>.out, where a listening server waits for this client.
 *
 * @param[in] rfd       Client end of <name>.out (non-blocking).
 * @param[in] wfd       Client end of <name>.in.
 * @param[in] deadline  @c monotonic_ms() value to give up at.
 *
 * @return @c true once output is readable; @c false with @c errno = @c ETIMEDOUT or
 *         @c ECONNREFUSED.
 */
static bool fifo_wait_connected(int32_t rfd, int32_t wfd, uint64_t deadline) {
  bool connected = false;
  bool waiting = true;

  while (waiting) {
    struct pollfd pfd[2];
    uint64_t now = monotonic_ms();
    int ready;

    pfd[0].fd = rfd;
    pfd[0].events = POLLIN;
    pfd[0].revents = 0;
    pfd[1].fd = wfd;
    pfd[1].events = 0; /* errors only */
    pfd[1].revents = 0;
    ready = (now < deadline) ? poll(pfd, 2, (int)(deadline - now)) : 0;

    if (ready < 0 && errno == EINTR) {
      /* Interrupted: wait for the rest of the time. */
    } else if (ready > 0 && (pfd[0].revents & POLLIN) != 0) {
      connected = true;
      waiting = false;
    } else if (ready > 0) {
      errno = ECONNREFUSED; /* the server closed its ends without a word */
      waiting = false;
    } else {
      errno = (ready == 0) ? ETIMEDOUT : errno;
      waiting = false;
    }
  }

  return connected;
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
