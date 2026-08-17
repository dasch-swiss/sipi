/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \ Handler of various
 *
 */
#ifndef __sipi_conf_h
#define __sipi_conf_h

#include "scripting/LuaServer.h"

namespace Sipi {

/*!
 * Parse a human-readable size string into bytes.
 * Supports suffixes: 'M' (megabytes), 'G' (gigabytes).
 * Returns the value in bytes. "-1" returns -1 (unlimited).
 * Throws std::invalid_argument on parse failure.
 */
long long parseSizeString(const std::string &str);

/*!
 * This class is used to read the sipi server configuration from
 * a Lua configuration file.
 */
class SipiConf
{
private:
  // In-class initializers below carry the same defaults the Lua ctor applies, so
  // a default-constructed SipiConf (the base for the Lua-less / TOML `sipi_init`
  // path) is a valid all-defaults config rather than indeterminate.
  // `hostname`/`ssl_port` are not consumed by the engine, but stay because Lua
  // route scripts read them via the `config` table (e.g. building preview URLs).
  std::string hostname{ "localhost" };
  int port{ 3333 };//<! port number for server
  int ssl_port{ -1 };

  std::string img_root;//<! path to root of image repository
  int max_temp_file_age{ 86400 };
  bool prefix_as_path{ true };//<! Use IIIF-prefix as part of path or ignore it...
  int jpeg_quality{ 80 };
  std::map<std::string, std::string> scaling_quality;
  std::string init_script;
  std::string cache_dir;
  long long cache_size{ 200LL * 1024 * 1024 };// 200M (the Lua-config default)
  std::string thumb_size;
  size_t cache_n_files{ 200 };
  size_t max_post_size{ 0 }; // 0 = unlimited
  std::string tmp_dir;
  std::string scriptdir;
  std::vector<shttps::LuaRoute> routes;
  std::string knora_path;
  std::string knora_port;
  std::string loglevel;
  std::string docroot;
  std::string wwwroute;
  std::string jwt_secret;
  std::string adminuser;
  std::string password;
  size_t memory_limit{ 0 };                       //!< total RAM envelope; 0 = auto (detect available RAM)
  std::string admission_mode_str{ "basic" };    //!< "basic", "advanced"
  double tiles_memory_ratio{ 0.25 };              //!< fraction of the envelope reserved for tiles + non-decode floor; the full lane gets envelope × (1 − ratio)
  unsigned drain_timeout{ 30 }; //!< seconds to wait for in-flight requests during shutdown

public:
  SipiConf();

  explicit SipiConf(shttps::LuaServer &luacfg);

  std::string getHostname() { return hostname; }
  void setHostname(const std::string &str) { hostname = str; }

  int getPort() const { return port; }
  void setPort(int i) { port = i; }

  int getSSLPort() const { return ssl_port; }
  void setSSLPort(int i) { ssl_port = i; }

  std::string getImgRoot() { return img_root; }
  void setImgRoot(const std::string &str) { img_root = str; }

  int getMaxTempFileAge() const { return max_temp_file_age; }
  void setMaxTempFileAge(int i) { max_temp_file_age = i; }

  bool getPrefixAsPath() const { return prefix_as_path; }
  void setPrefixAsPath(bool b) { prefix_as_path = b; }

  int getJpegQuality() const { return jpeg_quality; }
  void setJpegQuality(int i) { jpeg_quality = i; }

  std::map<std::string, std::string> getScalingQuality() { return scaling_quality; }
  void setScalingQuality(const std::map<std::string, std::string> &v) { scaling_quality = v; }

  std::string getInitScript() { return init_script; }
  void setInitScript(const std::string &str) { init_script = str; }

  long long getCacheSize() const { return cache_size; }
  void setCacheSize(long long i) { cache_size = i; }

  std::string getCacheDir() { return cache_dir; }
  void setCacheDir(const std::string &str) { cache_dir = str; }

  std::string getThumbSize() { return thumb_size; }
  void setThumbSize(const std::string &str) { thumb_size = str; }

  size_t getCacheNFiles() const { return cache_n_files; }
  void setCacheNFiles(size_t i) { cache_n_files = i; }

  size_t getMaxPostSize() const { return max_post_size; }
  void setMaxPostSize(const size_t i) { max_post_size = i; }

  std::string getTmpDir() { return tmp_dir; }
  void setTmpDir(const std::string &str) { tmp_dir = str; }

  std::string getScriptDir() { return scriptdir; }
  void setScriptDir(const std::string &str) { scriptdir = str; }

  std::vector<shttps::LuaRoute> getRoutes() { return routes; }
  void seRoutes(const std::vector<shttps::LuaRoute> &r) { routes = r; }

  std::string getKnoraPath() { return knora_path; }
  void setKnoraPath(const std::string &str) { knora_path = str; }

  std::string getKnoraPort() { return knora_port; }
  void setKnoraPort(const std::string &str) { knora_port = str; }

  std::string getLoglevel() { return loglevel; }
  void setLogLevel(const std::string &str) { loglevel = str; }

  std::string getDocRoot() { return docroot; }
  void setDocRoot(const std::string &str) { docroot = str; }

  std::string getWWWRoute() { return wwwroute; }
  void setWWWRoute(const std::string &str) { wwwroute = str; }

  std::string getJwtSecret() { return jwt_secret; }
  void setJwtSecret(const std::string &str) { jwt_secret = str; }

  std::string getAdminUser() { return adminuser; }
  void setAdminUser(const std::string &str) { adminuser = str; }

  std::string getPassword() { return password; }
  inline void setPasswort(const std::string &str) { password = str; }

  size_t getMemoryLimit() const { return memory_limit; }
  void setMemoryLimit(size_t v) { memory_limit = v; }

  std::string getAdmissionMode() const { return admission_mode_str; }
  void setAdmissionMode(const std::string &s) { admission_mode_str = s; }

  double getTilesMemoryRatio() const { return tiles_memory_ratio; }
  void setTilesMemoryRatio(double v) { tiles_memory_ratio = v; }

  unsigned getDrainTimeout() const { return drain_timeout; }
  void setDrainTimeout(unsigned v) { drain_timeout = v; }
};

}// namespace Sipi


#endif
