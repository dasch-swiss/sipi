/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * Misuse-resistant C++ layer over the C-ABI seam (`ffi/sipi_ffi.h`).
 *
 * The C ABI is deliberately dumb: a struct of function pointers the transport
 * implements. This header is the *engine-internal* API that drives it correctly
 * by construction:
 *
 *  - The body is a `std::variant` of exactly one kind — `FileBody` (known
 *    length → Content-Length framing, possibly zero-copy `sendfile(2)`),
 *    `EmptyBody`, or `StreamBody` (unknown length → chunked). The engine cannot
 *    emit two bodies; `apply` dispatches the one alternative.
 *  - The serve *response* is computed by `build_*` as a pure-of-the-transport
 *    value (`std::expected<ServeResponse, SipiStatus>`) — unit-testable without
 *    a socket, and every failure-prone step runs there, *before* the response
 *    is committed, so it returns a clean status code.
 *  - `apply` is the single place that touches the C callbacks.
 *  - `sipi_guard` wraps each `extern "C"` entry so no C++ exception crosses the
 *    boundary (UB into Rust). It catches *exceptions* only — not codec
 *    segfaults, which remain an image-engine-hardening concern.
 */
#ifndef SIPI_FFI_SERVE_RESPONSE_H
#define SIPI_FFI_SERVE_RESPONSE_H

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ffi/sipi_ffi.h"

namespace Sipi::ffi {

/*! Result of an FFI serve entry. The C ABI returns the underlying `int` (0 on
 *  success, else the HTTP status the caller renders); this enum keeps the C++
 *  side self-documenting. */
enum class SipiStatus : int {
  Ok = 0,
  BadRequest = 400,
  NotFound = 404,
  InternalError = 500,
  ServiceUnavailable = 503,//!< memory budget exhausted (with Retry-After)
  TooManyRequests = 429,//!< rate limit exceeded (with Retry-After)
  //! Not an HTTP status to render: the client disconnected mid-decode. The
  //! caller emits nothing (the response was never committed); the engine has
  //! already counted it. 499 mirrors the nginx/Traefik "client closed request".
  ClientGone = 499,
};

using Header = std::pair<std::string, std::string>;

/*! Known-length body: the transport frames Content-Length (and may use
 *  zero-copy `sendfile(2)`). */
struct FileBody
{
  std::string path;
  std::uint64_t offset;
  std::uint64_t length;
};

/*! No body — a HEAD response or a zero-length file. */
struct EmptyBody
{
};

/*! Thin C++ view over the response's streaming-write callbacks, handed to a
 *  `StreamBody` producer. */
class StreamSink
{
public:
  explicit StreamSink(const SipiResponse &resp) noexcept : resp_(resp) {}

  /*! Write one chunk. Returns 0 on success, non-zero on a write failure. */
  [[nodiscard]] int write(const std::uint8_t *data, std::size_t len) const { return resp_.write(resp_.ctx, data, len); }

  /*! 1 = client gone / timed out → the producer should abort. */
  [[nodiscard]] bool cancelled() const { return resp_.cancelled != nullptr && resp_.cancelled(resp_.ctx) != 0; }

private:
  const SipiResponse &resp_;
};

/*! Produces an unknown-length body (e.g. the image encoder), pushing bytes into
 *  the sink after status + headers are committed. A move-only, type-erased
 *  callable: hand-rolled because libc++ here ships no `std::move_only_function`,
 *  and it keeps this header decoupled from `SipiImage` (the concrete producer
 *  captures the decoded image and lives with `sipi_serve_image`). Returns 0 or
 *  a write/encode error. */
struct StreamProducer
{
  virtual ~StreamProducer() = default;
  virtual int produce(const StreamSink &sink) = 0;
};

/*! Unknown-length body → the transport frames it chunked. */
struct StreamBody
{
  std::unique_ptr<StreamProducer> producer;
};

/*! Exactly one body kind, by construction — the engine cannot emit two. */
using Body = std::variant<FileBody, EmptyBody, StreamBody>;

struct ServeResponse
{
  int http_status{};
  std::vector<Header> headers;
  Body body;
  /*! Run by `apply` after the body has been delivered (on every path, including
   *  a producer that aborts). Empty by default; `build_image_response` uses it to
   *  unblock a cache file it pinned for a cache-hit `FileBody`. */
  std::function<void()> on_complete;
};

/*! Build the response for a raw `/file` request: validate readability, stat, sniff
 *  MIME, parse + clamp the Range. Pure of the transport (touches no
 *  `SipiResponse` callback) so it is unit-testable in isolation. Returns a
 *  `ServeResponse` or the error status the caller renders. May throw only on a
 *  pathological Range overflow (`std::stoull`); `sipi_guard` catches it. */
[[nodiscard]] std::expected<ServeResponse, SipiStatus> build_file_response(const char *resolved_path, const char *range);

/*! The single place that drives the C-ABI response callbacks: set status, add
 *  each header, then deliver the one body. */
void apply(ServeResponse &&response, const SipiResponse &resp);

/*! Run an FFI entry body so no C++ exception crosses the `extern "C"` boundary:
 *  returns the body's status code, or 500 on any throw. */
template<class F> int sipi_guard(F &&f) noexcept
{
  try {
    return f();
  } catch (...) {
    return static_cast<int>(SipiStatus::InternalError);
  }
}

}// namespace Sipi::ffi

#endif// SIPI_FFI_SERVE_RESPONSE_H
