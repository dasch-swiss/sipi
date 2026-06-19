/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "formats/output_sink.h"

#include <fstream>

namespace Sipi {

bool is_streaming_sink(const OutputSink &sink) { return !std::holds_alternative<FilePath>(sink); }

SinkStream::SinkStream(const OutputSink &sink) { flatten(sink); }

void SinkStream::flatten(const OutputSink &sink)
{
  std::visit(
    [this](const auto &alt) {
      using T = std::decay_t<decltype(alt)>;
      if constexpr (std::is_same_v<T, CallbackSink>) {
        leaves_.push_back(Leaf{ alt.write, alt.ctx, nullptr, /*fatal=*/true });
      } else if constexpr (std::is_same_v<T, FilePath>) {
        // A FilePath leaf only reaches SinkStream as part of a tee (the cache
        // file). Open it for streaming; an open failure leaves the leaf inert,
        // matching shttps's best-effort cache (a bad cache write never aborts
        // the response).
        auto file = std::make_shared<std::ofstream>(alt.path, std::ios::binary | std::ios::trunc);
        leaves_.push_back(Leaf{ nullptr, nullptr, std::move(file), /*fatal=*/false });
      } else if constexpr (std::is_same_v<T, TeeSink>) {
        for (const auto &child : alt.sinks) flatten(child);
      }
    },
    sink);
}

int SinkStream::write(const uint8_t *data, size_t len)
{
  int rc = 0;
  for (auto &leaf : leaves_) {
    if (leaf.fn != nullptr) {
      const int r = leaf.fn(leaf.ctx, data, len);
      if (r != 0 && leaf.fatal) rc = r;
    } else if (leaf.file) {
      // Best-effort: a failed cache write sets failbit and is silently
      // dropped, exactly as shttps does for its tee'd cache file.
      leaf.file->write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(len));
    }
  }
  return rc;
}

}// namespace Sipi
