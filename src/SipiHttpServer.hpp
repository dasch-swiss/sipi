/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file implements a Webserver using mongoose (See \url https://github.com/cesanta/mongoose)
 *
 * {scheme}://{server}{/prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format}
 *
 * We support cross domain scripting (CORS according to \url http://www.html5rocks.com/en/tutorials/cors/)
 */
#ifndef _defined_sipihttp_server_h
#define _defined_sipihttp_server_h

#include <chrono>
#include <memory>
#include <string>

#include "SipiCache.h"
#include "SipiRateLimiter.h"
#include "iiifparser/SipiQualityFormat.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiRotation.h"
#include "iiifparser/SipiSize.h"
#include "shttps/Server.h"

#include "SipiIO.h"


namespace Sipi {
/*!
 * The class SipiHttpServer implements a webserver that can be used to serve images using the IIIF
 * API. For details on the API look for  \url http://iiif.io . I implemented support for
 * cross domain scripting (CORS according to \url http://www.html5rocks.com/en/tutorials/cors/). As a
 * special feature we support acces to the old PHP-based salsah version (this is a bad hack!)
 */
class SipiHttpServer : public shttps::Server
{
private:
protected:
  pid_t _pid{};
  std::string _imgroot;
  std::string _salsah_prefix;
  bool _prefix_as_path{};
  std::vector<std::string> _dirs_to_exclude;
  //!< Directories which should have no subdirs even if subdirs are enabled
  std::string _logfile;
  std::shared_ptr<SipiCache> _cache;
  int _jpeg_quality{};
  std::unordered_map<std::string, SipiCompressionParams> _j2k_compression_profiles;
  ScalingQuality _scaling_quality{};
  size_t _max_pixel_limit{ 0 };//!< max output pixels (w*h) per request, 0 = unlimited
  std::string _resolved_imgroot;  //!< realpath()-resolved image root, set at startup
  std::unique_ptr<SipiRateLimiter> _rate_limiter; //!< Per-client rate limiter (nullptr = disabled)
  std::chrono::steady_clock::time_point _start_time; //!< Server start time for health endpoint

public:
  /*!
   * Constructor which automatically starts the server
   *
   * \param port_p Portnumber on which the server should listen
   * \param nthreads_p Number of threads to be used for the server
   * \param userid_str Userid under which the server should run
   * \param logfile_p Name of the logfile
   * \param loglevel_p Loglevel (DEBUG, INFO, WARNING, ERROR, CRITICAL)
   */
  explicit SipiHttpServer(int port_p,
    size_t nthreads_p = 4,
    std::string userid_str = "",
    const std::string &logfile_p = "sipi.log",
    const std::string &loglevel_p = "DEBUG");

  void run() override;

  static std::pair<std::string, std::string> get_canonical_url(size_t img_w,
    size_t img_h,
    const std::string &host,
    const std::string &prefix,
    const std::string &identifier,
    std::shared_ptr<SipiRegion> region,
    std::shared_ptr<SipiSize> size,
    SipiRotation &rotation,
    SipiQualityFormat &quality_format,
    int pagenum = 0,
    const std::string &canonnical_watermark = "0");


  pid_t pid() const { return _pid; }

  void imgroot(const std::string &imgroot_p) { _imgroot = imgroot_p; }

  std::string imgroot() { return _imgroot; }

  std::string salsah_prefix() { return _salsah_prefix; }

  void salsah_prefix(const std::string &salsah_prefix) { _salsah_prefix = salsah_prefix; }

  bool prefix_as_path() const { return _prefix_as_path; }

  void prefix_as_path(const bool prefix_as_path_p) { _prefix_as_path = prefix_as_path_p; }

  std::vector<std::string> dirs_to_exclude() { return _dirs_to_exclude; }

  void dirs_to_exclude(const std::vector<std::string> &dirs_to_exclude) { _dirs_to_exclude = dirs_to_exclude; }

  void jpeg_quality(const int jpeg_quality_p) { _jpeg_quality = jpeg_quality_p; }

  void j2k_compression_profiles(const std::unordered_map<std::string, SipiCompressionParams> &j2k_compression_profiles)
  {
    _j2k_compression_profiles = j2k_compression_profiles;
  }

  int jpeg_quality() const { return _jpeg_quality; }


  void scaling_quality(std::map<std::string, std::string> jpeg_quality_p)
  {
    if (jpeg_quality_p["jpk"] == "high") {
      _scaling_quality.jk2 = ScalingMethod::HIGH;
    } else if (jpeg_quality_p["jpk"] == "medium") {
      _scaling_quality.jk2 = ScalingMethod::MEDIUM;
    } else if (jpeg_quality_p["jpk"] == "low") {
      _scaling_quality.jk2 = ScalingMethod::LOW;
    } else {
      _scaling_quality.jk2 = ScalingMethod::HIGH;
    }

    if (jpeg_quality_p["jpeg"] == "high") {
      _scaling_quality.jpeg = ScalingMethod::HIGH;
    } else if (jpeg_quality_p["jpeg"] == "medium") {
      _scaling_quality.jpeg = ScalingMethod::MEDIUM;
    } else if (jpeg_quality_p["jpeg"] == "low") {
      _scaling_quality.jpeg = ScalingMethod::LOW;
    } else {
      _scaling_quality.jpeg = ScalingMethod::HIGH;
    }

    if (jpeg_quality_p["tiff"] == "high") {
      _scaling_quality.tiff = ScalingMethod::HIGH;
    } else if (jpeg_quality_p["tiff"] == "medium") {
      _scaling_quality.tiff = ScalingMethod::MEDIUM;
    } else if (jpeg_quality_p["tiff"] == "low") {
      _scaling_quality.tiff = ScalingMethod::LOW;
    } else {
      _scaling_quality.tiff = ScalingMethod::HIGH;
    }

    if (jpeg_quality_p["png"] == "high") {
      _scaling_quality.png = ScalingMethod::HIGH;
    } else if (jpeg_quality_p["png"] == "medium") {
      _scaling_quality.png = ScalingMethod::MEDIUM;
    } else if (jpeg_quality_p["png"] == "low") {
      _scaling_quality.png = ScalingMethod::LOW;
    } else {
      _scaling_quality.png = ScalingMethod::HIGH;
    }
  }

  ScalingQuality scaling_quality() const { return _scaling_quality; }

  void max_pixel_limit(size_t v) { _max_pixel_limit = v; }
  size_t max_pixel_limit() const { return _max_pixel_limit; }

  std::string resolved_imgroot() const { return _resolved_imgroot; }

  void rate_limiter(std::unique_ptr<SipiRateLimiter> rl) { _rate_limiter = std::move(rl); }
  SipiRateLimiter *rate_limiter() { return _rate_limiter.get(); }

  std::chrono::steady_clock::time_point start_time() const { return _start_time; }

  void cache(const std::string &cachedir_p,
    long long max_cache_size_p = -1,
    unsigned max_nfiles_p = 0);

  std::shared_ptr<SipiCache> cache() { return _cache; }
};
}// namespace Sipi

#endif
