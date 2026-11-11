/**
 * @file cli_transport_tcp.c
 * @brief Portable TCP server helper implementation.
 *
 * Abstracts WinSock2 / POSIX BSD socket differences so the rest of the
 * library stays platform-independent on OS targets. This transport is
 * intended for Windows and Unix-like systems.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/
#include "../../../cli/cli_env_detect.h"
#include "../../../cli/cli.h"
#include "cli_transport_tcp.h"

#include <string.h>

#if ENV_IS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/** @brief Invalid socket sentinel on Windows. */
#define CLI_TCP_INVALID_SOCKET INVALID_SOCKET
#else
/** @brief Invalid socket sentinel on POSIX. */
#define CLI_TCP_INVALID_SOCKET (-1)
#endif

/** @brief One byte returned from the staged receive buffer. */
#define CLI_TCP_STAGED_READ_COUNT 1

/** @brief Fatal transport-read result (disconnect or non-retryable error). */
#define CLI_TCP_TRANSPORT_READ_ERR (-1)

/*=======================================================================================
 * Private Macros
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/** @brief Close a TCP socket on Windows. */
#define CLI_TCP_CLOSE_SOCKET(socket_fd) closesocket(socket_fd)

/** @brief Receive bytes from a TCP socket on Windows. */
#define CLI_TCP_RECV(socket_fd, buf, len) recv((SOCKET)(socket_fd), (char *)(buf), (len), 0)

/** @brief Send bytes to a TCP socket on Windows. */
#define CLI_TCP_SEND(socket_fd, buf, len) send((SOCKET)(socket_fd), (const char *)(buf), (len), 0)
#else
/** @brief Close a TCP socket on POSIX. */
#define CLI_TCP_CLOSE_SOCKET(socket_fd)   close(socket_fd)

/** @brief Receive bytes from a TCP socket on POSIX. */
#define CLI_TCP_RECV(socket_fd, buf, len) recv((socket_fd), (buf), (len), 0)

#ifdef MSG_NOSIGNAL
/** @brief Send flags: report EPIPE instead of raising SIGPIPE (Linux / BSD). */
#define CLI_TCP_SEND_FLAGS MSG_NOSIGNAL
#else
/** @brief macOS has no MSG_NOSIGNAL; SO_NOSIGPIPE is set on the socket instead. */
#define CLI_TCP_SEND_FLAGS 0
#endif

/** @brief Send bytes to a TCP socket on POSIX. */
#define CLI_TCP_SEND(socket_fd, buf, len) send((socket_fd), (buf), (len), CLI_TCP_SEND_FLAGS)
#endif

/*=======================================================================================
 * Private Types
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/** @brief Platform socket type on Windows. */
typedef SOCKET cli_sock_t;
#else
/** @brief Platform socket type on POSIX. */
typedef int32_t cli_sock_t;
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
static int32_t cli_winsock_init(void);
#endif

static bool cli_tcp_is_interrupted(void);
static int32_t cli_tcp_wait_readable(cli_sock_t socket_fd, uint32_t timeout_ms);
static cli_transport_ret_t cli_tcp_send_all(cli_sock_t socket_fd, const uint8_t *buf,
                                            cli_transport_buflen_t len);
static cli_transport_ret_t cli_tcp_transport_available(void *raw_ctx);
static cli_transport_ret_t cli_tcp_transport_read(void *raw_ctx);
static cli_transport_ret_t cli_tcp_transport_write(void *raw_ctx, const uint8_t *buf,
                                                   cli_transport_buflen_t len);
static cli_transport_ret_t cli_tcp_transport_flush(void *raw_ctx);

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

void cli_tcp_init(cli_tcp_ctx_struct_t *ctx, cli_transport_struct_t *transport, int32_t fd,
                  bool auto_newline) {
#if !(ENV_IS_WINDOWS || ENV_IS_UNIX_LIKE)
  (void)ctx;
  (void)transport;
  (void)fd;
  (void)auto_newline;
#else
  memset(ctx, 0, sizeof(*ctx));
  ctx->fd = fd;
  ctx->auto_newline = auto_newline;

  transport->available = cli_tcp_transport_available;
  transport->read = cli_tcp_transport_read;
  transport->write = cli_tcp_transport_write;
  transport->flush = cli_tcp_transport_flush;
  transport->ctx = ctx;
  transport->kind = CLI_TRANSPORT_TCP;
#endif
}

int32_t cli_tcp_listen(uint16_t port) {
#if !(ENV_IS_WINDOWS || ENV_IS_UNIX_LIKE)
  (void)port;
  return CLI_TCP_ERR;
#else
  int32_t result = CLI_TCP_ERR;

#if ENV_IS_WINDOWS
  bool init_ok = (cli_winsock_init() == CLI_TCP_OK);
#else
  bool init_ok = true;
#endif

  cli_sock_t sock = init_ok ? socket(AF_INET, SOCK_STREAM, 0) : CLI_TCP_INVALID_SOCKET;
  if (sock != CLI_TCP_INVALID_SOCKET) {
#if ENV_IS_WINDOWS
    /* On Windows SO_REUSEADDR would let another process bind the same port and steal
     * connections; SO_EXCLUSIVEADDRUSE forbids that (restart after a crash still works). */
    BOOL yes = TRUE;
    (void)setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&yes, sizeof(yes));
#else
    int32_t yes = 1;
    (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0 && listen(sock, 4) == 0) {
#if ENV_IS_WINDOWS
      result = (int32_t)sock;
#else
      result = sock;
#endif
    } else {
      CLI_TCP_CLOSE_SOCKET(sock);
    }
  }

  return result;
#endif
}

int32_t cli_tcp_accept(int32_t listen_fd) {
#if !(ENV_IS_WINDOWS || ENV_IS_UNIX_LIKE)
  (void)listen_fd;
  return CLI_TCP_ERR;
#else
  int32_t result = CLI_TCP_ERR;
  struct sockaddr_in peer;
#if ENV_IS_WINDOWS
  int32_t peer_len = (int32_t)sizeof(peer);
#else
  socklen_t peer_len = (socklen_t)sizeof(peer);
#endif

  cli_sock_t client = accept((cli_sock_t)listen_fd, (struct sockaddr *)&peer, &peer_len);
  if (client != CLI_TCP_INVALID_SOCKET) {
    /* Reads wait in cli_tcp_wait_readable(); the send timeout stops a client
     * that never reads its output from blocking the single-threaded server forever. */
#if ENV_IS_WINDOWS
    BOOL nd = TRUE;
    DWORD snd_to = (DWORD)CLI_TCP_SEND_TIMEOUT_MS;
    (void)setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char *)&nd, sizeof(nd));
    (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&snd_to, sizeof(snd_to));
#else
    int32_t nd = 1;
    struct timeval tv;
    tv.tv_sec = CLI_TCP_SEND_TIMEOUT_MS / 1000;
    tv.tv_usec = (CLI_TCP_SEND_TIMEOUT_MS % 1000) * 1000;
    (void)setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));
    (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
    int32_t nosig = 1;
    (void)setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif
#endif
#if ENV_IS_WINDOWS
    result = (int32_t)client;
#else
    result = client;
#endif
  }

  return result;
#endif
}

void cli_tcp_close(int32_t fd) {
#if !(ENV_IS_WINDOWS || ENV_IS_UNIX_LIKE)
  (void)fd;
#else
  if (fd >= 0) {
    CLI_TCP_CLOSE_SOCKET((cli_sock_t)fd);
  }
#endif
}

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/**
 * @brief Initialise WinSock once per process.
 *
 * @return @c CLI_TCP_OK on success, otherwise @c CLI_TCP_ERR.
 */
static int32_t cli_winsock_init(void) {
  static bool initialized = false;
  int32_t rc = CLI_TCP_OK;

  if (!initialized) {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
      rc = CLI_TCP_ERR;
    } else {
      initialized = true;
    }
  }

  return rc;
}
#endif

/**
 * @brief Return whether the last socket call was interrupted by a signal.
 *
 * @return @c true when the call should simply be repeated.
 */
static bool cli_tcp_is_interrupted(void) {
#if ENV_IS_WINDOWS
  return WSAGetLastError() == WSAEINTR;
#else
  return errno == EINTR;
#endif
}

/**
 * @brief Wait until @p socket_fd is readable or @p timeout_ms elapses.
 *
 * @param[in] socket_fd   Connected socket.
 * @param[in] timeout_ms  Maximum wait in milliseconds.
 *
 * @return 1 if readable, 0 on timeout or signal interruption, -1 on error.
 */
static int32_t cli_tcp_wait_readable(cli_sock_t socket_fd, uint32_t timeout_ms) {
  int32_t ready;

#if ENV_IS_WINDOWS
  /* WinSock's fd_set is a list of handles (no FD_SETSIZE bitmap limit); nfds is ignored. */
  fd_set rfds;
  struct timeval tv;

  FD_ZERO(&rfds);
  FD_SET(socket_fd, &rfds);
  tv.tv_sec = (long)(timeout_ms / 1000U);
  tv.tv_usec = (long)((timeout_ms % 1000U) * 1000U);
  ready = select(0, &rfds, NULL, NULL, &tv);
#else
  /* poll() works for any descriptor number; select() is undefined at fd >= FD_SETSIZE. */
  struct pollfd pfd;

  pfd.fd = socket_fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  ready = poll(&pfd, 1, (int)timeout_ms);
  if (ready > 0 && (pfd.revents & POLLNVAL) != 0) {
    ready = -1;
    errno = EBADF;
  }
#endif
  if (ready < 0 && cli_tcp_is_interrupted()) {
    ready = 0;
  }

  return (ready > 0) ? 1 : ((ready == 0) ? 0 : -1);
}

/**
 * @brief Send a whole buffer, retrying partial sends and signal interruptions.
 *
 * A send timeout (@c CLI_TCP_SEND_TIMEOUT_MS, the peer stopped reading) or a
 * closed peer is reported as an error so the session ends.
 *
 * @param[in] socket_fd  Connected socket.
 * @param[in] buf        Bytes to send.
 * @param[in] len        Number of bytes.
 *
 * @return @p len on success, @c -1 on error.
 */
static cli_transport_ret_t cli_tcp_send_all(cli_sock_t socket_fd, const uint8_t *buf,
                                            cli_transport_buflen_t len) {
  cli_transport_buflen_t sent = 0;
  cli_transport_ret_t result = (cli_transport_ret_t)len;

  while (sent < len && result >= 0) {
    int32_t rc = (int32_t)CLI_TCP_SEND(socket_fd, buf + sent, len - sent);
    if (rc > 0) {
      sent += (cli_transport_buflen_t)rc;
    } else if (rc < 0 && cli_tcp_is_interrupted()) {
      /* Interrupted by a signal: send the rest again. */
    } else {
      result = CLI_TCP_TRANSPORT_READ_ERR;
    }
  }

  return result;
}

/**
 * @brief Report whether a decoded byte is ready, receiving more when needed.
 *
 * Waits up to @c CLI_TRANSPORT_POLL_MS for data. With @c auto_newline, a
 * synthetic @c '\n' is produced only when the received text does not end
 * with a line terminator and the peer then stays silent for a full poll
 * interval, so a command split across TCP segments is not cut in two.
 *
 * @param[in] raw_ctx  Raw TCP context pointer.
 *
 * @return @c 1 if a byte is ready, @c 0 on timeout, or @c -1 on disconnect or error.
 */
static cli_transport_ret_t cli_tcp_transport_available(void *raw_ctx) {
  cli_tcp_ctx_struct_t *ctx = (cli_tcp_ctx_struct_t *)raw_ctx;
  cli_transport_ret_t rc = 0;

  if (ctx->rx_pos < ctx->rx_len) {
    rc = 1;
  } else {
    ctx->rx_pos = 0;
    ctx->rx_len = 0;

    int32_t ready = cli_tcp_wait_readable((cli_sock_t)ctx->fd, (uint32_t)CLI_TRANSPORT_POLL_MS);
    if (ready > 0) {
      cli_transport_ret_t received =
          CLI_TCP_RECV(ctx->fd, ctx->rx_buf, (int32_t)sizeof(ctx->rx_buf));
      if (received > 0) {
        uint8_t last = ctx->rx_buf[received - 1];
        ctx->line_open = (last != '\r' && last != '\n');
        ctx->rx_len = (uint16_t)received;
        rc = 1;
      } else if (received < 0 && cli_tcp_is_interrupted()) {
        rc = 0;
      } else {
        rc = CLI_TCP_TRANSPORT_READ_ERR; /* orderly close (0) or socket error */
      }
    } else if (ready == 0) {
      if (ctx->auto_newline && ctx->line_open) {
        ctx->rx_buf[0] = '\n';
        ctx->rx_len = 1U;
        ctx->line_open = false;
        rc = 1;
      }
    } else {
      rc = CLI_TCP_TRANSPORT_READ_ERR;
    }
  }

  return rc;
}

static cli_transport_ret_t cli_tcp_transport_read(void *raw_ctx) {
  cli_tcp_ctx_struct_t *ctx = (cli_tcp_ctx_struct_t *)raw_ctx;
  cli_transport_ret_t rc = CLI_TCP_TRANSPORT_READ_ERR;

  if (ctx->rx_pos < ctx->rx_len) {
    rc = ctx->rx_buf[ctx->rx_pos++];
    if (ctx->rx_pos >= ctx->rx_len) {
      ctx->rx_pos = 0;
      ctx->rx_len = 0;
    }
  }

  return rc;
}

/**
 * @brief Write to a raw TCP stream transport.
 *
 * @param[in] raw_ctx  Raw TCP context pointer.
 * @param[in] buf      Source buffer.
 * @param[in] len      Number of bytes to send.
 *
 * @return Bytes sent, or @c -1 on error.
 */
static cli_transport_ret_t cli_tcp_transport_write(void *raw_ctx, const uint8_t *buf,
                                                   cli_transport_buflen_t len) {
  cli_tcp_ctx_struct_t *ctx = (cli_tcp_ctx_struct_t *)raw_ctx;
  return cli_tcp_send_all((cli_sock_t)ctx->fd, buf, len);
}

/**
 * @brief Flush callback for the raw TCP stream transport.
 *
 * @param[in] raw_ctx  Unused.
 *
 * @return Always @c 0.
 */
static cli_transport_ret_t cli_tcp_transport_flush(void *raw_ctx) {
  (void)raw_ctx;
  return 0;
}

/*################################### END OF FILE ######################################*/
