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

#include "shttps/Connection.h"
#include "shttps/LuaServer.h"

namespace Sipi {

/*!
 * This class is used to read the sipi server configuration from
 * a Lua configuration file.
 */
class SipiConf
{
private:
  std::string userid_str;
  std::string hostname;
  int port;//<! port number for server
  int ssl_port = -1;
  std::string ssl_certificate;
  std::string ssl_key;

  std::string img_root;//<! path to root of image repository
  int max_temp_file_age;
  int subdir_levels = -1;
  std::vector<std::string> subdir_excludes;
  bool prefix_as_path;//<! Use IIIF-prefix as part of path or ignore it...
  int jpeg_quality;
  std::map<std::string, std::string> scaling_quality;
  std::string init_script;
  std::string cache_dir;
  size_t cache_size;
  float cache_hysteresis;
  int keep_alive;
  std::string thumb_size;
  size_t cache_n_files;
  size_t n_threads;
  size_t max_post_size;
  std::string tmp_dir;
  std::string scriptdir;
  std::vector<shttps::LuaRoute> routes;
  std::string knora_path;
  std::string knora_port;
  std::string logfile;
  std::string loglevel;
  std::string docroot;
  std::string wwwroute;
  std::string jwt_secret;
  std::string adminuser;
  std::string password;

public:
  SipiConf();

  explicit SipiConf(shttps::LuaServer &luacfg);

  std::string getUseridStr() { return userid_str; }
  void setUseridStr(const std::string &str) { userid_str = str; };

  std::string getHostname() { return hostname; }
  void setHostname(const std::string &str) { hostname = str; }

  int getPort() const { return port; }
  void setPort(int i) { port = i; }

  int getSSLPort() const { return ssl_port; }
  void setSSLPort(const int i) { ssl_port = i; }

  std::string getSSLCertificate() { return ssl_certificate; }
  void setSSLCertificate(const std::string &str) { ssl_certificate = str; }

  std::string getSSLKey() { return ssl_key; }
  void setSSLKey(const std::string &str) { ssl_key = str; }

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

  int getSubdirLevels() const { return subdir_levels; }
  void setSubdirLevels(const int i) { subdir_levels = i; }

  std::vector<std::string> getSubdirExcludes() { return subdir_excludes; }
  void setSubdirExcludes(const std::vector<std::string> &v) { subdir_excludes = v; }

  std::string getInitScript() { return init_script; }
  void setInitScript(const std::string &str) { init_script = str; }

  size_t getCacheSize() const { return cache_size; }
  void setCacheSize(const size_t i) { cache_size = i; }

  std::string getCacheDir() { return cache_dir; }
  void setCacheDir(const std::string &str) { cache_dir = str; }

  float getCacheHysteresis() const { return cache_hysteresis; }
  void setCacheHysteresis(float f) { cache_hysteresis = f; }

  int getKeepAlive() const { return keep_alive; }
  void setKeepAlive(int i) { keep_alive = i; }

  std::string getThumbSize() { return thumb_size; }
  void setThumbSize(const std::string &str) { thumb_size = str; }

  size_t getCacheNFiles() const { return cache_n_files; }
  void setCacheNFiles(size_t i) { cache_n_files = i; }

  unsigned int getNThreads() const { return n_threads; }
  void setNThreads(const unsigned int i) { n_threads = i; }

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

  std::string getLogfile() { return logfile; }
  void setLogfile(const std::string &str) { logfile = str; }

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
};

}// namespace Sipi


#endif
