/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SockStream.h"

#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL SO_NOSIGPIPE// for OS X
#endif

using namespace std;
using namespace shttps;

SockStream::SockStream(int sock_p, int in_bufsize_p, int out_bufsize_p, int putback_size_p)
  : in_bufsize(in_bufsize_p), putback_size(putback_size_p), out_bufsize(out_bufsize_p), sock(sock_p)
{
  cSSL = nullptr;

  in_buf = new char[in_bufsize + putback_size];
  char *end = in_buf + in_bufsize + putback_size;
  setg(end, end, end);

  out_buf = new char[out_bufsize];
  memset(out_buf, 0, out_bufsize);
  setp(out_buf, out_buf + out_bufsize);
}


SockStream::SockStream(SSL *cSSL_p, int in_bufsize_p, int out_bufsize_p, int putback_size_p)
  : in_bufsize(in_bufsize_p), putback_size(putback_size_p), out_bufsize(out_bufsize_p), cSSL(cSSL_p)
{
  sock = -1;
  in_buf = new char[in_bufsize + putback_size];
  char *end = in_buf + in_bufsize + putback_size;
  setg(end, end, end);

  out_buf = new char[out_bufsize];
  memset(out_buf, 0, out_bufsize);
  setp(out_buf, out_buf + out_bufsize);
}

SockStream::~SockStream()
{
  delete[] in_buf;
  delete[] out_buf;
}

streambuf::int_type SockStream::underflow(void)
{
  if (gptr() < egptr()) { return traits_type::to_int_type(*gptr()); }

  char *start = in_buf;

  if (eback() == in_buf) {// here we enter only if the first read has already taken place..
    memmove(in_buf, egptr() - putback_size, putback_size);// copy putback area to beginning of buffer
    start += putback_size;
  }

  ssize_t n;

  if (cSSL == nullptr) {
    n = read(sock, start, in_bufsize);
  } else {
    if (SSL_get_shutdown(cSSL) == 0) {
      n = SSL_read(cSSL, start, in_bufsize);
    } else {
      n = 0;
    }
  }
  if (n <= 0) { return traits_type::eof(); }

  setg(in_buf, start, start + n);

  return traits_type::to_int_type(*gptr());
}

streambuf::int_type SockStream::overflow(streambuf::int_type ch)
{
  if (ch == traits_type::eof()) {
    return ch;// do nothing;
  }

  if (pptr() >= epptr()) {
    size_t n = out_bufsize;
    size_t nn = 0;
    while (n > 0) {
      ssize_t tmp_n;
      if (cSSL == nullptr) {
        tmp_n = send(sock, out_buf + nn, n - nn, MSG_NOSIGNAL);
      } else {
        if (SSL_get_shutdown(cSSL) == 0) {
          tmp_n = SSL_write(cSSL, out_buf + nn, n - nn);
        } else {
          tmp_n = 0;
        }
      }
      if (tmp_n <= 0) {
        return traits_type::eof();
        // we have a problem.... Possibly a broken pipe
      }
      n -= tmp_n;
      nn += tmp_n;
    }

    pbump(-nn);
    *pptr() = ch;
    pbump(1);
  } else {
    *pptr() = ch;
    pbump(1);
  }

  return ch;
}

int SockStream::sync(void)
{
  std::ptrdiff_t n = pptr() - out_buf;
  size_t nn = 0;

  while (n > 0) {
    ssize_t tmp_n;
    if (cSSL == nullptr) {
      tmp_n = send(sock, out_buf + nn, n - nn, MSG_NOSIGNAL);
    } else {
      if (SSL_get_shutdown(cSSL) == 0) {
        tmp_n = SSL_write(cSSL, out_buf + nn, n - nn);
      } else {
        tmp_n = 0;
      }
    }
    if (tmp_n <= 0) { return -1; }

    n -= tmp_n;
    nn += tmp_n;
  }

  pbump(-nn);

  return 0;
}
