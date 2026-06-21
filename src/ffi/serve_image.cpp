/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/serve_image.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "SipiImage.h"
#include "SipiImageError.h"
#include "SipiCache.h"
#include "SipiMemoryBudget.h"
#include "SipiPeakMemory.h"
#include "SipiRateLimiter.h"
#include "formats/output_sink.h"
#include "iiifparser/SipiDecodeDims.h"
#include "iiifparser/SipiIdentifier.h"
#include "iiifparser/SipiQualityFormat.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiRotation.h"
#include "iiifparser/SipiSize.h"
#include "logging/logger.h"
#include "metadata/icc.h"
#include "observability/metrics.h"
#include "observability/sentry.h"
#include "shttps/util/Parsing.h"
#include "shttps/util/UrlDecode.h"

namespace Sipi::ffi {
namespace {

  using observability::capture_image_error;
  using observability::get_file_size;
  using observability::ImageContext;
  using observability::Metrics;
  using observability::populate_from_image;
  using observability::SipiMode;

  constexpr const char *kCacheControl = "must-revalidate, post-check=0, pre-check=0";

  // The Content-Type for an emitted IIIF format (matches the legacy switch).
  const char *content_type_for(SipiQualityFormat::FormatType fmt)
  {
    switch (fmt) {
    case SipiQualityFormat::TIF:
      return "image/tiff";
    case SipiQualityFormat::JPG:
      return "image/jpeg";
    case SipiQualityFormat::PNG:
      return "image/png";
    case SipiQualityFormat::JP2:
      return "image/jp2";
    default:
      return nullptr;
    }
  }

  // The image-root mimetype → input format (matches the legacy sniff).
  SipiQualityFormat::FormatType detect_in_format(const std::string &infile)
  {
    const std::string mime = shttps::Parsing::getFileMimetype(infile).first;
    if (mime == "image/tiff") { return SipiQualityFormat::TIF; }
    if (mime == "image/jpeg") { return SipiQualityFormat::JPG; }
    if (mime == "image/png") { return SipiQualityFormat::PNG; }
    if (mime == "image/jpx" || mime == "image/jp2") { return SipiQualityFormat::JP2; }
    return SipiQualityFormat::UNSUPPORTED;
  }

  // Reconstruct the typed iiifparser objects from the flat seam struct (the
  // params are typed so the seam carries no iiifparser class layout). The values
  // were produced by the C++ string parsers caller-side, so reconstruction cannot
  // fail — it only re-seats the parse-time fields.
  std::shared_ptr<SipiRegion> rebuild_region(const SipiIiifParams &p)
  {
    return std::make_shared<SipiRegion>(static_cast<SipiRegion::CoordType>(p.region_type),
      p.region[0],
      p.region[1],
      p.region[2],
      p.region[3]);
  }

  std::shared_ptr<SipiSize> rebuild_size(const SipiIiifParams &p)
  {
    return std::make_shared<SipiSize>(static_cast<SipiSize::SizeType>(p.size_type),
      p.size_upscaling != 0,
      p.size_percent,
      p.size_reduce,
      p.size_nx,
      p.size_ny);
  }

  // The canonical IIIF URL (Link header + cache key). Relocated verbatim from
  // SipiHttpServer::get_canonical_url — pure of the transport, so it lives with
  // the engine-facing seam rather than the soon-deleted HTTP server.
  std::pair<std::string, std::string> build_canonical_url(size_t tmp_w,
    size_t tmp_h,
    const std::string &host,
    const std::string &prefix,
    const std::string &identifier,
    const std::shared_ptr<SipiRegion> &region,
    const std::shared_ptr<SipiSize> &size,
    SipiRotation &rotation,
    SipiQualityFormat &quality_format,
    int pagenum,
    const std::string &cannonical_watermark)
  {
    static constexpr int canonical_len = 127;

    char canonical_region[canonical_len + 1];
    char canonical_size[canonical_len + 1];

    int tmp_r_x = 0, tmp_r_y = 0, tmp_red = 0;
    size_t tmp_r_w = 0, tmp_r_h = 0;
    bool tmp_ro = false;

    if (region->getType() != SipiRegion::FULL) {
      region->crop_coords(tmp_w, tmp_h, tmp_r_x, tmp_r_y, tmp_r_w, tmp_r_h);
    }

    region->canonical(canonical_region, canonical_len);

    if (size->getType() != SipiSize::FULL) {
      try {
        size->get_size(tmp_w, tmp_h, tmp_r_w, tmp_r_h, tmp_red, tmp_ro);
      } catch (Sipi::SipiSizeError &err) {
        throw SipiError("SipiSize error!");
      }
    }

    size->canonical(canonical_size, canonical_len);
    float angle;
    const bool mirror = rotation.get_rotation(angle);
    char canonical_rotation[canonical_len + 1];

    if (mirror || (angle != 0.0)) {
      if ((angle - floorf(angle)) < 1.0e-6) {
        if (mirror) {
          (void)snprintf(canonical_rotation, canonical_len, "!%ld", std::lround(angle));
        } else {
          (void)snprintf(canonical_rotation, canonical_len, "%ld", std::lround(angle));
        }
      } else {
        if (mirror) {
          (void)snprintf(canonical_rotation, canonical_len, "!%1.1f", angle);
        } else {
          (void)snprintf(canonical_rotation, canonical_len, "%1.1f", angle);
        }
      }
    } else {
      (void)snprintf(canonical_rotation, canonical_len, "0");
    }

    constexpr unsigned canonical_header_len = 511;
    char canonical_header[canonical_header_len + 1];
    char ext[5];

    switch (quality_format.format()) {
    case SipiQualityFormat::JPG:
      ext[0] = 'j', ext[1] = 'p', ext[2] = 'g', ext[3] = '\0';
      break;
    case SipiQualityFormat::JP2:
      ext[0] = 'j', ext[1] = 'p', ext[2] = '2', ext[3] = '\0';
      break;
    case SipiQualityFormat::TIF:
      ext[0] = 't', ext[1] = 'i', ext[2] = 'f', ext[3] = '\0';
      break;
    case SipiQualityFormat::PNG:
      ext[0] = 'p', ext[1] = 'n', ext[2] = 'g', ext[3] = '\0';
      break;
    default:
      throw SipiError("Unsupported file format requested! Supported are .jpg, .jp2, .tif, .png");
    }

    std::string format;
    if (quality_format.quality() != SipiQualityFormat::DEFAULT) {
      switch (quality_format.quality()) {
      case SipiQualityFormat::COLOR:
        format = "/color.";
        break;
      case SipiQualityFormat::GRAY:
        format = "/gray.";
        break;
      case SipiQualityFormat::BITONAL:
        format = "/bitonal.";
        break;
      default:
        format = "/default.";
      }
    } else {
      format = "/default.";
    }

    std::string fullid = identifier;
    if (pagenum > 0) { fullid += "@" + std::to_string(pagenum); }
    (void)snprintf(canonical_header,
      canonical_header_len,
      "<http://%s/%s/%s/%s/%s/%s/default.%s/%s>;rel=\"canonical\"",
      host.c_str(),
      prefix.c_str(),
      fullid.c_str(),
      canonical_region,
      canonical_size,
      canonical_rotation,
      ext,
      cannonical_watermark.c_str());

    std::string canonical = host + "/" + prefix + "/" + fullid + "/" + std::string(canonical_region) + "/"
                            + std::string(canonical_size) + "/" + std::string(canonical_rotation) + format
                            + std::string{ ext } + "/" + std::string{ cannonical_watermark };

    return std::make_pair(std::string(canonical_header), canonical);
  }

  // The decoded image + the encode job, captured for the streamed-body tail.
  // produce() runs ONLY the encode (the rarely-failing step): the decode +
  // transforms already ran in build_image_response, before the response committed.
  class ImageEncodeProducer : public StreamProducer
  {
  public:
    ImageEncodeProducer(SipiImage &&img,
      SipiQualityFormat::FormatType format,
      int jpeg_quality,
      SipiCache *cache,
      std::string cachefile,
      std::string infile,
      std::string canonical,
      std::string request_uri,
      SipiImgInfo info,
      std::optional<MemoryBudgetGuard> budget_guard)
      : budget_guard_(std::move(budget_guard)), img_(std::move(img)), format_(format), jpeg_quality_(jpeg_quality),
        cache_(cache), cachefile_(std::move(cachefile)), infile_(std::move(infile)), canonical_(std::move(canonical)),
        request_uri_(std::move(request_uri)), info_(info)
    {}

    int produce(const StreamSink &sink) override
    {
      // Bridge the StreamSink to the format handlers' C-ABI write callback. The
      // free thunk + struct ctx carry both the sink (the socket) and a running
      // byte count for the DEV-6660 cache-integrity check.
      ThunkCtx tctx{ &sink, 0 };
      const CallbackSink socket{ &ImageEncodeProducer::sink_thunk, &tctx };

      const bool caching = cache_ != nullptr && !cachefile_.empty();
      const OutputSink out = caching
                               ? OutputSink{ TeeSink{ { OutputSink{ socket }, OutputSink{ FilePath{ cachefile_ } } } } }
                               : OutputSink{ socket };

      try {
        switch (format_) {
        case SipiQualityFormat::JPG: {
          SipiCompressionParams qp = { { JPEG_QUALITY, std::to_string(jpeg_quality_) } };
          img_.write("jpg", out, &qp);
          break;
        }
        case SipiQualityFormat::JP2:
          img_.write("jpx", out);
          break;
        case SipiQualityFormat::TIF:
          img_.write("tif", out);
          break;
        case SipiQualityFormat::PNG:
          img_.write("png", out);
          break;
        default:
          break;
        }
      } catch (SipiImageClientAbortError &) {
        // Client closed the socket mid-response (Traefik 499). Not a server
        // error: drop the partial cache file, no Sentry.
        if (caching) { ::unlink(cachefile_.c_str()); }
        log_info("Client aborted HTTP response for %s", request_uri_.c_str());
        Metrics::instance().client_disconnected_total.Increment();
        return 1;
      } catch (SipiError &err) {
        if (caching) { ::unlink(cachefile_.c_str()); }
        capture_write_error(err.to_string());
        log_err("GET %s: error writing image: %s", request_uri_.c_str(), err.to_string().c_str());
        return 1;
      } catch (SipiImageError &err) {
        if (caching) { ::unlink(cachefile_.c_str()); }
        capture_write_error(err.what());
        log_err("GET %s: error writing image: %s", request_uri_.c_str(), err.what());
        return 1;
      }

      if (caching) { finalize_cache(tctx.bytes); }
      return 0;
    }

  private:
    struct ThunkCtx
    {
      const StreamSink *sink;
      std::uint64_t bytes;
    };

    static int sink_thunk(void *ctx, const std::uint8_t *data, std::size_t len)
    {
      auto *t = static_cast<ThunkCtx *>(ctx);
      t->bytes += len;
      return t->sink->write(data, len);
    }

    void capture_write_error(const std::string &message) const
    {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile_;
      sentry_ctx.file_size_bytes = get_file_size(infile_);
      sentry_ctx.request_uri = request_uri_;
      sentry_ctx.output_format = format_type_to_string(format_);
      populate_from_image(sentry_ctx, img_);
      capture_image_error(message, "write", sentry_ctx, SipiMode::Server);
    }

    // Commit the cache file iff it is intact (DEV-6660): the FilePath leaf is
    // best-effort, so a short/failed cache write must not register a truncated
    // file. Also drops over-size cache files (the legacy post-write check).
    void finalize_cache(std::uint64_t streamed_bytes)
    {
      struct stat st{};
      if (stat(cachefile_.c_str(), &st) != 0) {
        Metrics::instance().cache_skips_total.Increment();
        return;
      }
      const auto written = static_cast<std::uint64_t>(st.st_size);
      if (written == 0 || written != streamed_bytes) {
        // Truncated or empty: the cache write did not keep up with the socket.
        log_warn("Cache file %s incomplete (%llu of %llu bytes), discarding",
          cachefile_.c_str(),
          static_cast<unsigned long long>(written),
          static_cast<unsigned long long>(streamed_bytes));
        ::unlink(cachefile_.c_str());
        Metrics::instance().cache_skips_total.Increment();
        return;
      }
      const long long max_cs = cache_->getMaxCacheSize();
      if (max_cs > 0 && st.st_size > max_cs) {
        log_warn("Converted file %s (%lld bytes) exceeds cache_size (%lld bytes), removing",
          cachefile_.c_str(),
          static_cast<long long>(st.st_size),
          max_cs);
        ::unlink(cachefile_.c_str());
        Metrics::instance().cache_skips_total.Increment();
        return;
      }
      cache_->add(infile_,
        canonical_,
        cachefile_,
        info_.width,
        info_.height,
        info_.tile_width,
        info_.tile_height,
        info_.clevels,
        info_.numpages);
    }

    // The decode-memory reservation, held (not read) so the budget stays
    // accounted for across the streamed encode and is released on destruction.
    // Declared first so it outlives img_: the image buffer frees, *then* the
    // budget is released, keeping the in-flight accounting honest at teardown.
    std::optional<MemoryBudgetGuard> budget_guard_;
    SipiImage img_;
    SipiQualityFormat::FormatType format_;
    int jpeg_quality_;
    SipiCache *cache_;
    std::string cachefile_;
    std::string infile_;
    std::string canonical_;
    std::string request_uri_;
    SipiImgInfo info_;
  };

  std::string str_or_empty(const char *s) { return s != nullptr ? std::string(s) : std::string(); }

  // A full-file body for the passthrough / cache-hit paths, stat'd here so a
  // file that vanished after the earlier checks is a clean error rather than a
  // 200 with a wrong length (get_file_size returns 0 on a failed stat, which
  // would underflow the transport's inclusive send_file range). A 0-byte file
  // becomes an EmptyBody for the same reason.
  std::expected<Body, SipiStatus> full_file_body(const std::string &path)
  {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) { return std::unexpected(SipiStatus::InternalError); }
    const auto size = static_cast<std::uint64_t>(st.st_size);
    if (size == 0) { return Body{ EmptyBody{} }; }
    return Body{ FileBody{ path, 0, size } };
  }

}// namespace

std::expected<ServeResponse, SipiStatus>
  build_image_response(const SipiServeRequest &req, const EngineContext &eng, const std::function<bool()> &cancelled)
{
  const std::string infile = str_or_empty(req.resolved_path);
  const std::string uri = str_or_empty(req.request_uri);

  // Reconstruct the typed IIIF params from the flat seam (caller already
  // validated the source strings, so this cannot throw).
  auto region = rebuild_region(req.params);
  auto size = rebuild_size(req.params);
  SipiRotation rotation(req.params.rotation, req.params.rotation_mirror != 0);
  SipiQualityFormat quality_format(
    static_cast<SipiQualityFormat::QualityType>(req.params.quality_type),
    static_cast<SipiQualityFormat::FormatType>(req.params.format_type));

  const SipiIdentifier sid(shttps::urldecode(str_or_empty(req.identifier)));

  const std::string watermark = str_or_empty(req.watermark_path);
  auto restricted_size =
    req.restricted_size != nullptr ? std::make_shared<SipiSize>(std::string(req.restricted_size)) : std::make_shared<SipiSize>();

  const SipiQualityFormat::FormatType in_format = detect_in_format(infile);

  if (access(infile.c_str(), R_OK) != 0) { return std::unexpected(SipiStatus::NotFound); }

  float angle = 0.F;
  const bool mirror = rotation.get_rotation(angle);

  // Image shape (no full decode) — needed for size math, the canonical URL, the
  // memory estimate, and the cache entry.
  SipiImgInfo info;
  try {
    SipiImage probe;
    info = probe.read_shape(infile);
  } catch (SipiImageError &err) {
    ImageContext sentry_ctx;
    sentry_ctx.input_file = infile;
    sentry_ctx.file_size_bytes = get_file_size(infile);
    sentry_ctx.request_uri = uri;
    capture_image_error(err.to_string(), "read", sentry_ctx, SipiMode::Server);
    return std::unexpected(SipiStatus::InternalError);
  }
  if (info.success == SipiImgInfo::FAILURE) { return std::unexpected(SipiStatus::InternalError); }

  const size_t img_w = info.width;
  const size_t img_h = info.height;

  size_t tmp_r_w{ 0 }, tmp_r_h{ 0 };
  int tmp_red{ 0 };
  bool tmp_ro{ false };
  try {
    size->get_size(img_w, img_h, tmp_r_w, tmp_r_h, tmp_red, tmp_ro);
  } catch (Sipi::SipiSizeError &) {
    return std::unexpected(SipiStatus::BadRequest);
  } catch (Sipi::SipiError &) {
    return std::unexpected(SipiStatus::BadRequest);
  }

  // Requested output dims, before restricted_size may shrink them (the pixel
  // limit + rate limit key off the request, not the served size).
  const size_t requested_w = tmp_r_w;
  const size_t requested_h = tmp_r_h;

  try {
    restricted_size->get_size(img_w, img_h, tmp_r_w, tmp_r_h, tmp_red, tmp_ro);
  } catch (Sipi::SipiSizeError &) {
    return std::unexpected(SipiStatus::BadRequest);
  } catch (Sipi::SipiError &) {
    return std::unexpected(SipiStatus::BadRequest);
  }
  if (!restricted_size->undefined() && (*size > *restricted_size)) { size = restricted_size; }

  // Output pixel-count guard.
  if (eng.max_pixel_limit > 0 && requested_w > 0 && requested_h > 0) {
    const size_t output_pixels = requested_w * requested_h;
    if (output_pixels > eng.max_pixel_limit) {
      log_warn("Request rejected: output %zux%zu (%zu pixels) exceeds limit %zu: %s",
        requested_w, requested_h, output_pixels, eng.max_pixel_limit, uri.c_str());
      Metrics::instance().image_too_large_total.Increment();
      return std::unexpected(SipiStatus::BadRequest);
    }
  }

  // Per-client rate limiting.
  if (eng.rate_limiter != nullptr && requested_w > 0 && requested_h > 0) {
    const size_t request_pixels = requested_w * requested_h;
    const std::string client_id = str_or_empty(req.client_ip);
    const auto result = eng.rate_limiter->check_and_record(client_id, request_pixels);
    auto &metrics = Metrics::instance();

    if (result.over_budget) {
      const std::string action = result.allowed ? "shadow_rejected" : "rejected";
      log_warn("{\"event\":\"rate_limit_exceeded\",\"client_ip\":\"%s\","
               "\"pixels_consumed\":%zu,\"budget\":%zu,\"window_seconds\":%u,"
               "\"action\":\"%s\",\"request_pixels\":%zu,\"path\":\"%s\"}",
        client_id.c_str(), result.pixels_consumed, result.budget,
        eng.rate_limiter->mode() == RateLimitMode::MONITOR ? 0u : result.retry_after,
        action.c_str(), request_pixels, uri.c_str());
      (result.allowed ? metrics.rate_limit_shadow_rejected : metrics.rate_limit_rejected).Increment();

      if (!result.allowed) {
        ServeResponse out;
        out.http_status = 429;
        out.headers.emplace_back("Retry-After", std::to_string(result.retry_after));
        out.body = EmptyBody{};
        return out;
      }
    } else {
      metrics.rate_limit_allowed.Increment();
    }

    if (result.pixels_consumed > result.budget * 80 / 100) { metrics.rate_limit_near_limit_total.Increment(); }
    metrics.rate_limit_clients_tracked.Set(static_cast<double>(eng.rate_limiter->tracked_clients()));
  }

  // Canonical URL (Link header + cache key).
  const std::string cannonical_watermark = watermark.empty() ? "0" : "1";
  std::pair<std::string, std::string> canonical_info;
  try {
    canonical_info = build_canonical_url(
      img_w, img_h, str_or_empty(req.forwarded_host), str_or_empty(req.prefix), sid.getIdentifier(),
      region, size, rotation, quality_format, sid.getPage(), cannonical_watermark);
  } catch (Sipi::SipiError &) {
    return std::unexpected(SipiStatus::BadRequest);
  }
  const std::string canonical_header = canonical_info.first;
  const std::string canonical = canonical_info.second;
  const char *content_type = content_type_for(quality_format.format());

  auto base_headers = [&] {
    std::vector<Header> h;
    h.emplace_back("Cache-Control", kCacheControl);
    h.emplace_back("Link", canonical_header);
    if (content_type != nullptr) { h.emplace_back("Content-Type", content_type); }
    return h;
  };

  // HEAD: headers only — no decode, no cache write (also closes the legacy
  // zero-byte-HEAD cache bug, DEV-6660).
  if (req.is_head != 0) {
    ServeResponse out;
    out.http_status = 200;
    out.headers = base_headers();
    out.body = EmptyBody{};
    return out;
  }

  // Direct passthrough: the request maps 1:1 onto the source file.
  if (region->getType() == SipiRegion::FULL && size->getType() == SipiSize::FULL && angle == 0.0 && !mirror
      && watermark.empty() && quality_format.format() == in_format
      && quality_format.quality() == SipiQualityFormat::DEFAULT) {
    auto body = full_file_body(infile);
    if (!body) { return std::unexpected(body.error()); }
    ServeResponse out;
    out.http_status = 200;
    out.headers = base_headers();
    out.body = std::move(*body);
    return out;
  }

  // Cache hit (never for watermarked output): pin the file, serve it, unpin when
  // the body has been delivered.
  if (eng.cache != nullptr) {
    const std::string cachefile = eng.cache->check(infile, canonical, true);
    if (!cachefile.empty()) {
      log_debug("Using cachefile %s", cachefile.c_str());
      SipiCache *cache = eng.cache;
      auto body = full_file_body(cachefile);
      if (!body) {
        cache->deblock(cachefile);// pinned by check(); release it before bailing
        return std::unexpected(body.error());
      }
      ServeResponse out;
      out.http_status = 200;
      out.headers = base_headers();
      out.body = std::move(*body);
      out.on_complete = [cache, cachefile] { cache->deblock(cachefile); };
      return out;
    }
  }

  // Memory budget — only the decode path pays it.
  std::optional<MemoryBudgetGuard> budget_guard;
  if (eng.memory_budget != nullptr) {
    const auto ddims = compute_decode_dims(img_w, img_h, info.clevels, region, size);
    const bool needs_icc = quality_format.quality() == SipiQualityFormat::COLOR
                           || quality_format.quality() == SipiQualityFormat::GRAY;
    const size_t estimated = estimate_peak_memory(
      ddims.width, ddims.height, ddims.out_w, ddims.out_h, info.nc, info.bps, static_cast<double>(angle), needs_icc);

    auto &metrics = Metrics::instance();
    metrics.decode_memory_estimate_bytes.Observe(static_cast<double>(estimated));
    const auto result = eng.memory_budget->try_acquire(estimated);
    metrics.decode_memory_used_bytes.Set(static_cast<double>(result.used));

    if (result.allowed && !result.over_budget) {
      metrics.decode_memory_acquired.Increment();
    } else if (result.allowed && result.over_budget) {
      metrics.decode_memory_shadow_rejected.Increment();
      log_warn("Memory budget over limit (monitor): %zu / %zu bytes for %s", result.used, result.budget, uri.c_str());
    } else {
      metrics.decode_memory_rejected.Increment();
      log_warn("Memory budget exhausted (enforce): %zu / %zu bytes, rejecting %s", result.used, result.budget, uri.c_str());
      ServeResponse out;
      out.http_status = 503;
      out.headers.emplace_back("Retry-After", "5");
      out.body = EmptyBody{};
      return out;
    }

    if (result.used > result.budget - result.budget / 5) { metrics.decode_memory_near_limit_total.Increment(); }

    SipiMemoryBudget *mb = eng.memory_budget;
    budget_guard.emplace(*mb, estimated, result.allowed, [mb] {
      Metrics::instance().decode_memory_used_bytes.Set(static_cast<double>(mb->used()));
    });
  }

  if (cancelled()) {
    Metrics::instance().client_disconnected_total.Increment();
    return std::unexpected(SipiStatus::ClientGone);
  }

  SipiImage img;
  try {
    img.read(infile, region, size, quality_format.format() == SipiQualityFormat::JPG, eng.scaling_quality);
  } catch (const std::bad_alloc &) {
    Metrics::instance().memory_alloc_failures_total.Increment();
    ImageContext sentry_ctx;
    sentry_ctx.input_file = infile;
    sentry_ctx.file_size_bytes = get_file_size(infile);
    sentry_ctx.request_uri = uri;
    capture_image_error("std::bad_alloc during image read", "read", sentry_ctx, SipiMode::Server);
    return std::unexpected(SipiStatus::InternalError);
  } catch (const SipiImageError &err) {
    ImageContext sentry_ctx;
    sentry_ctx.input_file = infile;
    sentry_ctx.file_size_bytes = get_file_size(infile);
    sentry_ctx.request_uri = uri;
    populate_from_image(sentry_ctx, img);
    capture_image_error(err.to_string(), "read", sentry_ctx, SipiMode::Server);
    return std::unexpected(SipiStatus::InternalError);
  } catch (const SipiSizeError &) {
    return std::unexpected(SipiStatus::BadRequest);
  }

  if (mirror || angle != 0.0) {
    if (cancelled()) {
      Metrics::instance().client_disconnected_total.Increment();
      return std::unexpected(SipiStatus::ClientGone);
    }
    try {
      img.rotate(angle, mirror);
    } catch (const std::bad_alloc &) {
      Metrics::instance().memory_alloc_failures_total.Increment();
      return std::unexpected(SipiStatus::InternalError);
    } catch (Sipi::SipiError &err) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      sentry_ctx.request_uri = uri;
      populate_from_image(sentry_ctx, img);
      capture_image_error(err.to_string(), "convert", sentry_ctx, SipiMode::Server);
      return std::unexpected(SipiStatus::InternalError);
    }
  }

  if (quality_format.quality() != SipiQualityFormat::DEFAULT) {
    if (cancelled()) {
      Metrics::instance().client_disconnected_total.Increment();
      return std::unexpected(SipiStatus::ClientGone);
    }
    switch (quality_format.quality()) {
    case SipiQualityFormat::COLOR:
      img.convertToIcc(Icc(icc_sRGB), 8);
      break;
    case SipiQualityFormat::GRAY:
      img.convertToIcc(Icc(icc_GRAY_D50), 8);
      break;
    case SipiQualityFormat::BITONAL:
      img.toBitonal();
      break;
    default:
      return std::unexpected(SipiStatus::BadRequest);
    }
  }

  if (!watermark.empty()) {
    if (cancelled()) {
      Metrics::instance().client_disconnected_total.Increment();
      return std::unexpected(SipiStatus::ClientGone);
    }
    try {
      img.add_watermark(watermark);
    } catch (Sipi::SipiError &err) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      sentry_ctx.request_uri = uri;
      populate_from_image(sentry_ctx, img);
      capture_image_error(err.to_string(), "convert", sentry_ctx, SipiMode::Server);
      return std::unexpected(SipiStatus::InternalError);
    } catch (std::exception &err) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      sentry_ctx.request_uri = uri;
      populate_from_image(sentry_ctx, img);
      capture_image_error(err.what(), "convert", sentry_ctx, SipiMode::Server);
      return std::unexpected(SipiStatus::InternalError);
    }
    log_info("GET %s: adding watermark", uri.c_str());
  }

  if (cancelled()) {
    Metrics::instance().client_disconnected_total.Increment();
    return std::unexpected(SipiStatus::ClientGone);
  }

  // Cache file: probe writability now (a 500 here is still pre-commit), then let
  // the producer's TeeSink fill it during the encode.
  std::string cachefile;
  if (eng.cache != nullptr) {
    cachefile = eng.cache->getNewCacheFileName();
    std::ofstream probe(cachefile, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);
    if (probe.fail()) { return std::unexpected(SipiStatus::InternalError); }
  }

  if (content_type == nullptr) { return std::unexpected(SipiStatus::BadRequest); }

  ServeResponse out;
  out.http_status = 200;
  out.headers = base_headers();
  out.body = StreamBody{ std::make_unique<ImageEncodeProducer>(std::move(img),
    quality_format.format(),
    eng.jpeg_quality,
    eng.cache,
    std::move(cachefile),// the only non-const local here; infile/canonical/uri are const, so copied
    infile,
    canonical,
    uri,
    info,
    std::move(budget_guard)) };
  return out;
}

}// namespace Sipi::ffi
