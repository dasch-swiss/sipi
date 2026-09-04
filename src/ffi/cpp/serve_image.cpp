/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/serve_image.h"
#include "ffi/serve_timings.h"// PhaseTimer + decode-estimate capture, read back by the shell

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>

#include "image/SipiImage.h"
#include "image/SipiImageError.h"
#include "image_processing/processing.h"
#include "cache/SipiCache.h"
#include "throttling/SipiMemoryBudget.h"
#include "throttling/SipiPeakMemory.h"
#include "format_handlers/output_sink.h"
#include "iiifparser/SipiDecodeDims.h"
#include "iiifparser/SipiIdentifier.h"
#include "iiifparser/SipiQualityFormat.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiRotation.h"
#include "iiifparser/SipiSize.h"
#include "logging/logger.h"
#include "metadata/icc.h"
#include "observability/metrics.h"
#include "image/populate_from_image.h"
#include "util/Parsing.h"
#include "util/UrlDecode.h"

namespace Sipi::ffi {
namespace {

  using observability::get_file_size;
  using observability::ImageContext;
  using observability::Metrics;
  using observability::populate_from_image;

  constexpr const char *kCacheControl = "must-revalidate, post-check=0, pre-check=0";

  // Runs `producer()` on a joinable worker thread bounded by `timeout`. Returns
  // the producer's result if it finished in time; `std::nullopt` on timeout — in
  // which case the worker is DETACHED (an unpatchable Kakadu decode hang keeps
  // running until the process restarts, DEV-7080) and `wedged_threads` is
  // incremented. `producer` must capture its inputs BY VALUE and return BY
  // VALUE: on timeout the caller's stack unwinds while the detached worker still
  // owns the packaged_task's shared state, so the worker only ever writes into
  // heap it co-owns (no use-after-free).
  template<class F> std::optional<std::invoke_result_t<F>> run_with_deadline(std::chrono::milliseconds timeout, F producer)
  {
    using T = std::invoke_result_t<F>;
    auto task = std::make_shared<std::packaged_task<T()>>(std::move(producer));
    std::future<T> fut = task->get_future();
    std::thread worker([task] { (*task)(); });
    if (fut.wait_for(timeout) == std::future_status::ready) {
      worker.join();
      return fut.get();
    }
    worker.detach();
    Metrics::instance().wedged_threads.Increment();
    return std::nullopt;
  }

  // Flattens a handled image error into the seam's SipiImageErrorReport and
  // reports it through `report_error` iff non-null — the seam's "NULL =
  // absent" idiom: a caller that passes no callback stays on a log-only path.
  // Never affects the caller's SipiStatus: this is purely a side channel for
  // error reporting.
  // Named *Report*, not *Error*, to avoid colliding with the
  // `Sipi::SipiImageError` exception type this same file catches.
  void report_image_error(SipiReportErrorFn report_error,
    void *report_ctx,
    const std::string &message,
    const std::string &phase,
    const ImageContext &ctx)
  {
    if (report_error == nullptr) { return; }
    SipiImageErrorReport err{};
    err.phase = phase.c_str();
    err.message = message.c_str();
    err.input_file = ctx.input_file.c_str();
    err.output_format = ctx.output_format.c_str();
    err.colorspace = ctx.colorspace.c_str();
    err.icc_profile_type = ctx.icc_profile_type.c_str();
    err.orientation = ctx.orientation.c_str();
    err.width = ctx.width;
    err.height = ctx.height;
    err.channels = ctx.channels;
    err.bps = ctx.bps;
    err.file_size_bytes = ctx.file_size_bytes;
    report_error(report_ctx, &err);
  }

  // The single FormatType ↔ Content-Type table for the served image formats.
  // `content_type_for` and `detect_in_format` are exact inverses and both source
  // from this one table, so the format/mime pairing lives in one place
  // (DUNE-005). The engine's own read-time mime classifier
  // (`SipiImage::getFileType`, SipiImage.cpp) keeps its separate sniff-alias
  // list — unifying the two is format-descriptor-table work, deferred until a
  // fifth format arrives (decision 2; see ARCH-MAP format_handlers entry).
  struct FormatMime
  {
    SipiQualityFormat::FormatType fmt;
    const char *mime;
  };
  constexpr std::array<FormatMime, 4> kFormatMimes = { {
    { SipiQualityFormat::TIF, "image/tiff" },
    { SipiQualityFormat::JPG, "image/jpeg" },
    { SipiQualityFormat::PNG, "image/png" },
    { SipiQualityFormat::JP2, "image/jp2" },
  } };

  // The Content-Type for an emitted IIIF format.
  const char *content_type_for(SipiQualityFormat::FormatType fmt)
  {
    for (const auto &e : kFormatMimes) {
      if (e.fmt == fmt) { return e.mime; }
    }
    return nullptr;
  }

  // The image-root mimetype → input format (matches the legacy sniff). `image/jpx`
  // is the JP2 alias `getFileMimetype` can return; fold it onto the canonical mime
  // before the table lookup.
  SipiQualityFormat::FormatType detect_in_format(const std::string &infile)
  {
    const std::string mime = shttps::Parsing::getFileMimetype(infile).first;
    const std::string canonical = (mime == "image/jpx") ? "image/jp2" : mime;
    for (const auto &e : kFormatMimes) {
      if (canonical == e.mime) { return e.fmt; }
    }
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

  // Builds the Canonical URL (IIIF spec form). Returns it twice-shaped: the
  // `<...>;rel="canonical"` Link-header value (`.first`) and the bare URL
  // (`.second`), which the serve path then uses as the Cache key. Engine-facing,
  // so it lives with the seam.
  std::pair<std::string, std::string> build_canonical_url(size_t tmp_w,
    size_t tmp_h,
    const std::string &scheme,
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
      "<%s://%s/%s/%s/%s/%s/%s/default.%s/%s>;rel=\"canonical\"",
      scheme.c_str(),
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
      std::string cache_key,
      std::string request_uri,
      SipiImgInfo info,
      std::optional<MemoryBudgetGuard> budget_guard,
      SipiReportErrorFn report_error,
      void *report_ctx)
      : budget_guard_(std::move(budget_guard)), img_(std::move(img)), format_(format), jpeg_quality_(jpeg_quality),
        cache_(cache), cachefile_(std::move(cachefile)), infile_(std::move(infile)), cache_key_(std::move(cache_key)),
        request_uri_(std::move(request_uri)), info_(info), report_error_(report_error), report_ctx_(report_ctx)
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

      Result<void> r;
      try {
        // Spans the encode AND the streamed write to the sink, so a slow client's
        // back-pressure counts toward this phase's duration (see SipiServeTimings).
        PhaseTimer phase_timer(SIPI_PHASE_ENCODE);
        switch (format_) {
        case SipiQualityFormat::JPG: {
          SipiCompressionParams qp = { { JPEG_QUALITY, std::to_string(jpeg_quality_) } };
          r = img_.write("jpg", out, &qp);
          break;
        }
        case SipiQualityFormat::JP2:
          r = img_.write("jpx", out);
          break;
        case SipiQualityFormat::TIF:
          r = img_.write("tif", out);
          break;
        case SipiQualityFormat::PNG:
          r = img_.write("png", out);
          break;
        default:
          break;
        }
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

      if (!r) {
        const auto &err = r.error();
        if (caching) { ::unlink(cachefile_.c_str()); }
        if (err.code() == ErrorCode::kClientAbort) {
          // Client closed the socket mid-response (Traefik 499). Not a server
          // error: no Sentry.
          log_info("Client aborted HTTP response for %s", request_uri_.c_str());
          Metrics::instance().client_disconnected_total.Increment();
          return 1;
        }
        if (policy_for(err.code()).sentry_policy != SentryPolicy::kSkip) { capture_write_error(err.diagnostic_message()); }
        log_err("GET %s: error writing image: %s", request_uri_.c_str(), err.diagnostic_message().c_str());
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
      sentry_ctx.output_format = format_type_to_string(format_);
      populate_from_image(sentry_ctx, img_);
      report_image_error(report_error_, report_ctx_, message, "write", sentry_ctx);
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
        cache_key_,
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
    std::string cache_key_;
    std::string request_uri_;
    SipiImgInfo info_;
    // Safe to hold past construction only because `produce()` runs
    // synchronously within the same `sipi_serve_image` call that constructed
    // this producer (the Rust caller is blocked on that call the whole time,
    // per the seam's synchronous contract) — so `report_ctx_` (the Rust-owned
    // request-URI C string, per routes.rs) is still alive whenever `produce()`
    // reads it. Never retain either past this object's lifetime.
    SipiReportErrorFn report_error_;
    void *report_ctx_;
  };

  std::string str_or_empty(const char *s) { return s != nullptr ? std::string(s) : std::string(); }

  // A full-file body for the passthrough / cache-hit paths, stat'd here so a
  // file that vanished after the earlier checks is a clean error rather than a
  // 200 with a wrong length (get_file_size returns 0 on a failed stat, which
  // would underflow send_file's inclusive byte range). A 0-byte file
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

SipiStatus status_for(const SipiValueError &err)
{
  switch (policy_for(err.code()).http_status_class) {
  case HttpStatusClass::kInternalError:
    return SipiStatus::InternalError;
  case HttpStatusClass::kClientError:
    return SipiStatus::BadRequest;
  }
}

void report_value_error(SipiReportErrorFn report_error,
  void *report_ctx,
  const SipiValueError &err,
  const std::string &phase,
  const observability::ImageContext &ctx)
{
  // kSkip marks a failure that is not a server-side fault (e.g. a client
  // abort mid-response): no callback, no Sentry.
  if (policy_for(err.code()).sentry_policy == SentryPolicy::kSkip) { return; }
  report_image_error(report_error, report_ctx, err.diagnostic_message(), phase, ctx);
}

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
  {
    PhaseTimer phase_timer(SIPI_PHASE_SHAPE);
    auto deadline_result = run_with_deadline(std::chrono::milliseconds(eng.decode_timeout_ms), [infile] {
      SipiImage probe;
      return probe.read_shape(infile);
    });
    if (!deadline_result) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      report_image_error(
        req.report_error, req.report_ctx, "JP2 decode deadline exceeded (read_shape)", "read", sentry_ctx);
      return std::unexpected(SipiStatus::InternalError);
    }
    auto &shape = *deadline_result;
    if (!shape) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      report_value_error(req.report_error, req.report_ctx, shape.error(), "read", sentry_ctx);
      return std::unexpected(status_for(shape.error()));
    }
    // read_shape() converts an all-handlers-refused probe into an error value
    // before returning, so a successfully unwrapped SipiImgInfo is always valid.
    info = *shape;
  }

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

  size_t rest_w{ 0 }, rest_h{ 0 };
  try {
    restricted_size->get_size(img_w, img_h, rest_w, rest_h, tmp_red, tmp_ro);
  } catch (Sipi::SipiSizeError &) {
    return std::unexpected(SipiStatus::BadRequest);
  } catch (Sipi::SipiError &) {
    return std::unexpected(SipiStatus::BadRequest);
  }
  // A restrict decision that does not reduce resolution and adds no watermark
  // is not a restriction — refuse it rather than pass the original through at
  // full fidelity (S2-09). Catches size=max, size=pct:100, ^pct:200 (upscale).
  // `req.restricted_size` (not `restricted_size->undefined()`) is the signal
  // that a size was actually requested: "max" parses to the same `undefined()`
  // shape as "no restriction", but still resolves rest_w/rest_h to the full
  // image extent above. (A bare {type='restrict'} with no size and no
  // watermark arrives with a null restricted_size and is refused on the Rust
  // edge before the seam.)
  if (req.restricted_size != nullptr && rest_w >= img_w && rest_h >= img_h && watermark.empty()) {
    return std::unexpected(SipiStatus::Forbidden);
  }
  if (!restricted_size->undefined()) {
    // The restricted-size cap must bound the effective SAMPLING FACTOR of ANY
    // region, not the full-image output box: comparing full-image boxes (the
    // previous check) lets a client reconstruct the full-resolution image by
    // requesting many native-scale REGIONS, since each region's own absolute
    // output stays below the restricted full-image box and the cap never
    // fires. `f` is the fraction of native resolution the restricted profile
    // may reveal; the requested output for THIS region is capped to `f` times
    // that region's own pixel extent, so the cap composes across regions
    // where an absolute output box does not. We SCALE, not reject — rejecting
    // region requests on a restricted image breaks IIIF viewers that tile
    // through the image.
    //
    // Two residuals accepted here, not fixed by this change:
    //  - Many overlapping, sub-pixel-offset regions each resampled at `f`
    //    from the full-resolution source form a super-resolution attack that
    //    can recover more detail than a single `f`-scaled image. Accepted:
    //    rejecting region requests on restricted images breaks viewers.
    //  - The canonical `Link` header still emits the region in native pixel
    //    coordinates (`SipiRegion::canonical`), so e.g. `pct:0,0,100,100`
    //    discloses the native width/height even though the served bytes are
    //    capped. Recommendation for a later finding: omit the canonical Link
    //    for non-Allow permissions.
    const double f = std::min(static_cast<double>(rest_w) / static_cast<double>(img_w),
      static_cast<double>(rest_h) / static_cast<double>(img_h));

    int region_x{ 0 }, region_y{ 0 };
    size_t region_w{ 0 }, region_h{ 0 };
    try {
      region->crop_coords(img_w, img_h, region_x, region_y, region_w, region_h);
    } catch (Sipi::SipiError &) {
      return std::unexpected(SipiStatus::BadRequest);
    }

    size_t out_w{ 0 }, out_h{ 0 };
    int out_reduce{ 0 };
    bool out_redonly{ false };
    try {
      size->get_size(region_w, region_h, out_w, out_h, out_reduce, out_redonly);
    } catch (Sipi::SipiSizeError &) {
      return std::unexpected(SipiStatus::BadRequest);
    } catch (Sipi::SipiError &) {
      return std::unexpected(SipiStatus::BadRequest);
    }

    if (static_cast<double>(out_w) > static_cast<double>(region_w) * f
        || static_cast<double>(out_h) > static_cast<double>(region_h) * f) {
      size = std::make_shared<SipiSize>(static_cast<float>(f * 100.0));
    }
  }

  // Canonical URL (Link header + cache key). The Link header honours
  // X-Forwarded-Proto (SIPI serves plain HTTP behind Traefik); the cache key
  // stays scheme-free so an http and an https request share one cache entry.
  const std::string cannonical_watermark = watermark.empty() ? "0" : "1";
  const char *forwarded_proto = req.forwarded_proto;
  const std::string scheme =
    (forwarded_proto != nullptr && *forwarded_proto != '\0') ? std::string(forwarded_proto) : std::string("http");
  std::pair<std::string, std::string> canonical_info;
  try {
    canonical_info = build_canonical_url(
      img_w, img_h, scheme, str_or_empty(req.forwarded_host), str_or_empty(req.prefix), sid.getIdentifier(),
      region, size, rotation, quality_format, sid.getPage(), cannonical_watermark);
  } catch (Sipi::SipiError &) {
    return std::unexpected(SipiStatus::BadRequest);
  }
  const std::string canonical_header = canonical_info.first;
  // The Canonical URL used verbatim as the Cache key (this serve path adds no
  // watermark suffix); `SipiCache` keys its table by this string.
  const std::string cache_key = canonical_info.second;
  const char *content_type = content_type_for(quality_format.format());

  auto base_headers = [&] {
    std::vector<Header> h;
    h.emplace_back("Cache-Control", kCacheControl);
    // IIIF Image API 3.0 profileLinkHeader (advertised in info.json's extraFeatures):
    // fold the profile Link into the canonical Link's value (one header) — the Rust
    // response sink is last-write-wins per header name, so two separate "Link"
    // entries would drop one.
    h.emplace_back("Link", canonical_header + R"(, <http://iiif.io/api/image/3/level2.json>;rel="profile")");
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
    const std::string cachefile = eng.cache->check(infile, cache_key, true);
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

  // Estimated peak decode memory for this serve. Recorded for every decode —
  // handed back over the seam accumulator into the shell's OTLP histogram —
  // independently of whether the budget is enforced: the estimate describes the
  // request, not the budget feature.
  const auto ddims = compute_decode_dims(img_w, img_h, info.clevels, region, size);
  const bool needs_icc = quality_format.quality() == SipiQualityFormat::COLOR
                         || quality_format.quality() == SipiQualityFormat::GRAY;
  const size_t estimated = estimate_peak_memory(
    ddims.width, ddims.height, ddims.out_w, ddims.out_h, info.nc, info.bps, static_cast<double>(angle), needs_icc);

  auto &metrics = Metrics::instance();
  serve_timings_set_decode_estimate(static_cast<std::uint64_t>(estimated));

  // The full-lane memory budget accounts only full-lane decodes: those whose
  // estimated peak memory reaches the large-decode threshold. Tile decodes
  // (below the threshold) bypass the budget entirely and are never charged, so a
  // tile is never rejected for full-lane memory pressure.
  std::optional<MemoryBudgetGuard> budget_guard;
  const bool is_full_lane = estimated >= eng.large_decode_threshold_bytes;
  if (eng.memory_budget != nullptr && is_full_lane) {
    const auto result = eng.memory_budget->try_acquire(estimated);
    metrics.decode_memory_used_bytes.Set(static_cast<double>(result.used));

    if (result.allowed && !result.over_budget) {
      metrics.decode_memory_acquired.Increment();
    } else if (result.allowed && result.over_budget) {
      // BASIC: admit, but shadow-count the would-be rejection — distinguishing
      // the permanently-unservable (would-be 413) case from the transient one.
      if (result.exceeds_budget_alone) {
        metrics.decode_memory_shadow_too_large_total.Increment();
      } else {
        metrics.decode_memory_shadow_rejected.Increment();
      }
      log_warn("Full-lane memory budget over limit (basic): %zu / %zu bytes for %s",
        result.used, result.budget, uri.c_str());
    } else if (result.exceeds_budget_alone) {
      // ADVANCED, permanently unservable: a single request whose estimate alone
      // exceeds the full-lane budget can never succeed — 413, no Retry-After.
      metrics.decode_memory_too_large_total.Increment();
      log_warn("Request estimate %zu exceeds full-lane budget %zu; rejecting (413): %s",
        estimated, result.budget, uri.c_str());
      ServeResponse out;
      out.http_status = 413;
      out.body = EmptyBody{};
      return out;
    } else {
      // ADVANCED, transient: the full lane is currently exhausted — 503 + Retry-After.
      metrics.decode_memory_rejected.Increment();
      log_warn("Full-lane memory budget exhausted (advanced): %zu / %zu bytes, rejecting %s",
        result.used, result.budget, uri.c_str());
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

  // The producer's own SipiImage / decode result, carried out by value so the
  // deadline helper's timeout path never touches this stack (see
  // run_with_deadline's lifetime-safety note).
  struct DecodeOutcome
  {
    SipiImage img;
    Result<void> status;
  };

  SipiImage img;
  try {
    PhaseTimer phase_timer(SIPI_PHASE_DECODE);
    const bool force_bps_8 = quality_format.format() == SipiQualityFormat::JPG;
    auto deadline_result = run_with_deadline(std::chrono::milliseconds(eng.decode_timeout_ms),
      [infile, region, size, force_bps_8, scaling_quality = eng.scaling_quality] {
        SipiImage decoded;
        auto status = decoded.read(infile, region, size, force_bps_8, scaling_quality);
        return DecodeOutcome{ std::move(decoded), std::move(status) };
      });
    if (!deadline_result) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      report_image_error(req.report_error, req.report_ctx, "JP2 decode deadline exceeded (read)", "read", sentry_ctx);
      return std::unexpected(SipiStatus::InternalError);
    }
    img = std::move(deadline_result->img);
    if (auto &r = deadline_result->status; !r) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      populate_from_image(sentry_ctx, img);
      report_value_error(req.report_error, req.report_ctx, r.error(), "read", sentry_ctx);
      return std::unexpected(status_for(r.error()));
    }
  } catch (const std::bad_alloc &) {
    Metrics::instance().memory_alloc_failures_total.Increment();
    ImageContext sentry_ctx;
    sentry_ctx.input_file = infile;
    sentry_ctx.file_size_bytes = get_file_size(infile);
    report_image_error(req.report_error, req.report_ctx, "std::bad_alloc during image read", "read", sentry_ctx);
    return std::unexpected(SipiStatus::InternalError);
  } catch (const SipiImageError &err) {
    // Covers the allocation-guard and codec-boundary throws that stay exception-based
    // (checked_buf_size_or_throw, memTiffOpen, Kakadu), not decode failures.
    ImageContext sentry_ctx;
    sentry_ctx.input_file = infile;
    sentry_ctx.file_size_bytes = get_file_size(infile);
    populate_from_image(sentry_ctx, img);
    report_image_error(req.report_error, req.report_ctx, err.to_string(), "read", sentry_ctx);
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
      PhaseTimer phase_timer(SIPI_PHASE_ROTATE);
      if (auto r = processing::rotate(img, angle, mirror); !r) {
        ImageContext sentry_ctx;
        sentry_ctx.input_file = infile;
        sentry_ctx.file_size_bytes = get_file_size(infile);
        populate_from_image(sentry_ctx, img);
        report_value_error(req.report_error, req.report_ctx, r.error(), "convert", sentry_ctx);
        return std::unexpected(status_for(r.error()));
      }
    } catch (const std::bad_alloc &) {
      Metrics::instance().memory_alloc_failures_total.Increment();
      return std::unexpected(SipiStatus::InternalError);
    } catch (Sipi::SipiError &err) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      populate_from_image(sentry_ctx, img);
      report_image_error(req.report_error, req.report_ctx, err.to_string(), "convert", sentry_ctx);
      return std::unexpected(SipiStatus::InternalError);
    }
  }

  if (quality_format.quality() != SipiQualityFormat::DEFAULT) {
    if (cancelled()) {
      Metrics::instance().client_disconnected_total.Increment();
      return std::unexpected(SipiStatus::ClientGone);
    }
    try {
      PhaseTimer phase_timer(SIPI_PHASE_QUALITY);
      Result<void> r;
      switch (quality_format.quality()) {
      case SipiQualityFormat::COLOR:
        r = processing::convertToIcc(img, Icc(icc_sRGB), 8);
        break;
      case SipiQualityFormat::GRAY:
        r = processing::convertToIcc(img, Icc(icc_GRAY_D50), 8);
        break;
      case SipiQualityFormat::BITONAL:
        r = processing::toBitonal(img);
        break;
      default:
        return std::unexpected(SipiStatus::BadRequest);
      }
      if (!r) {
        ImageContext sentry_ctx;
        sentry_ctx.input_file = infile;
        sentry_ctx.file_size_bytes = get_file_size(infile);
        populate_from_image(sentry_ctx, img);
        report_value_error(req.report_error, req.report_ctx, r.error(), "convert", sentry_ctx);
        return std::unexpected(status_for(r.error()));
      }
    } catch (const std::bad_alloc &) {
      Metrics::instance().memory_alloc_failures_total.Increment();
      return std::unexpected(SipiStatus::InternalError);
    } catch (Sipi::SipiError &err) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      populate_from_image(sentry_ctx, img);
      report_image_error(req.report_error, req.report_ctx, err.to_string(), "convert", sentry_ctx);
      return std::unexpected(SipiStatus::InternalError);
    }
  }

  if (!watermark.empty()) {
    if (cancelled()) {
      Metrics::instance().client_disconnected_total.Increment();
      return std::unexpected(SipiStatus::ClientGone);
    }
    try {
      PhaseTimer phase_timer(SIPI_PHASE_WATERMARK);
      if (auto r = processing::add_watermark(img, watermark); !r) {
        ImageContext sentry_ctx;
        sentry_ctx.input_file = infile;
        sentry_ctx.file_size_bytes = get_file_size(infile);
        populate_from_image(sentry_ctx, img);
        report_value_error(req.report_error, req.report_ctx, r.error(), "convert", sentry_ctx);
        return std::unexpected(status_for(r.error()));
      }
    } catch (Sipi::SipiError &err) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      populate_from_image(sentry_ctx, img);
      report_image_error(req.report_error, req.report_ctx, err.to_string(), "convert", sentry_ctx);
      return std::unexpected(SipiStatus::InternalError);
    } catch (std::exception &err) {
      ImageContext sentry_ctx;
      sentry_ctx.input_file = infile;
      sentry_ctx.file_size_bytes = get_file_size(infile);
      populate_from_image(sentry_ctx, img);
      report_image_error(req.report_error, req.report_ctx, err.what(), "convert", sentry_ctx);
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
    std::move(cachefile),// the only non-const local here; infile/cache_key/uri are const, so copied
    infile,
    cache_key,
    uri,
    info,
    std::move(budget_guard),
    req.report_error,
    req.report_ctx) };
  return out;
}

}// namespace Sipi::ffi
