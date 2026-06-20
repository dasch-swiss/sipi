/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * Typed write destinations for the format handlers (ADR-0006).
 *
 * `OutputSink` replaces the magic-string filepath sentinels (`"-"`/`"stdout:"`
 * for stdout, `"HTTP"` for the HTTP-server output) at the `SipiIO::write`
 * surface. It is deliberately free of any `shttps`/HTTP types: the HTTP socket
 * is reached only through `CallbackSink`'s opaque C-ABI callback, so
 * `src/formats/` carries no dependency on the transport layer. That callback
 * signature (`SipiWriteFn`) is fixed here and reused verbatim by the Phase B/C
 * FFI seam, where the Rust shell supplies the body-write callback.
 */
#ifndef SIPI_FORMATS_OUTPUT_SINK_H
#define SIPI_FORMATS_OUTPUT_SINK_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace Sipi {

/*!
 * Opaque body-write callback. Returns 0 on success, non-zero on a write
 * failure (peer gone, socket error). Identical to the Phase B/C FFI
 * `SipiWriteFn`: a pure C-ABI callback so Rust can supply it unchanged.
 */
extern "C" {
typedef int (*SipiWriteFn)(void *ctx, const uint8_t *data, size_t len);
}

/*!
 * A filesystem path. `"-"` / `"stdout:"` keep their stdout meaning. Stdout is
 * deliberately folded into FilePath rather than given a separate StdoutSink
 * alternative (ADR-0006 listed one): each codec already special-cases the
 * stdout string against its native writer, so a distinct type would add nothing.
 */
struct FilePath
{
  std::string path;
};

/*! An opaque C-ABI sink — the HTTP socket today, a Rust-owned sink in Phase C. */
struct CallbackSink
{
  SipiWriteFn write;
  void *ctx;
};

struct TeeSink;//!< forward declaration (a variant alternative may contain the variant)

/*!
 * Where encoded image bytes go. `FilePath` is handled by each codec's native
 * file/stdout writer; `CallbackSink` and `TeeSink` are streamed through
 * `SinkStream` (the codec drives its destination manager chunk-by-chunk).
 */
using OutputSink = std::variant<FilePath, CallbackSink, TeeSink>;

/*!
 * Broadcasts each encoded chunk to several sinks at once. Replaces the
 * dual-write-to-socket-and-cache tee that `shttps::Connection` performs today
 * (`openCacheFile` + `sendAndFlush`); per ADR-0007 the tee lives in the write
 * loop, not in the request handler.
 */
struct TeeSink
{
  std::vector<OutputSink> sinks;
};

/*!
 * True when the codec must stream the destination through `SinkStream` rather
 * than use its native seekable file/stdout writer — i.e. for any sink that is
 * not a bare `FilePath`.
 */
[[nodiscard]] bool is_streaming_sink(const OutputSink &sink);

/*!
 * Drives an `OutputSink` as a forward-only byte stream. A codec's destination
 * manager (libjpeg dest mgr, libpng write fn, Kakadu `kdu_compressed_target`,
 * the TIFF mem-buffer dump) calls `write()` once per encoded chunk.
 *
 * Failure policy mirrors `shttps::Connection`'s tee exactly:
 * - A `CallbackSink` (the HTTP socket) is **fatal** — a non-zero return
 *   propagates so the codec aborts its encode (the equivalent of today's
 *   `OUTPUT_WRITE_FAIL`-driven abort), without a C++ exception crossing the
 *   codec's C frames.
 * - A `FilePath` leaf (the cache file in a tee) is **best-effort** — a write
 *   or open failure is swallowed, matching shttps, which never aborts a
 *   response because the cache write failed.
 */
class SinkStream
{
public:
  explicit SinkStream(const OutputSink &sink);

  /*! Write one chunk. Returns 0 on success, non-zero if a fatal sink failed. */
  [[nodiscard]] int write(const uint8_t *data, size_t len);

private:
  struct Leaf
  {
    SipiWriteFn fn{ nullptr };//!< set for a CallbackSink leaf
    void *ctx{ nullptr };
    std::shared_ptr<std::ostream> file;//!< set for a FilePath leaf
    bool fatal{ false };//!< CallbackSink failures abort; FilePath failures don't
  };

  void flatten(const OutputSink &sink);

  std::vector<Leaf> leaves_;
};

}// namespace Sipi

#endif// SIPI_FORMATS_OUTPUT_SINK_H
