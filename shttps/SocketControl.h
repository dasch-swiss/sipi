/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SOCKETCONTROL_H
#define SIPI_SOCKETCONTROL_H

#include <csignal>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <unordered_set>
#include <vector>

#include <arpa/inet.h>//inet_addr
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
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

    SocketInfo(const SocketInfo &si)
    {
      type = si.type;
      socket_type = si.socket_type;
      sid = si.sid;
      ssl_sid = si.ssl_sid;
      sslctx = si.sslctx;
      for (int i = 0; i < INET6_ADDRSTRLEN; i++) peer_ip[i] = si.peer_ip[i];
      peer_port = si.peer_port;
    }

    explicit SocketInfo(const SIData &data)
    {
      type = data.type;
      socket_type = data.socket_type;
      sid = data.sid;
      ssl_sid = data.ssl_sid;
      sslctx = data.sslctx;
      for (int i = 0; i < INET6_ADDRSTRLEN; i++) peer_ip[i] = data.peer_ip[i];
      peer_port = data.peer_port;
    }


    SocketInfo &operator=(const SocketInfo &si)
    {
      if (this == &si) return *this;
      type = si.type;
      socket_type = si.socket_type;
      sid = si.sid;
      ssl_sid = si.ssl_sid;
      sslctx = si.sslctx;
      for (int i = 0; i < INET6_ADDRSTRLEN; i++) peer_ip[i] = si.peer_ip[i];
      peer_port = si.peer_port;
      return *this;
    }
  };

private:
  std::mutex sockets_mutex;//!> protecting mutex
  std::vector<pollfd> open_sockets;//!> open sockets waiting for reading
  std::vector<SocketInfo> generic_open_sockets;//!> open socket-info's waiting for reading
  std::queue<SocketInfo> waiting_sockets;//!> Sockets that have input and are waiting for the thread
  // std::unordered_set<SocketInfo, socket_info_hash> working_sockets; //!> Socket's that are currently working
  int n_msg_sockets;//!> Number of sockets communicating with the threads
  int stop_sock_id;//!> Index of the stopsocket (the thread that catches signals sens to this socket)
  int http_sock_id;//!> Index of the HTTP socckel
  int ssl_sock_id;//!> Index of the SSL socket
  int dyn_socket_base;//!> base index of the dynanic sockets created by accept

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

  int size() { return static_cast<int>(generic_open_sockets.size()); }

  inline const pollfd &operator[](int index) { return open_sockets[index]; };

  void remove(int pos, SocketInfo &sockid);


  void move_to_waiting(int pos);

  bool get_waiting(SocketInfo &sockid);

  static ssize_t send_control_message(int pipe_id, const SocketInfo &msg);

  static SocketInfo receive_control_message(int pipe_id);

  void broadcast_exit();

  void close_all_dynsocks(int (*closefunc)(const SocketInfo &));
};

}// namespace shttps


#endif// SIPI_SOCKETCONTROL_H
