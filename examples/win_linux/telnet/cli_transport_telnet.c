/**
 * @file cli_transport_telnet.c
 * @brief Telnet transport layer implementation.
 *
 * Handles RFC 854 IAC option negotiation and strips Telnet protocol bytes
 * from the incoming stream before they reach the CLI engine. Supports both
 * POSIX sockets and Winsock. On Arduino the public functions are compiled as
 * no-ops.
 *
 * @copyright Copyright (c) 2026 Muhammad Hassaan Shah.
 *
 * SPDX-License-Identifier: MIT
 */

/*=======================================================================================
 * Includes
 *=======================================================================================*/
#include <cli_env_detect.h>
#include "cli_transport_telnet.h"

#include <stdio.h>
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
#elif ENV_IS_UNIX_LIKE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#endif

/*=======================================================================================
 * Private Defines
 *=======================================================================================*/

/** @brief Interpret As Command prefix byte. */
#define TELNET_IAC 255U
/** @brief Request that the other party not perform an option. */
#define TELNET_DONT 254U
/** @brief Request that the other party perform an option. */
#define TELNET_DO 253U
/** @brief Refuse to perform an option. */
#define TELNET_WONT 252U
/** @brief Agree to perform an option. */
#define TELNET_WILL 251U
/** @brief Begin sub-negotiation block. */
#define TELNET_SB 250U
/** @brief End sub-negotiation block. */
#define TELNET_SE 240U

/** @brief Echo option. */
#define TELNET_OPT_ECHO 1U
/** @brief Suppress Go-Ahead option. */
#define TELNET_OPT_SGA 3U
/** @brief Negotiate About Window Size option. */
#define TELNET_OPT_NAWS 31U
/** @brief Linemode option. */
#define TELNET_OPT_LINEMODE 34U

/** @brief IAC parser is waiting for regular data. */
#define TELNET_STATE_NORMAL 0U
/** @brief IAC parser received IAC and is waiting for a verb. */
#define TELNET_STATE_IAC 1U
/** @brief IAC parser received a verb and is waiting for an option. */
#define TELNET_STATE_VERB 2U
/** @brief IAC parser is consuming sub-negotiation data. */
#define TELNET_STATE_SB 3U
/** @brief IAC parser saw IAC inside sub-negotiation. */
#define TELNET_STATE_SB_IAC 4U

#if ENV_IS_WINDOWS
#define CLI_TELNET_INVALID_NATIVE_SOCKET INVALID_SOCKET
#else
#define CLI_TELNET_INVALID_NATIVE_SOCKET (-1)
#endif

/*=======================================================================================
 * Private Macros
 *=======================================================================================*/

#if ENV_IS_WINDOWS
/** @brief Close a TCP socket on Windows. */
#define CLI_TCP_CLOSE_SOCKET(socket_fd) closesocket(socket_fd)

/** @brief Socket receive wrapper. */
#define CLI_TELNET_SOCK_READ(sock, buf, len) recv((sock), (char *)(buf), (len), 0)

/** @brief Socket send wrapper. */
#define CLI_TELNET_SOCK_WRITE(sock, buf, len) send((sock), (const char *)(buf), (len), 0)

#elif ENV_IS_UNIX_LIKE
/** @brief Close a TCP socket on POSIX. */
#define CLI_TCP_CLOSE_SOCKET(socket_fd)       close(socket_fd)

/** @brief Socket receive wrapper. */
#define CLI_TELNET_SOCK_READ(sock, buf, len)  read((sock), (buf), (len))

/** @brief Socket send wrapper. */
#define CLI_TELNET_SOCK_WRITE(sock, buf, len) send((sock), (const char *)(buf), (len), 0)
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

/** @brief Initial IAC option bundle transmitted by @c cli_telnet_negotiate(). */
static const uint8_t s_telnet_negotiation[] = {
    TELNET_IAC, TELNET_WILL, TELNET_OPT_ECHO,     TELNET_IAC, TELNET_WILL, TELNET_OPT_SGA,
    TELNET_IAC, TELNET_DONT, TELNET_OPT_LINEMODE, TELNET_IAC, TELNET_DO,   TELNET_OPT_NAWS,
};

/*=======================================================================================
 * Private Function Prototypes
 *=======================================================================================*/

#if ENV_IS_WINDOWS
static int32_t cli_telnet_winsock_init(void);
#endif
static int32_t cli_set_recv_timeout_ms(cli_sock_t sock, int32_t timeout_ms);

static void telnet_respond(cli_telnet_ctx_struct_t *ctx, uint8_t verb, uint8_t opt);
static bool telnet_process_byte(cli_telnet_ctx_struct_t *ctx, uint8_t byte, uint8_t *out);

static cli_transport_ret_t cli_telnet_transport_available(void *raw_ctx);
static cli_transport_ret_t cli_telnet_transport_read(void *raw_ctx);
static cli_transport_ret_t cli_telnet_transport_write(void *raw_ctx, const uint8_t *buf,
                                                      cli_transport_buflen_t len);
static cli_transport_ret_t cli_telnet_transport_flush(void *raw_ctx);

static cli_transport_ret_t cli_telnet_send_all(cli_sock_t sock, const uint8_t *buf,
                                               cli_transport_buflen_t len);

/*=======================================================================================
 * Public Functions
 *=======================================================================================*/

void cli_telnet_init(cli_telnet_ctx_struct_t *ctx, cli_transport_struct_t *transport, int32_t fd) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->fd = fd;
  ctx->state = TELNET_STATE_NORMAL;

  cli_set_recv_timeout_ms((cli_sock_t)ctx->fd, CLI_TRANSPORT_POLL_MS);
  /*
  #if ENV_IS_WINDOWS
          {
            u_long mode = 1;
            ioctlsocket((cli_sock_t)fd, FIONBIO, &mode);
          }
  #else
          {
            int32_t flags = fcntl((cli_sock_t)fd, F_GETFL, 0);
            fcntl((cli_sock_t)fd, F_SETFL, flags | O_NONBLOCK);
          }
  #endif
  */

  transport->available = cli_telnet_transport_available;
  transport->read = cli_telnet_transport_read;
  transport->write = cli_telnet_transport_write;
  transport->flush = cli_telnet_transport_flush;
  transport->ctx = ctx;
  transport->kind = CLI_TRANSPORT_TELNET;
}

void cli_telnet_negotiate(cli_telnet_ctx_struct_t *ctx) {
  (void)cli_telnet_send_all((cli_sock_t)ctx->fd, s_telnet_negotiation,
                            sizeof(s_telnet_negotiation));
  ctx->negotiated = true;
  ctx->local_echo = true;
  ctx->local_sga = true;
  ctx->remote_linemode_rejected = true;
  ctx->remote_naws = true;
}

int32_t cli_telnet_listen(uint16_t port) {
  int32_t result = CLI_TELNET_INVALID_SOCKET;

#if ENV_IS_WINDOWS
  bool init_ok = (cli_telnet_winsock_init() == CLI_TELNET_OK);
#else
  bool init_ok = true;
#endif

  if (init_ok) {
    cli_sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock != CLI_TELNET_INVALID_NATIVE_SOCKET) {

#if ENV_IS_WINDOWS
      {
        BOOL yes = TRUE;
        (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
      }
#else
      {
        int32_t yes = 1;
        (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
      }
#endif

      struct sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      addr.sin_port = htons(port);

      if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(sock, 4) != 0) {
        CLI_TCP_CLOSE_SOCKET(sock);
      } else {
        result = (int32_t)sock;
      }
    }
  }

  return result;
}

int32_t cli_telnet_accept(int32_t listen_fd) {
  int32_t result = CLI_TELNET_INVALID_SOCKET;

  struct sockaddr_in peer;
#if ENV_IS_WINDOWS
  int32_t peer_len = (int32_t)sizeof(peer);
#else
  socklen_t peer_len = (socklen_t)sizeof(peer);
#endif

  cli_sock_t client = accept((cli_sock_t)listen_fd, (struct sockaddr *)&peer, &peer_len);
  if (client != CLI_TELNET_INVALID_NATIVE_SOCKET) {

#if ENV_IS_WINDOWS
    {
      BOOL nd = TRUE;
      DWORD rcv_to = (DWORD)CLI_TRANSPORT_POLL_MS;
      (void)setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char *)&nd, sizeof(nd));
      (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&rcv_to, sizeof(rcv_to));
    }
#else
    {
      int32_t nd = 1;
      struct timeval tv;
      tv.tv_sec = CLI_TRANSPORT_POLL_MS / 1000;
      tv.tv_usec = (CLI_TRANSPORT_POLL_MS % 1000) * 1000;
      (void)setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));
      (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
#endif
    result = (int32_t)client;
  }

  return result;
}

void cli_telnet_close(int32_t fd) {
  if (fd >= 0) {
    CLI_TCP_CLOSE_SOCKET((cli_sock_t)fd);
  }
}

int8_t cli_telnet_handler(cli_telnet_ctx_struct_t *ctx) {
  int8_t result = CLI_OK;

  if (ctx->rx_pos >= ctx->rx_len) {
    cli_transport_buflen_t room = (cli_transport_buflen_t)(sizeof(ctx->rx_buf) - ctx->rx_len);
    if (room > 0) {

      cli_transport_ret_t received =
          CLI_TELNET_SOCK_READ((cli_sock_t)ctx->fd, &ctx->rx_buf[ctx->rx_len], room);

      if (received < 0) {
#if ENV_IS_WINDOWS
        int32_t error = WSAGetLastError();
        if (!(error == WSAETIMEDOUT || error == WSAEWOULDBLOCK || error == WSAEINTR)) {
          result = CLI_ERR;
        }
#else
        if (!(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
          result = CLI_ERR;
        }
#endif
      } else if (received == 0) {
        result = CLI_ERR;
      } else {
        uint16_t end = ctx->rx_len + (uint16_t)received;
        uint16_t wp = ctx->rx_len;
        for (uint16_t i = ctx->rx_len; i < end && wp < sizeof(ctx->rx_buf); i++) {
          uint8_t out;
          if (telnet_process_byte(ctx, ctx->rx_buf[i], &out)) {
            ctx->rx_buf[wp++] = out;
          }
        }
        ctx->rx_len = wp;
      }
    }
  }

  return result;
}

/*=======================================================================================
 * Private Functions
 *=======================================================================================*/

/* Socket Helpers -----------------------------------------------------------------------*/

#if ENV_IS_WINDOWS
/**
 * @brief Initialise Winsock for the Telnet transport.
 *
 * Ensures @c WSAStartup is called once per application lifecycle.
 *
 * @return @c CLI_TELNET_OK on success, @c CLI_TELNET_ERR on failure.
 */
static int32_t cli_telnet_winsock_init(void) {
  static bool initialized = false;
  int32_t rc = CLI_TELNET_OK;

  if (!initialized) {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
      rc = CLI_TELNET_ERR;
    } else {
      initialized = true;
    }
  }

  return rc;
}
#endif

static int32_t cli_set_recv_timeout_ms(cli_sock_t sock, int32_t timeout_ms) {
  int32_t rc = 0;

#if ENV_IS_WINDOWS
  DWORD ms = (DWORD)timeout_ms;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, (int32_t)sizeof(ms)) != 0) {
    rc = -1;
  }
#else
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, (socklen_t)sizeof(tv)) != 0) {
    rc = -1;
  }
#endif

  return rc;
}

/**
 * @brief Transmit all bytes over the socket, blocking until complete.
 *
 * Used for protocol negotiation and escaping IAC bytes.
 *
 * @param[in] sock  Socket file descriptor.
 * @param[in] buf   Data buffer to transmit.
 * @param[in] len   Number of bytes to transmit.
 *
 * @return Total bytes sent on success, or @c CLI_TELNET_ERR on failure.
 */
static cli_transport_ret_t cli_telnet_send_all(cli_sock_t sock, const uint8_t *buf,
                                               cli_transport_buflen_t len) {
  cli_transport_buflen_t sent = 0;
  bool error = false;

  while (sent < len && !error) {
    int32_t rc = CLI_TELNET_SOCK_WRITE(sock, &buf[sent], len - sent);
    if (rc <= 0) {
      error = true;
    } else {
      sent += rc;
    }
  }

  if (error) {
    sent = CLI_TELNET_ERR;
  }

  return sent;
}

/* Telnet Protocol Helpers --------------------------------------------------------------*/

/**
 * @brief Respond to an incoming IAC option negotiation request.
 *
 * @param[in] ctx   Telnet context carrying the socket fd.
 * @param[in] verb  Telnet verb byte.
 * @param[in] opt   Telnet option byte.
 */
static void telnet_respond(cli_telnet_ctx_struct_t *ctx, uint8_t verb, uint8_t opt) {
  uint8_t response[3];
  bool send_response = true;

  response[0] = TELNET_IAC;

  switch (verb) {
  case TELNET_DO:
    if (opt == TELNET_OPT_ECHO) {
      if (ctx->local_echo) {
        send_response = false;
      } else {
        ctx->local_echo = true;
        response[1] = TELNET_WILL;
      }
    } else if (opt == TELNET_OPT_SGA) {
      if (ctx->local_sga) {
        send_response = false;
      } else {
        ctx->local_sga = true;
        response[1] = TELNET_WILL;
      }
    } else {
      response[1] = TELNET_WONT;
    }
    break;
  case TELNET_DONT:
    if (opt == TELNET_OPT_ECHO && ctx->local_echo) {
      ctx->local_echo = false;
      response[1] = TELNET_WONT;
    } else if (opt == TELNET_OPT_SGA && ctx->local_sga) {
      ctx->local_sga = false;
      response[1] = TELNET_WONT;
    } else {
      send_response = false;
    }
    break;
  case TELNET_WILL:
    if (opt == TELNET_OPT_NAWS) {
      if (ctx->remote_naws) {
        send_response = false;
      } else {
        ctx->remote_naws = true;
        response[1] = TELNET_DO;
      }
    } else if (opt == TELNET_OPT_LINEMODE && ctx->remote_linemode_rejected) {
      send_response = false;
    } else {
      if (opt == TELNET_OPT_LINEMODE) {
        ctx->remote_linemode_rejected = true;
      }
      response[1] = TELNET_DONT;
    }
    break;
  case TELNET_WONT:
    if (opt == TELNET_OPT_NAWS && ctx->remote_naws) {
      ctx->remote_naws = false;
      response[1] = TELNET_DONT;
    } else {
      send_response = false;
    }
    break;
  default:
    send_response = false;
    break;
  }

  if (send_response) {
    response[2] = opt;
    (void)cli_telnet_send_all((cli_sock_t)ctx->fd, response, 3);
  }
}

/**
 * @brief Process one byte through the IAC state machine.
 *
 * Feeds a single raw byte into the telnet state machine, which either
 * stores decoded user data into the rx buffer or triggers IAC negotiation
 * responses via @c telnet_respond().
 *
 * @param[in,out] ctx  Telnet context (state machine state + rx buffer).
 * @param[in]     byte The next byte from the raw socket read.
 */
static bool telnet_process_byte(cli_telnet_ctx_struct_t *ctx, uint8_t byte, uint8_t *out) {
  bool stored = false;

  switch (ctx->state) {
  case TELNET_STATE_NORMAL:
    if (byte == TELNET_IAC) {
      ctx->state = TELNET_STATE_IAC;
    } else if (byte != '\0') {
      *out = byte;
      stored = true;
    }
    break;

  case TELNET_STATE_IAC:
    if (byte == TELNET_IAC) {
      *out = 0xFF;
      ctx->state = TELNET_STATE_NORMAL;
      stored = true;
    } else if (byte == TELNET_DO || byte == TELNET_DONT || byte == TELNET_WILL ||
               byte == TELNET_WONT) {
      ctx->verb = byte;
      ctx->state = TELNET_STATE_VERB;
    } else if (byte == TELNET_SB) {
      ctx->state = TELNET_STATE_SB;
    } else {
      ctx->state = TELNET_STATE_NORMAL;
    }
    break;

  case TELNET_STATE_VERB:
    telnet_respond(ctx, ctx->verb, byte);
    ctx->state = TELNET_STATE_NORMAL;
    break;

  case TELNET_STATE_SB:
    if (byte == TELNET_IAC) {
      ctx->state = TELNET_STATE_SB_IAC;
    }
    break;

  case TELNET_STATE_SB_IAC:
    ctx->state = (byte == TELNET_SE) ? TELNET_STATE_NORMAL : TELNET_STATE_SB;
    break;

  default:
    ctx->state = TELNET_STATE_NORMAL;
    break;
  }

  return stored;
}

/* Transport Callbacks ------------------------------------------------------------------*/

/**
 * @brief Check whether at least one decoded Telnet payload byte is ready.
 *
 * Returns 1 immediately when the internal rx buffer still has unconsumed
 * bytes.  When the buffer is empty, calls @c cli_telnet_handler() to pump
 * raw socket data through the IAC state machine.  If the handler refills the
 * buffer, returns 1; if the handler reports a disconnect or fatal socket
 * error, returns @c CLI_TELNET_ERR.
 *
 * This is the Stream-style readiness check the engine calls it first
 * before every @c cli_telnet_transport_read() to decide whether to loop.
 *
 * @param[in] raw_ctx  Telnet context pointer.
 *
 * @return 1 when at least one decoded byte is available,
 *         0 when no data is ready (nonâ€‘blocking),
 *         @c CLI_TELNET_ERR on disconnect or fatal error.
 */
static cli_transport_ret_t cli_telnet_transport_available(void *raw_ctx) {
  cli_telnet_ctx_struct_t *ctx = (cli_telnet_ctx_struct_t *)raw_ctx;
  cli_transport_ret_t result = 0;

  if (ctx->rx_pos < ctx->rx_len) {
    result = 1;
  } else {
    int8_t handler_rc = cli_telnet_handler(ctx);
    if (handler_rc != CLI_OK) {
      result = CLI_TELNET_ERR;
    } else if (ctx->rx_pos < ctx->rx_len) {
      result = 1;
    }
  }

  return result;
}

/**
 * @brief Read one decoded Telnet payload byte.
 *
 * Must only be called when @c cli_telnet_transport_available() has returned a
 * positive value.  Consumes one byte from the internal rx buffer and advances
 * the read cursor.  When the last byte is consumed the buffer is reset
 * (rx_pos = rx_len = 0) so the next @c available() call will refill it.
 *
 * @param[in] raw_ctx  Telnet context pointer.
 *
 * @return The next decoded user byte (0..255), or @c CLI_TELNET_ERR if the
 *         buffer is unexpectedly empty.
 */
static cli_transport_ret_t cli_telnet_transport_read(void *raw_ctx) {
  cli_telnet_ctx_struct_t *ctx = (cli_telnet_ctx_struct_t *)raw_ctx;
  cli_transport_ret_t result = CLI_TELNET_ERR;

  if (ctx->rx_pos < ctx->rx_len) {
    result = ctx->rx_buf[ctx->rx_pos++];
    if (ctx->rx_pos >= ctx->rx_len) {
      ctx->rx_pos = 0;
      ctx->rx_len = 0;
    }
  }

  return result;
}

/**
 * @brief Send bytes over Telnet, escaping literal @c 0xFF values.
 *
 * @param[in] raw_ctx  Telnet context pointer.
 * @param[in] buf      User payload to transmit.
 * @param[in] len      Number of bytes to transmit.
 *
 * @return Logical payload bytes sent, or @c -1 on error.
 */
static cli_transport_ret_t cli_telnet_transport_write(void *raw_ctx, const uint8_t *buf,
                                                      cli_transport_buflen_t len) {
  cli_telnet_ctx_struct_t *ctx = (cli_telnet_ctx_struct_t *)raw_ctx;
  cli_sock_t sock = (cli_sock_t)ctx->fd;
  cli_transport_ret_t rc = len;
  cli_transport_buflen_t start = 0;

  for (cli_transport_buflen_t i = 0; i < len && rc >= 0; i++) {
    if (buf[i] == 0xFF) {
      if (i > start) {
        cli_transport_buflen_t span_len = i - start;
        if (cli_telnet_send_all(sock, &buf[start], span_len) < 0) {
          rc = CLI_TELNET_ERR;
        }
      }

      if (rc >= 0) {
        static const uint8_t iac_escaped[2] = {0xFF, 0xFF};
        if (cli_telnet_send_all(sock, iac_escaped, 2) < 0) {
          rc = CLI_TELNET_ERR;
        }
      }

      start = i + 1;
    }
  }

  if (rc >= 0 && start < len) {
    cli_transport_buflen_t tail_len = len - start;
    if (cli_telnet_send_all(sock, &buf[start], tail_len) < 0) {
      rc = CLI_TELNET_ERR;
    }
  }

  return rc;
}

/**
 * @brief Flush callback for Telnet transport.
 *
 * @param[in] raw_ctx  Unused.
 *
 * @return Always @c 0 because @c TCP_NODELAY already minimizes buffering.
 */
static cli_transport_ret_t cli_telnet_transport_flush(void *raw_ctx) {
  (void)raw_ctx;
  return 0;
}

/*################################### END OF FILE ######################################*/
