/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SipiConf.h"
#include "Logger.h"
#include <stdexcept>
#include <string>
#include <thread>

namespace Sipi {

long long parseSizeString(const std::string &str)
{
  if (str.empty()) return 0;
  if (str == "-1") return -1;

  size_t l = str.length();
  char c = str[l - 1];
  if (c == 'M' || c == 'm') {
    return std::stoll(str.substr(0, l - 1)) * 1024 * 1024;
  } else if (c == 'G' || c == 'g') {
    return std::stoll(str.substr(0, l - 1)) * 1024 * 1024 * 1024;
  } else {
    return std::stoll(str);
  }
}

SipiConf::SipiConf() {}

SipiConf::SipiConf(shttps::LuaServer &luacfg)
{
  userid_str = luacfg.configString("sipi", "userid", "");
  hostname = luacfg.configString("sipi", "hostname", "localhost");
  port = luacfg.configInteger("sipi", "port", 3333);

  ssl_port = luacfg.configInteger("sipi", "ssl_port", -1);
  ssl_certificate = luacfg.configString("sipi", "ssl_certificate", "");
  ssl_key = luacfg.configString("sipi", "ssl_key", "");

  img_root = luacfg.configString("sipi", "imgroot", ".");
  max_temp_file_age = luacfg.configInteger("sipi", "max_temp_file_age", 86400);
  subdir_levels = luacfg.configInteger("sipi", "subdir_levels", 0);
  subdir_excludes = luacfg.configStringList("sipi", "subdir_excludes");
  // has no defaults, returns an empty vector if nothing is there
  prefix_as_path = luacfg.configBoolean("sipi", "prefix_as_path", true);
  jpeg_quality = luacfg.configInteger("sipi", "jpeg_quality", 80);

  std::map<std::string, std::string> default_scaling_quality = {
    { "jpeg", "medium" }, { "tiff", "high" }, { "png", "high" }, { "j2k", "high" }
  };
  scaling_quality = luacfg.configStringTable("sipi", "scaling_quality", default_scaling_quality);
  init_script = luacfg.configString("sipi", "initscript", ".");

  // --- cache_dir: new key first, then deprecated key ---
  std::string cache_dir_new = luacfg.configString("sipi", "cache_dir", "");
  std::string cache_dir_old = luacfg.configString("sipi", "cachedir", "");
  if (!cache_dir_new.empty() && !cache_dir_old.empty()) {
    throw std::runtime_error("ERROR: Both 'cachedir' and 'cache_dir' specified. Remove the deprecated 'cachedir' key.");
  } else if (!cache_dir_new.empty()) {
    cache_dir = cache_dir_new;
  } else if (!cache_dir_old.empty()) {
    log_warn("Config key 'cachedir' is deprecated. Use 'cache_dir' instead.");
    cache_dir = cache_dir_old;
  } else {
    cache_dir = "./cache";
  }

  // --- cache_size: new key first, then deprecated key ---
  std::string cache_size_new = luacfg.configString("sipi", "cache_size", "");
  std::string cache_size_old = luacfg.configString("sipi", "cachesize", "");
  std::string cachesize_str;
  if (!cache_size_new.empty() && !cache_size_old.empty()) {
    throw std::runtime_error("ERROR: Both 'cachesize' and 'cache_size' specified. Remove the deprecated 'cachesize' key.");
  } else if (!cache_size_new.empty()) {
    cachesize_str = cache_size_new;
  } else if (!cache_size_old.empty()) {
    log_warn("Config key 'cachesize' is deprecated. Use 'cache_size' instead.");
    cachesize_str = cache_size_old;
  } else {
    cachesize_str = "200M"; // default
  }

  // Parse cache_size string: "-1" = unlimited, "0" = disabled, "200M" / "1G" = limit
  cache_size = parseSizeString(cachesize_str);

  if (cache_size < -1) {
    throw std::runtime_error("ERROR: Invalid cache_size value '" + cachesize_str
      + "'. Use '-1' (unlimited), '0' (disabled), or a positive value like '200M'.");
  }

  // --- cache_nfiles ---
  cache_n_files = luacfg.configInteger("sipi", "cache_nfiles", 200);

  // --- cache_hysteresis: warn if present, no longer supported ---
  // Use sentinel default -1.0 to detect if the key is explicitly set
  float hysteresis_check = luacfg.configFloat("sipi", "cache_hysteresis", -1.0f);
  if (hysteresis_check >= 0.0f) {
    log_warn("Config key 'cache_hysteresis' is no longer supported (replaced by built-in 80%% low-water mark). Remove it from your config.");
  }

  keep_alive = luacfg.configInteger("sipi", "keep_alive", 20);
  thumb_size = luacfg.configString("sipi", "thumb_size", "!128,128");
  n_threads = luacfg.configInteger("sipi", "nthreads", 0);// 0 = auto-detect from CPU cores
  std::string max_post_size_str = luacfg.configString("sipi", "max_post_size", "0");
  long long parsed_post_size = parseSizeString(max_post_size_str);
  if (parsed_post_size < 0) parsed_post_size = 0;
  max_post_size = static_cast<size_t>(parsed_post_size);

  tmp_dir = luacfg.configString("sipi", "tmpdir", "/tmp");
  scriptdir = luacfg.configString("sipi", "scriptdir", "./scripts");
  jwt_secret = luacfg.configString("sipi", "jwt_secret", "");
  knora_path = luacfg.configString("sipi", "knora_path", "localhost");
  knora_port = luacfg.configString("sipi", "knora_port", "3333");
  loglevel = luacfg.configString("sipi", "loglevel", "WARN");
  logfile = luacfg.configString("sipi", "logfile", "sipi.log");
  adminuser = luacfg.configString("admin", "user", "");
  password = luacfg.configString("admin", "password", "");
  long long parsed_pixel_limit = luacfg.configInteger("sipi", "max_pixel_limit", 0);
  if (parsed_pixel_limit < 0) parsed_pixel_limit = 0;
  max_pixel_limit = static_cast<size_t>(parsed_pixel_limit);

  // Rate limiter configuration
  long long parsed_rl_max = luacfg.configInteger("sipi", "rate_limit_max_pixels", 0);
  if (parsed_rl_max < 0) parsed_rl_max = 0;
  rate_limit_max_pixels = static_cast<size_t>(parsed_rl_max);

  long long parsed_rl_window = luacfg.configInteger("sipi", "rate_limit_window", 600);
  if (parsed_rl_window < 1) parsed_rl_window = 600;
  rate_limit_window = static_cast<unsigned>(parsed_rl_window);

  rate_limit_mode_str = luacfg.configString("sipi", "rate_limit_mode", "off");

  long long parsed_rl_threshold = luacfg.configInteger("sipi", "rate_limit_pixel_threshold", 2000000);
  if (parsed_rl_threshold < 0) parsed_rl_threshold = 0;
  rate_limit_pixel_threshold = static_cast<size_t>(parsed_rl_threshold);

  long long parsed_drain_timeout = luacfg.configInteger("sipi", "drain_timeout", 30);
  if (parsed_drain_timeout < 1) parsed_drain_timeout = 30;
  drain_timeout = static_cast<unsigned>(parsed_drain_timeout);

  routes = luacfg.configRoute("routes");
  docroot = luacfg.configString("fileserver", "docroot", "");
  wwwroute = luacfg.configString("fileserver", "wwwroute", "");
}

}// namespace Sipi
