/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SOCKETCONTROL_H
#define SIPI_SOCKETCONTROL_H

#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <vector>

#include <arpa/inet.h>//inet_addr
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "openssl/bio.h"
#include "openssl/err.h"
#include "openssl/ssl.h"

#include "Error.h"
#include "ThreadControl.h"


namespace shttps {


class SocketControl
{
public:
  enum ControlMessageType {
    NOOP,
    PROCESS_REQUEST,
    FINISHED_AND_CONTINUE,
    FINISHED_AND_CLOSE,
    SOCKET_CLOSED,
    EXIT,
    ERROR
  };
  enum SocketType { CONTROL_SOCKET, STOP_SOCKET, HTTP_SOCKET, SSL_SOCKET, DYN_SOCKET };

  struct SIData
  {
    ControlMessageType type{ NOOP };
    SocketType socket_type{ CONTROL_SOCKET };
    int sid{};
    SSL *ssl_sid{};
    SSL_CTX *sslctx{};
    char peer_ip[INET6_ADDRSTRLEN]{};
    int peer_port{};
    // Note: enqueue_time and last_activity are NOT part of SIData because
    // SIData is the IPC wire format sent over socketpairs between threads.
    // Time points are only meaningful within the event loop and are set fresh
    // when a socket enters the waiting queue or poll set.
  };

  class SocketInfo
  {
  public:
    ControlMessageType type;
    SocketType socket_type;
    int sid;
    SSL *ssl_sid;
    SSL_CTX *sslctx;
    char peer_ip[INET6_ADDRSTRLEN]{};
    int peer_port;
    std::chrono::steady_clock::time_point enqueue_time{};   //!< set when moved to waiting queue
    std::chrono::steady_clock::time_point last_activity{};  //!< set when added to poll set (for keep-alive)

    explicit SocketInfo(ControlMessageType type = NOOP,
      SocketType socket_type = CONTROL_SOCKET,
      int sid = -1,
      SSL *ssl_sid = nullptr,
      SSL_CTX *sslctx = nullptr,
      char *_peer_ip = nullptr,
      int peer_port = -1)
      : type(type), socket_type(socket_type), sid(sid), ssl_sid(ssl_sid), sslctx(sslctx), peer_port(peer_port)
    {
      if (_peer_ip == nullptr) {
        for (char &i : peer_ip) i = '\0';
      } else {
        strncpy(peer_ip, _peer_ip, INET6_ADDRSTRLEN - 1);
        peer_ip[INET6_ADDRSTRLEN - 1] = '\0';
      }
    }

    // Rule of Five: all members are trivially copyable (enums, ints, pointers,
    // char[], time_points), so compiler-generated copy/move is correct.
    SocketInfo(const SocketInfo &) = default;
    SocketInfo(SocketInfo &&) noexcept = default;
    SocketInfo &operator=(const SocketInfo &) = default;
    SocketInfo &operator=(SocketInfo &&) noexcept = default;
    ~SocketInfo() = default;// Non-owning — does not free SSL* or close fd

    explicit SocketInfo(const SIData &data)
      : type(data.type), socket_type(data.socket_type), sid(data.sid), ssl_sid(data.ssl_sid),
        sslctx(data.sslctx), peer_port(data.peer_port)
    // enqueue_time and last_activity intentionally left default-initialized (epoch).
    // SIData is the IPC wire format — time points are set fresh by add_dyn_socket/try_move_to_waiting.
    {
      std::memcpy(peer_ip, data.peer_ip, INET6_ADDRSTRLEN);
    }
  };

private:
  mutable std::mutex sockets_mutex;//!> protecting mutex
  std::vector<pollfd> open_sockets;//!> open sockets waiting for reading
  std::vector<SocketInfo> generic_open_sockets;//!> open socket-info's waiting for reading
  std::queue<SocketInfo> waiting_sockets;//!> Sockets that have input and are waiting for the thread
  int n_msg_sockets;//!> Number of sockets communicating with the threads
  int stop_sock_id;//!> Index of the stopsocket (the thread that catches signals sens to this socket)
  int http_sock_id;//!> Index of the HTTP socckel
  int ssl_sock_id;//!> Index of the SSL socket
  int dyn_socket_base;//!> base index of the dynanic sockets created by accept
  size_t max_waiting_connections{0};//!> Max queue size (0 = unlimited)
  unsigned queue_timeout_seconds{10}; //!> Max seconds a socket can wait in queue

public:
  /*!
   * Initialize the socket control.
   *
   * The SocketControl instance manages the sockets of the server. These consist of
   * internal sockets for the communication with the threads, and the HTTP sockets
   * from the requests from the outside.
   *
   * @param thread_control ThreadControl instance which is used to initialize the
   * sockets for the internal communication with the threads. The Initial sockets
   * are added to the idling sockets pool
   */
  explicit SocketControl(ThreadControl &thread_control);

  pollfd *get_sockets_arr();

  int get_sockets_size() const { return static_cast<int>(generic_open_sockets.size()); }

  int get_n_msg_sockets() const { return n_msg_sockets; }

  void add_stop_socket(int sid);

  int get_stop_socket_id() const { return stop_sock_id; }

  void add_http_socket(int sid);

  int get_http_socket_id() const { return http_sock_id; }

  void add_ssl_socket(int sid);

  int get_ssl_socket_id() const { return ssl_sock_id; }

  void add_dyn_socket(SocketInfo sockid);

  int get_dyn_socket_base() const { return dyn_socket_base; }

  void remove(int pos, SocketInfo &sockid);

  //! Dequeue a waiting socket, checking liveness. Dead sockets are closed via closefunc.
  //! Returns true if a live socket was found, false if queue is empty or all dead.
  bool get_waiting(SocketInfo &sockid, int (*closefunc)(const SocketInfo &));

  static ssize_t send_control_message(int pipe_id, const SocketInfo &msg);

  static SocketInfo receive_control_message(int pipe_id);

  void broadcast_exit();

  void close_all_dynsocks(int (*closefunc)(const SocketInfo &));

  void set_max_waiting_connections(size_t max) { max_waiting_connections = max; }

  void set_queue_timeout(unsigned seconds) { queue_timeout_seconds = seconds; }

  [[nodiscard]] size_t waiting_queue_size() const;

  //! Move socket to waiting queue. Returns false if queue is full.
  [[nodiscard]] bool try_move_to_waiting(int pos);

  //! Sweep waiting queue, collecting expired sockets.
  //! Returns vector of expired sockets to be closed by caller (outside any lock).
  [[nodiscard]] std::vector<SocketInfo> collect_expired_waiting();

  //! Drain all waiting sockets (used during shutdown).
  //! Returns vector of sockets to be closed by caller (outside any lock).
  [[nodiscard]] std::vector<SocketInfo> collect_all_waiting();

  //! Sweep idle DYN_SOCKETs older than keep_alive_timeout from poll set.
  //! Returns vector of reaped sockets to be closed by caller (outside any lock).
  //! Must be called between poll() iterations (not during iteration over the poll set).
  [[nodiscard]] std::vector<SocketInfo> collect_idle_dynsocks(int keep_alive_timeout_seconds);
};

}// namespace shttps


#endif// SIPI_SOCKETCONTROL_H
