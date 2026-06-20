/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/serve_response.h"

#include <sys/stat.h>
#include <unistd.h>

#include <ctime>
#include <regex>
#include <string>

#include "shttps/util/Parsing.h"

namespace Sipi::ffi {
namespace {

  template<class... Ts> struct overloaded : Ts...
  {
    using Ts::operator()...;
  };

  /*!
   * Parse a single-range `Range` header (`bytes=start-end`, end optional) against
   * the file size. Mirrors the legacy regex. Returns false on a malformed
   * expression (caller maps to 400). `end` is the requested upper bound verbatim;
   * the caller clamps it. May throw `std::out_of_range` on a numeric overflow —
   * `sipi_guard` catches it (→ 500), matching the legacy behaviour.
   */
  bool parse_range(const char *range, std::uint64_t fsize, std::uint64_t &start, std::uint64_t &end)
  {
    static const std::regex re(R"(bytes=\s*(\d+)-(\d*)[\D.*]?)");
    std::cmatch m;
    if (!std::regex_match(range, m, re) || m.size() < 2) { return false; }
    start = std::stoull(m[1]);
    end = (m.size() > 1 && !m[2].str().empty()) ? std::stoull(m[2]) : (fsize > 0 ? fsize - 1 : 0);
    return true;
  }

}// namespace

std::expected<ServeResponse, SipiStatus> decide_serve_file(const char *resolved_path, const char *range)
{
  // Readability gate — matches the legacy 404 contract.
  if (access(resolved_path, R_OK) != 0) { return std::unexpected(SipiStatus::NotFound); }

  struct stat st{};
  if (stat(resolved_path, &st) != 0) { return std::unexpected(SipiStatus::InternalError); }
  const auto fsize = static_cast<std::uint64_t>(st.st_size);

  // Resolve the served byte span first, so a bad Range fails before any of the
  // (more expensive) header work below.
  const bool is_range = (range != nullptr && range[0] != '\0');
  std::uint64_t start = 0;
  std::uint64_t end = (fsize > 0 ? fsize - 1 : 0);
  std::uint64_t length = fsize;// non-range: the whole file
  if (is_range) {
    if (!parse_range(range, fsize, start, end)) { return std::unexpected(SipiStatus::BadRequest); }
    // A start beyond EOF is unserveable; the legacy seek surfaced this as 500.
    if (start >= fsize) { return std::unexpected(SipiStatus::InternalError); }
    // Clamp end so Content-Range's last-byte-pos always equals the delivered
    // length — the two cannot diverge (the legacy handler let them).
    if (end >= fsize) { end = fsize - 1; }
    length = end - start + 1;
  }

#ifdef __APPLE__
  const std::time_t mtime = st.st_mtimespec.tv_sec;
#else
  const std::time_t mtime = st.st_mtim.tv_sec;
#endif
  char timebuf[100];
  timebuf[0] = '\0';
  std::tm tm_buf{};
  if (const std::tm *gmt = gmtime_r(&mtime, &tm_buf); gmt != nullptr) {
    std::strftime(timebuf, sizeof timebuf, "%a, %d %b %Y %H:%M:%S %Z", gmt);
  }

  ServeResponse out;
  out.http_status = is_range ? 206 : 200;
  out.headers.emplace_back("Content-Type", shttps::Parsing::getBestFileMimetype(resolved_path));
  out.headers.emplace_back("Cache-Control", "public, must-revalidate, max-age=0");
  out.headers.emplace_back("Pragma", "no-cache");
  out.headers.emplace_back("Accept-Ranges", "bytes");
  out.headers.emplace_back("Last-Modified", timebuf);
  if (is_range) {
    out.headers.emplace_back(
      "Content-Range", "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(fsize));
  }

  if (length > 0) {
    out.body = FileBody{ resolved_path, start, length };
  } else {
    out.body = EmptyBody{};
  }
  return out;
}

void apply(ServeResponse &&response, const SipiResponse &resp)
{
  resp.set_status(resp.ctx, response.http_status);
  for (const auto &[name, value] : response.headers) { resp.add_header(resp.ctx, name.c_str(), value.c_str()); }

  std::visit(overloaded{
               [](const EmptyBody &) {},
               [&](const FileBody &f) { resp.send_file(resp.ctx, f.path.c_str(), f.offset, f.length); },
               [&](StreamBody &s) {
                 const StreamSink sink(resp);
                 s.producer->produce(sink);
               },
             },
    response.body);
}

}// namespace Sipi::ffi
