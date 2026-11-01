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
#include "cli_env_detect.h"
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
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
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
/** @brief Erase Line command. */
#define TELNET_EL 248U
/** @brief Erase Character command. */
#define TELNET_EC 247U
/** @brief Are You There command. */
#define TELNET_AYT 246U

/** @brief Echo option. */
#define TELNET_OPT_ECHO 1U
/** @brief Suppress Go-Ahead option. */
#define TELNET_OPT_SGA 3U
/** @brief Negotiate About Window Size option. */
#define TELNET_OPT_NAWS 31U
/** @brief Linemode option. */
#define TELNET_OPT_LINEMODE 34U

/** @brief Our WILL ECHO is awaiting DO / DONT. */
#define TELNET_PENDING_ECHO 0x01U
/** @brief Our WILL SGA is awaiting DO / DONT. */
#define TELNET_PENDING_SGA 0x02U
/** @brief Our DO NAWS is awaiting WILL / WONT. */
#define TELNET_PENDING_NAWS 0x04U

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

#ifdef MSG_NOSIGNAL
/** @brief Send flags: report EPIPE instead of raising SIGPIPE (Linux / BSD). */
#define CLI_TELNET_SEND_FLAGS MSG_NOSIGNAL
#else
/** @brief macOS has no MSG_NOSIGNAL; SO_NOSIGPIPE is set on the socket instead. */
#define CLI_TELNET_SEND_FLAGS 0
#endif

/** @brief Socket send wrapper. */
#define CLI_TELNET_SOCK_WRITE(sock, buf, len)                                                      \
  send((sock), (const char *)(buf), (len), CLI_TELNET_SEND_FLAGS)
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
static bool cli_telnet_is_interrupted(void);
static int32_t cli_telnet_wait_readable(cli_sock_t sock, uint32_t timeout_ms);

static bool cli_telnet_option_update(cli_telnet_ctx_struct_t *ctx, bool *enabled, uint8_t bit,
                                     bool enable);
static void cli_telnet_respond(cli_telnet_ctx_struct_t *ctx, uint8_t verb, uint8_t opt);
static bool cli_telnet_process_byte(cli_telnet_ctx_struct_t *ctx, uint8_t byte, uint8_t *out);

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
  /* Options take effect only once the peer answers (RFC 1143); until then the requests
   * are pending. DONT LINEMODE asks for the default state and expects no answer. */
  ctx->pending = (uint8_t)(TELNET_PENDING_ECHO | TELNET_PENDING_SGA | TELNET_PENDING_NAWS);
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
        /* SO_REUSEADDR on Windows would let another process bind the same port. */
        BOOL yes = TRUE;
        (void)setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&yes, sizeof(yes));
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

    /* Reads wait in cli_telnet_wait_readable(); the send timeout stops a client
     * that never reads its output from blocking the single-threaded server forever. */
#if ENV_IS_WINDOWS
    {
      BOOL nd = TRUE;
      DWORD snd_to = (DWORD)CLI_TELNET_SEND_TIMEOUT_MS;
      (void)setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char *)&nd, sizeof(nd));
      (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&snd_to, sizeof(snd_to));
    }
#else
    {
      int32_t nd = 1;
      struct timeval tv;
      tv.tv_sec = CLI_TELNET_SEND_TIMEOUT_MS / 1000;
      tv.tv_usec = (CLI_TELNET_SEND_TIMEOUT_MS % 1000) * 1000;
      (void)setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));
      (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
      int32_t nosig = 1;
      (void)setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif
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
    int32_t ready = 0;

    if (room > 0) {
      ready = cli_telnet_wait_readable((cli_sock_t)ctx->fd, (uint32_t)CLI_TRANSPORT_POLL_MS);
      if (ready < 0) {
        result = CLI_ERR;
      }
    }

    if (ready > 0) {
      cli_transport_ret_t received =
          CLI_TELNET_SOCK_READ((cli_sock_t)ctx->fd, &ctx->rx_buf[ctx->rx_len], room);

      if (received < 0) {
        if (!cli_telnet_is_interrupted()) {
          result = CLI_ERR;
        }
      } else if (received == 0) {
        result = CLI_ERR;
      } else {
        uint16_t end = ctx->rx_len + (uint16_t)received;
        uint16_t wp = ctx->rx_len;
        for (uint16_t i = ctx->rx_len; i < end && wp < sizeof(ctx->rx_buf); i++) {
          uint8_t out;
          if (cli_telnet_process_byte(ctx, ctx->rx_buf[i], &out)) {
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

/**
 * @brief Return whether the last socket call was interrupted by a signal.
 *
 * @return @c true when the call should simply be repeated.
 */
static bool cli_telnet_is_interrupted(void) {
#if ENV_IS_WINDOWS
  return WSAGetLastError() == WSAEINTR;
#else
  return errno == EINTR;
#endif
}

/**
 * @brief Wait until @p sock is readable or @p timeout_ms elapses.
 *
 * @param[in] sock        Connected socket.
 * @param[in] timeout_ms  Maximum wait in milliseconds.
 *
 * @return 1 if readable, 0 on timeout or signal interruption, -1 on error.
 */
static int32_t cli_telnet_wait_readable(cli_sock_t sock, uint32_t timeout_ms) {
  int32_t ready;

#if ENV_IS_WINDOWS
  /* WinSock's fd_set is a list of handles (no FD_SETSIZE bitmap limit); nfds is ignored. */
  fd_set rfds;
  struct timeval tv;

  FD_ZERO(&rfds);
  FD_SET(sock, &rfds);
  tv.tv_sec = (long)(timeout_ms / 1000U);
  tv.tv_usec = (long)((timeout_ms % 1000U) * 1000U);
  ready = select(0, &rfds, NULL, NULL, &tv);
#else
  /* poll() works for any descriptor number; select() is undefined at fd >= FD_SETSIZE. */
  struct pollfd pfd;

  pfd.fd = sock;
  pfd.events = POLLIN;
  pfd.revents = 0;
  ready = poll(&pfd, 1, (int)timeout_ms);
  if (ready > 0 && (pfd.revents & POLLNVAL) != 0) {
    ready = -1;
    errno = EBADF;
  }
#endif
  if (ready < 0 && cli_telnet_is_interrupted()) {
    ready = 0;
  }

  return (ready > 0) ? 1 : ((ready == 0) ? 0 : -1);
}

/**
 * @brief Transmit all bytes over the socket, blocking until complete.
 *
 * Used for protocol negotiation and escaping IAC bytes. Signal interruptions
 * are retried; a send timeout (@c CLI_TELNET_SEND_TIMEOUT_MS) or a closed peer
 * is an error.
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
    int32_t rc = (int32_t)CLI_TELNET_SOCK_WRITE(sock, &buf[sent], len - sent);
    if (rc < 0 && cli_telnet_is_interrupted()) {
      continue;
    }
    if (rc <= 0) {
      error = true;
    } else {
      sent += (cli_transport_buflen_t)rc;
    }
  }

  if (error) {
    sent = CLI_TELNET_ERR;
  }

  return sent;
}

/* Telnet Protocol Helpers --------------------------------------------------------------*/

/**
 * @brief Apply a peer's answer or request for one option we track (RFC 1143).
 *
 * @param[in,out] ctx      Telnet context (pending request bits).
 * @param[in,out] enabled  The option's current state.
 * @param[in]     bit      The option's @c TELNET_PENDING_* bit.
 * @param[in]     enable   @c true for DO / WILL, @c false for DONT / WONT.
 *
 * @return @c true when a reply must be sent (a new request that changes the state);
 *         answers to our own pending requests and redundant requests get none.
 */
static bool cli_telnet_option_update(cli_telnet_ctx_struct_t *ctx, bool *enabled, uint8_t bit,
                                     bool enable) {
  bool reply = false;

  if ((ctx->pending & bit) != 0U) {
    /* Answer to our request: accept the peer's decision, never acknowledge it. */
    ctx->pending = (uint8_t)(ctx->pending & (uint8_t)~bit);
    *enabled = enable;
  } else if (*enabled != enable) {
    /* The peer asks for a change: agree, and confirm it. */
    *enabled = enable;
    reply = true;
  } else {
    /* Already in the requested state: no reply (prevents negotiation loops). */
  }

  return reply;
}

/**
 * @brief Respond to an incoming IAC option negotiation request.
 *
 * ECHO and SGA (performed here) and NAWS (performed by the peer) are supported
 * and tracked; every other option stays disabled: a request to enable it is
 * refused, a request to disable it needs no reply.
 *
 * @param[in] ctx   Telnet context carrying the socket fd.
 * @param[in] verb  Telnet verb byte.
 * @param[in] opt   Telnet option byte.
 */
static void cli_telnet_respond(cli_telnet_ctx_struct_t *ctx, uint8_t verb, uint8_t opt) {
  uint8_t response[3];
  bool send_response = false;
  bool enable = (verb == TELNET_DO || verb == TELNET_WILL);

  response[0] = TELNET_IAC;

  switch (verb) {
  case TELNET_DO:
  case TELNET_DONT:
    /* Options this end would perform. */
    if (opt == TELNET_OPT_ECHO) {
      send_response = cli_telnet_option_update(ctx, &ctx->local_echo, TELNET_PENDING_ECHO, enable);
    } else if (opt == TELNET_OPT_SGA) {
      send_response = cli_telnet_option_update(ctx, &ctx->local_sga, TELNET_PENDING_SGA, enable);
    } else {
      send_response = enable; /* refuse DO; DONT for a disabled option needs no reply */
      enable = false;
    }
    response[1] = enable ? TELNET_WILL : TELNET_WONT;
    break;
  case TELNET_WILL:
  case TELNET_WONT:
    /* Options the peer would perform. */
    if (opt == TELNET_OPT_NAWS) {
      send_response = cli_telnet_option_update(ctx, &ctx->remote_naws, TELNET_PENDING_NAWS, enable);
    } else {
      send_response = enable; /* refuse WILL (incl. LINEMODE); WONT needs no reply */
      enable = false;
    }
    response[1] = enable ? TELNET_DO : TELNET_DONT;
    break;
  default:
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
 * responses via @c cli_telnet_respond().
 *
 * @param[in,out] ctx  Telnet context (state machine state + rx buffer).
 * @param[in]     byte The next byte from the raw socket read.
 */
static bool cli_telnet_process_byte(cli_telnet_ctx_struct_t *ctx, uint8_t byte, uint8_t *out) {
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
      ctx->sb_len = 0U;
    } else {
      /* Editing commands become the line editor's keys (Backspace, Ctrl-U); "are you there"
       * is answered with a bell, which leaves the line being edited intact. Others (NOP, GA,
       * IP, AO, BRK, DM) mean nothing to a line editor. */
      if (byte == TELNET_EC) {
        *out = 0x08U;
        stored = true;
      } else if (byte == TELNET_EL) {
        *out = 0x15U;
        stored = true;
      } else if (byte == TELNET_AYT) {
        static const uint8_t bell = 0x07U;
        (void)cli_telnet_send_all((cli_sock_t)ctx->fd, &bell, 1);
      } else {
        /* Ignored. */
      }
      ctx->state = TELNET_STATE_NORMAL;
    }
    break;

  case TELNET_STATE_VERB:
    cli_telnet_respond(ctx, ctx->verb, byte);
    ctx->state = TELNET_STATE_NORMAL;
    break;

  case TELNET_STATE_SB:
    if (byte == TELNET_IAC) {
      ctx->state = TELNET_STATE_SB_IAC;
    } else if (++ctx->sb_len > CLI_TELNET_MAX_SB_LEN) {
      /* No IAC SE in sight (broken client): give up on the block so input is not
       * swallowed forever. The remaining bytes are treated as normal data. */
      ctx->state = TELNET_STATE_NORMAL;
    } else {
      /* Sub-negotiation payload (e.g. NAWS size) is not used. */
    }
    break;

  case TELNET_STATE_SB_IAC:
    if (byte == TELNET_SE) {
      ctx->state = TELNET_STATE_NORMAL;
    } else if (byte == TELNET_IAC) {
      ctx->state = TELNET_STATE_SB; /* escaped 0xFF inside the payload */
    } else {
      /* IAC <command> inside SB: the block was never terminated; resume normal parsing. */
      ctx->state = TELNET_STATE_IAC;
      stored = cli_telnet_process_byte(ctx, byte, out);
    }
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

  if (ctx) {
    if (ctx->rx_pos < ctx->rx_len) {
      result = (cli_transport_ret_t)ctx->rx_buf[ctx->rx_pos++];
      if (ctx->rx_pos >= ctx->rx_len) {
        ctx->rx_pos = 0;
        ctx->rx_len = 0;
      }
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