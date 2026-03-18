/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <algorithm>

#include "SocketControl.h"

namespace shttps {

SocketControl::SocketControl(ThreadControl &thread_control)
{
  for (int i = 0; i < thread_control.nthreads(); i++) {
    generic_open_sockets.emplace_back(NOOP, CONTROL_SOCKET, thread_control[i].control_pipe);
  }
  n_msg_sockets = thread_control.nthreads();
  stop_sock_id = -1;
  http_sock_id = -1;
  ssl_sock_id = -1;
  dyn_socket_base = -1;
}
//=========================================================================

pollfd *SocketControl::get_sockets_arr()
{
  open_sockets.clear();
  for (auto const &tmp : generic_open_sockets) { open_sockets.push_back({ tmp.sid, POLLIN, 0 }); }
  return open_sockets.data();
}

void SocketControl::add_stop_socket(int sid)
{// only called once
  std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
  if (dyn_socket_base != -1) {
    throw Error("Adding stop socket not allowed after adding dynamic sockets!");
  }
  generic_open_sockets.emplace_back(NOOP, STOP_SOCKET, sid);
  stop_sock_id = static_cast<int>(generic_open_sockets.size() - 1);
}
//=========================================================================

void SocketControl::add_http_socket(int sid)
{// only called once
  std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
  if (dyn_socket_base != -1) {
    throw Error("Adding HTTP socket not allowed after adding dynamic sockets!");
  }
  generic_open_sockets.emplace_back(NOOP, HTTP_SOCKET, sid);
  http_sock_id = static_cast<int>(generic_open_sockets.size() - 1);
}
//=========================================================================

void SocketControl::add_ssl_socket(int sid)
{// only called once
  std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
  if (dyn_socket_base != -1) {
    throw Error("Adding SSL socket not allowed after adding dynamic sockets!");
  }
  generic_open_sockets.emplace_back(NOOP, SSL_SOCKET, sid, nullptr);
  ssl_sock_id = static_cast<int>(generic_open_sockets.size() - 1);
}
//=========================================================================

void SocketControl::add_dyn_socket(SocketInfo sockid)
{// called multiple times, changes open_sockets vector!!!
  std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
  sockid.type = NOOP;
  sockid.socket_type = DYN_SOCKET;
  sockid.last_activity = std::chrono::steady_clock::now();
  generic_open_sockets.push_back(sockid);
  if (dyn_socket_base == -1) { dyn_socket_base = static_cast<int>(generic_open_sockets.size() - 1); }
}
//=========================================================================

void SocketControl::remove(const int pos, SocketInfo &sockid)
{// called multiple times, changes open sockets vector!!!
  std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
  if ((pos >= 0) && (pos < generic_open_sockets.size())) {
    sockid = generic_open_sockets[pos];
    generic_open_sockets.erase(generic_open_sockets.begin() + pos);
  } else {
    throw Error("Socket index out of range!");
  }
  if (pos < n_msg_sockets) {// we removed a thread socket, therefore we have to decrement all position ids
    n_msg_sockets--;
    stop_sock_id--;
    http_sock_id--;
    ssl_sock_id--;
    dyn_socket_base--;
  } else if (pos == http_sock_id) {
    http_sock_id = -1;
    ssl_sock_id--;
    dyn_socket_base--;
  } else if (pos == ssl_sock_id) {
    ssl_sock_id = -1;
    dyn_socket_base--;
  }
}

bool SocketControl::get_waiting(SocketInfo &sockid, int (*closefunc)(const SocketInfo &))
{
  // Collect dead sockets under the lock, close them after releasing it.
  // All I/O (closefunc → SSL_shutdown, shutdown, close) happens outside the lock.
  std::vector<SocketInfo> dead_sockets;
  bool found = false;
  {
    std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
    while (!waiting_sockets.empty()) {
      sockid = waiting_sockets.front();
      waiting_sockets.pop();
      // Liveness check: verify the TCP connection is still alive
      struct pollfd pfd = {sockid.sid, POLLIN | POLLHUP, 0};
      if (poll(&pfd, 1, 0) >= 0 && !(pfd.revents & (POLLHUP | POLLERR))) {
        found = true;
        break;
      }
      dead_sockets.push_back(sockid);
    }
  }
  // Close dead sockets outside the lock (SSL_shutdown can block)
  for (const auto &dead : dead_sockets) { (void)closefunc(dead); }
  return found;
}
//=========================================================================§

ssize_t SocketControl::send_control_message(int pipe_id, const SocketInfo &msg)
{
  SIData data{};
  data.type = msg.type;
  data.socket_type = msg.socket_type;
  data.sid = msg.sid;
  data.ssl_sid = msg.ssl_sid;
  data.sslctx = msg.sslctx;
  std::memcpy(data.peer_ip, msg.peer_ip, INET6_ADDRSTRLEN);
  data.peer_port = msg.peer_port;
  return ::send(pipe_id, &data, sizeof(SIData), 0);
}
//=========================================================================§

SocketControl::SocketInfo SocketControl::receive_control_message(int pipe_id)
{
  SIData data{};
  ssize_t n;
  if ((n = ::read(pipe_id, &data, sizeof(SIData))) != sizeof(SIData)) {
    data.type = ERROR;
    std::cerr << "==> receive_control_message: received only " << n << " bytes!!" << '\n';
  }
  return SocketInfo(data);
}
//=========================================================================§

void SocketControl::broadcast_exit()
{
  SIData data{};
  data.type = EXIT;
  data.socket_type = CONTROL_SOCKET;
  data.sid = 0;
  for (int i = 0; i < n_msg_sockets; i++) {
    data.sid = generic_open_sockets[i].sid;
    ::send(generic_open_sockets[i].sid, &data, sizeof(SIData), 0);
  }
}

void SocketControl::close_all_dynsocks(int (*closefunc)(const SocketInfo &))
{
  std::vector<SocketInfo> to_close;
  {
    std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
    if (dyn_socket_base < 0) return;
    to_close.assign(generic_open_sockets.begin() + dyn_socket_base, generic_open_sockets.end());
    generic_open_sockets.erase(generic_open_sockets.begin() + dyn_socket_base, generic_open_sockets.end());
  }
  for (const auto &si : to_close) { (void)closefunc(si); }
}

size_t SocketControl::waiting_queue_size() const
{
  std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
  return waiting_sockets.size();
}

bool SocketControl::try_move_to_waiting(int pos)
{
  std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
  if (((pos - dyn_socket_base) < 0) || (pos >= static_cast<int>(generic_open_sockets.size()))) {
    throw Error("Socket index out of range!");
  }
  if (max_waiting_connections > 0 && waiting_sockets.size() >= max_waiting_connections) {
    return false;// Queue full — caller should send 503
  }
  SocketControl::SocketInfo sockid = generic_open_sockets[pos];
  generic_open_sockets.erase(generic_open_sockets.begin() + pos);
  sockid.enqueue_time = std::chrono::steady_clock::now();
  waiting_sockets.push(sockid);
  return true;
}

std::vector<SocketControl::SocketInfo> SocketControl::collect_expired_waiting()
{
  std::vector<SocketInfo> expired;
  std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
  auto now = std::chrono::steady_clock::now();
  auto timeout = std::chrono::seconds(queue_timeout_seconds);

  // Queue is FIFO with monotonically increasing enqueue_time — pop from front until fresh
  while (!waiting_sockets.empty()) {
    if ((now - waiting_sockets.front().enqueue_time) >= timeout) {
      expired.push_back(waiting_sockets.front());
      waiting_sockets.pop();
    } else {
      break;// Remaining entries are newer — stop
    }
  }
  return expired;
}

std::vector<SocketControl::SocketInfo> SocketControl::collect_all_waiting()
{
  std::vector<SocketInfo> collected;
  std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
  while (!waiting_sockets.empty()) {
    collected.push_back(waiting_sockets.front());
    waiting_sockets.pop();
  }
  return collected;
}

std::vector<SocketControl::SocketInfo> SocketControl::collect_idle_dynsocks(int keep_alive_timeout_seconds)
{
  std::vector<SocketInfo> reaped;
  std::unique_lock<std::mutex> mutex_guard(sockets_mutex);
  if (dyn_socket_base < 0) return reaped;

  auto now = std::chrono::steady_clock::now();
  auto timeout = std::chrono::seconds(keep_alive_timeout_seconds);

  // Partition: move idle sockets to reaped, then erase from vector in one pass.
  // Order of reaped sockets doesn't matter (they're about to be closed).
  auto it = std::partition(
    generic_open_sockets.begin() + dyn_socket_base,
    generic_open_sockets.end(),
    [&](const SocketInfo &si) { return (now - si.last_activity) < timeout; });

  // Collect the idle sockets (those after the partition point)
  for (auto idle_it = it; idle_it != generic_open_sockets.end(); ++idle_it) {
    reaped.push_back(*idle_it);
  }
  generic_open_sockets.erase(it, generic_open_sockets.end());
  return reaped;
}
}
