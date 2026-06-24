/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Implements an IIIF server with many features.
 *
 */
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include <fstream>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <jansson.h>
#include <sentry.h>

#include "shttps/lua/LuaServer.h"
#include "shttps/lua_sqlite/LuaSqlite.h"
#include "shttps/util/Parsing.h"
#include "shttps/transport/Server.h"


#include <CLI/CLI.hpp>
#include "logging/logger.h"
#include "cli/commands/convert_access_file.h"
#include "cli/commands/convert_service_file.h"
#include "cli/commands/health.h"
#include "cli/commands/verify.h"
#include "SipiConf.h"
#include "observability/connection_metrics_adapter.h"
#include "observability/sentry_init.h"
#include "SipiFilenameHash.h"
#include "SipiHttpServer.h"
#include "SipiMemoryBudget.h"
#include "observability/metrics.h"
#include "SipiRateLimiter.h"
#include "SipiIO.h"
#include "SipiImage.h"
#include "SipiImageError.h"
#include "ffi/SipiLua.h"
#include "ffi/engine_context.h"
#include "ffi/lua_config.h"
#include "ffi/sipi_ffi.h"
#include "SipiReport.h"
#include "observability/sentry.h"
#include "formats/SipiIOTiff.h"

#include "generated/SipiVersion.h"

#ifdef __linux__
#include <sched.h>
#endif

// A macro for silencing incorrect compiler warnings about unused variables.
#define _unused(x) ((void)(x))

/*!
 * Detect the number of CPU cores available to this process, with container awareness.
 *
 * Detection order (Linux):
 *   1. cgroups v2 (/sys/fs/cgroup/cpu.max) — respects Docker --cpus
 *   2. cgroups v1 (/sys/fs/cgroup/cpu/cpu.cfs_quota_us) — older Docker
 *   3. sched_getaffinity() — respects Docker --cpuset-cpus
 *   4. std::thread::hardware_concurrency() — host CPU count
 *
 * On macOS: falls through directly to hardware_concurrency().
 */
[[nodiscard]] static unsigned int detect_available_cores()
{
#ifdef __linux__
  // 1. cgroups v2: /sys/fs/cgroup/cpu.max contains "quota period" or "max period"
  if (std::ifstream cpu_max("/sys/fs/cgroup/cpu.max"); cpu_max.is_open()) {
    std::string quota_str;
    long period = 0;
    cpu_max >> quota_str >> period;
    if (quota_str != "max" && period > 0) {
      try {
        long quota = std::stol(quota_str);
        if (quota > 0) {
          return std::max(1u, static_cast<unsigned int>((quota + period - 1) / period));
        }
      } catch (...) {
        // Parse failure — fall through
      }
    }
  }

  // 2. cgroups v1: separate quota/period files
  if (std::ifstream cfs_quota("/sys/fs/cgroup/cpu/cpu.cfs_quota_us"); cfs_quota.is_open()) {
    long quota = 0;
    cfs_quota >> quota;
    if (quota > 0) {// -1 means unlimited
      long period = 100000;// default cfs period
      if (std::ifstream cfs_period("/sys/fs/cgroup/cpu/cpu.cfs_period_us"); cfs_period.is_open()) {
        cfs_period >> period;
      }
      return std::max(1u, static_cast<unsigned int>((quota + period - 1) / period));
    }
  }

  // 3. CPU affinity (respects --cpuset-cpus)
  cpu_set_t cpuset;
  if (sched_getaffinity(0, sizeof(cpuset), &cpuset) == 0) {
    int count = CPU_COUNT(&cpuset);
    if (count > 0) return static_cast<unsigned int>(count);
  }
#endif
  // 4. Fallback: hardware concurrency (works on macOS and Linux)
  unsigned int cores = std::thread::hardware_concurrency();
  return (cores > 0) ? cores : 4;
}

/// Detect available memory from container limits or system info.
/// Returns bytes, or 0 if detection fails.
[[nodiscard]] static size_t detect_available_memory()
{
#ifdef __linux__
  // 1. cgroups v2: /sys/fs/cgroup/memory.max
  if (std::ifstream mem_max("/sys/fs/cgroup/memory.max"); mem_max.is_open()) {
    std::string limit_str;
    mem_max >> limit_str;
    if (limit_str != "max") {
      try {
        return static_cast<size_t>(std::stoll(limit_str));
      } catch (...) {
        // Parse failure — fall through
      }
    }
  }

  // 2. cgroups v1: /sys/fs/cgroup/memory/memory.limit_in_bytes
  if (std::ifstream mem_limit("/sys/fs/cgroup/memory/memory.limit_in_bytes"); mem_limit.is_open()) {
    long long limit = 0;
    mem_limit >> limit;
    // 9223372036854771712 = kernel "unlimited" sentinel
    if (limit > 0 && limit < 9223372036854771712LL) {
      return static_cast<size_t>(limit);
    }
  }

  // 3. /proc/meminfo fallback
  if (std::ifstream meminfo("/proc/meminfo"); meminfo.is_open()) {
    std::string line;
    while (std::getline(meminfo, line)) {
      if (line.rfind("MemTotal:", 0) == 0) {
        // Format: "MemTotal:     16384000 kB"
        long long kb = 0;
        std::sscanf(line.c_str(), "MemTotal: %lld kB", &kb);
        if (kb > 0) return static_cast<size_t>(kb) * 1024;
      }
    }
  }
#endif

#ifdef __APPLE__
  // macOS: sysctl hw.memsize
  int64_t memsize = 0;
  size_t len = sizeof(memsize);
  if (sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0) == 0 && memsize > 0) {
    return static_cast<size_t>(memsize);
  }
#endif

  return 0;
}

/*!
 * \mainpage
 *
 * # Sipi – Simple Image Presentation Interface #
 *
 * Sipi is a package that can be used to convert images from/to different formats while
 * preserving as much metadata thats embeded in the file headers a possible. Sipi is also
 * able to do some conversions, especially some common color space transformation using
 * ICC profiles. Currently Sipi supports the following file formats
 *
 * - TIFF
 * - JPEG2000
 * - PNG
 * - JPEG
 *
 * The following metadata "standards" are beeing preserved
 * - EXIF
 * - IPTC
 * - XMP
 *
 * ## Commandline Use ##
 *
 * For simple conversions, Sipi is being used from the command line (in a terminal window). The
 * format is usually
 *
 *     sipi [options] <infile> <outfile>
 *
 */

static void sipiConfGlobals(lua_State *L, shttps::RequestContext &ctx, void *user_data)
{
  auto *conf = (Sipi::SipiConf *)user_data;

  lua_createtable(L, 0, 14);// table1

  lua_pushstring(L, "hostname");// table1 - "index_L1"
  lua_pushstring(L, conf->getHostname().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "port");// table1 - "index_L1"
  lua_pushinteger(L, conf->getPort());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "sslport");// table1 - "index_L1"
  lua_pushinteger(L, conf->getSSLPort());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "imgroot");// table1 - "index_L1"
  lua_pushstring(L, conf->getImgRoot().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "max_temp_file_age");// table1 - "index_L1"
  lua_pushinteger(L, conf->getMaxTempFileAge());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "prefix_as_path");// table1 - "index_L1"
  lua_pushboolean(L, conf->getPrefixAsPath());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "init_script");// table1 - "index_L1"
  lua_pushstring(L, conf->getInitScript().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "cache_dir");// table1 - "index_L1"
  lua_pushstring(L, conf->getCacheDir().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "cache_size");// table1 - "index_L1"
  lua_pushinteger(L, conf->getCacheSize());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "jpeg_quality");// table1 - "index_L1"
  lua_pushinteger(L, conf->getJpegQuality());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "keep_alive");// table1 - "index_L1"
  lua_pushinteger(L, conf->getKeepAlive());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "thumb_size");// table1 - "index_L1"
  lua_pushstring(L, conf->getThumbSize().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "cache_n_files");// table1 - "index_L1"
  lua_pushinteger(L, conf->getCacheNFiles());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "n_threads");// table1 - "index_L1"
  lua_pushinteger(L, conf->getNThreads());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "max_post_size");// table1 - "index_L1"
  lua_pushinteger(L, conf->getMaxPostSize());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "tmpdir");// table1 - "index_L1"
  lua_pushstring(L, conf->getTmpDir().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "ssl_certificate");// table1 - "index_L1"
  lua_pushstring(L, conf->getSSLCertificate().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "ssl_key");// table1 - "index_L1"
  lua_pushstring(L, conf->getSSLKey().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "scriptdir");// table1 - "index_L1"
  lua_pushstring(L, conf->getScriptDir().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "logfile");// table1 - "index_L1"
  lua_pushstring(L, conf->getLogfile().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "loglevel");// table1 - "index_L1"
  const std::string loglevel = conf->getLoglevel();
  if (loglevel == "EMERG") {
    lua_pushinteger(L, LL_EMERG);
  } else if (loglevel == "ALERT") {
    lua_pushinteger(L, LL_ALERT);
  } else if (loglevel == "CRIT") {
    lua_pushinteger(L, LL_CRIT);
  } else if (loglevel == "ERR") {
    lua_pushinteger(L, LL_ERR);
  } else if (loglevel == "WARNING") {
    lua_pushinteger(L, LL_WARNING);
  } else if (loglevel == "NOTICE") {
    lua_pushinteger(L, LL_NOTICE);
  } else if (loglevel == "INFO") {
    lua_pushinteger(L, LL_INFO);
  } else if (loglevel == "DEBUG") {
    lua_pushinteger(L, LL_DEBUG);
  } else {
    lua_pushinteger(L, -1);
  }
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "knora_path");// table1 - "index_L1"
  lua_pushstring(L, conf->getKnoraPath().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "knora_port");// table1 - "index_L1"
  lua_pushstring(L, conf->getKnoraPort().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "adminuser");// table1 - "index_L1"
  lua_pushstring(L, conf->getAdminUser().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "password");// table1 - "index_L1"
  lua_pushstring(L, conf->getPassword().c_str());
  lua_rawset(L, -3);// table1

  // TODO: in the sipi config file, there are different namespaces that are unified here (danger of collision)
  lua_pushstring(L, "docroot");// table1 - "index_L1"
  lua_pushstring(L, conf->getDocRoot().c_str());
  lua_rawset(L, -3);// table1

  lua_setglobal(L, "config");
}

// small function to check if file exist
inline bool exists_file(const std::string &name)
{
  struct stat buffer;
  return (stat(name.c_str(), &buffer) == 0);
}

/*!
 * A singleton that does global initialisation and cleanup of libraries used by Sipi. This class is used only
 * in main().
 *
 * Some libraries such as the ones used for processing XMP or TIFF need some global initialization. These
 * are performed in the constructor of this singleton class
 */
class LibraryInitialiser
{
public:
  /*!
   * @return the singleton instance.
   */
  static LibraryInitialiser &instance()
  {
    // In C++11, initialization of this static local variable happens once and is thread-safe.
    static LibraryInitialiser sipi_init;
    return sipi_init;
  }

private:
  LibraryInitialiser()
  {
    // Initialise libcurl.
    curl_global_init(CURL_GLOBAL_ALL);

    // Initialise Exiv2, registering namespace sipi. Since this is not thread-safe, it must
    // be done here in the main thread.
    if (!Exiv2::XmpParser::initialize(Sipi::xmplock_func, &Sipi::xmp_mutex)) {
      throw shttps::Error("Exiv2::XmpParser::initialize failed");
    }

    // Inititalise the TIFF library.
    Sipi::SipiIOTiff::initLibrary();
  }

  ~LibraryInitialiser()
  {
    // Clean up libcurl.
    curl_global_cleanup();

    // Clean up Exiv2.
    Exiv2::XmpParser::terminate();
  }
};

/*!
 * Handle any unhandled exceptions.
 *
 * This function is called when an unhandled exception is thrown. It sends the exception
 * message to Sentry as a fatal event, then aborts the program. The subsequent SIGABRT
 * is caught by sentry-native's inproc backend, which produces a proper crash event
 * with symbolicated stack traces.
 *
 * NOTE: This intentionally produces TWO Sentry events per unhandled exception:
 * 1. A message event (from here) carrying the exception text
 * 2. A crash event (from inproc SIGABRT handler) with the symbolicated stack trace
 * Sentry groups these separately, which is useful: the message event identifies
 * the exception, while the crash event provides the full native stack trace.
 */
void my_terminate_handler()
{
  std::string msg;
  try {
    // Rethrow the current exception to identify it
    throw;
  } catch (const std::exception &e) {
    msg = "Unhandled exception caught: " + std::string(e.what());
  } catch (...) {
    msg = "Unhandled unknown exception caught";
  }

  sentry_capture_event(sentry_value_new_message_event(SENTRY_LEVEL_FATAL, "my_terminate_handler", msg.c_str()));
  log_err("%s", msg.c_str());
  sentry_flush(2000);

  std::abort();// Triggers SIGABRT → caught by sentry-native inproc backend
}

namespace {
/*! Map a config scaling-quality string to a ScalingMethod; unknown/missing → HIGH
 *  (matching the legacy SipiHttpServer::scaling_quality setter). */
Sipi::ScalingMethod parse_scaling_method(const std::string &v)
{
  if (v == "medium") { return Sipi::ScalingMethod::MEDIUM; }
  if (v == "low") { return Sipi::ScalingMethod::LOW; }
  return Sipi::ScalingMethod::HIGH;
}

/*! Convert SipiConf's `map<string,string>` scaling-quality table into the
 *  `ScalingQuality` struct EngineContext holds — the conversion the legacy
 *  SipiHttpServer::scaling_quality(map) setter performs (jk2 ← the "jpk" key). */
Sipi::ScalingQuality to_scaling_quality(const std::map<std::string, std::string> &m)
{
  const auto get = [&](const char *k) -> std::string {
    const auto it = m.find(k);
    return it != m.end() ? it->second : std::string();
  };
  Sipi::ScalingQuality q;
  q.jk2 = parse_scaling_method(get("jpk"));
  q.jpeg = parse_scaling_method(get("jpeg"));
  q.tiff = parse_scaling_method(get("tiff"));
  q.png = parse_scaling_method(get("png"));
  return q;
}

/*!
 * Process-wide server runtime installed by `sipi_init` (strangler-fig rewrite).
 *
 * The Rust shell, unlike the C++ `SipiHttpServer`, has no server object to own
 * the engine services, so `sipi_init` parks them here for the process lifetime.
 * `engine_context()` stores non-owning pointers into this holder, so it must
 * outlive every serve call — hence file-static. The held `SipiConf` also backs
 * the `sipiConfGlobals` installer captured in the Lua config (the per-request VM
 * factory reads it on every preflight / route call).
 */
struct ServerRuntime
{
  Sipi::SipiConf conf;
  std::unique_ptr<Sipi::SipiCache> cache;
  std::unique_ptr<Sipi::SipiRateLimiter> rate_limiter;
  std::unique_ptr<Sipi::SipiMemoryBudget> memory_budget;
};
std::unique_ptr<ServerRuntime> g_server_runtime;
}// namespace

/*!
 * Parse the Lua config and install the engine + Lua config from scratch
 * (strangler-fig rewrite). This is the from-scratch counterpart to the C++
 * server's parity install (`SipiHttpServer`'s `set_engine_context` +
 * run_server's `set_lua_config`); the Rust shell calls it once at startup
 * before serving. Builds the cache / rate limiter / memory budget into
 * `g_server_runtime`, points `engine_context()` at them, and installs the
 * engine-held Lua config VM factory. Returns 0 on success or `EXIT_FAILURE`;
 * never lets a C++ exception cross the boundary.
 *
 * `overrides` carries the CLI/env flags the Rust shell parsed (or null = none).
 * Present overrides are layered onto the Lua-parsed SipiConf below, before the
 * engine services read it. Only engine-behaviour flags are forwarded; transport
 * flags the Rust shell owns (TLS, keep-alive, concurrency) are not in the struct.
 */
extern "C" int sipi_init(const char *lua_config_path, const SipiServerConfig *overrides)
{
  try {
    if (lua_config_path == nullptr || lua_config_path[0] == '\0') {
      log_err("sipi_init: a Lua config path is required");
      return EXIT_FAILURE;
    }

    // Initialise the codec libraries (curl / Exiv2 / TIFF) the decode pipeline
    // needs. The Rust server path does not go through sipi_cli_main's
    // LibraryInitialiser, so sipi_init owns it here; the singleton is idempotent.
    LibraryInitialiser::instance();

    auto runtime = std::make_unique<ServerRuntime>();

    // Parse the Lua config — the same VM the C++ server path reads via SipiConf.
    {
      shttps::LuaServer luacfg(lua_config_path);
      runtime->conf = Sipi::SipiConf(luacfg);
    }
    Sipi::SipiConf &conf = runtime->conf;

    // CLI/env overrides (plan 02 §7.5 M4): layer the present overrides onto the
    // Lua-parsed SipiConf BEFORE the cache / rate-limiter / memory-budget
    // services below are built from `conf`, so an override reaches the engine.
    // Setter names are SipiConf's verbatim (incl. the `setPasswort` typo). Sized
    // strings (cache_size/maxpost/max_decode_memory) carry the raw "300M" text;
    // parseSizeString does the suffix expansion engine-side. cache_nfiles stays
    // signed and is passed straight through (0 = unlimited, negatives wrap),
    // mirroring run_server's `setCacheNFiles(int)` exactly.
    if (overrides != nullptr) {
      const SipiServerConfig &o = *overrides;
      // Strings (null = absent).
      if (o.imgroot != nullptr) conf.setImgRoot(o.imgroot);
      if (o.scriptdir != nullptr) conf.setScriptDir(o.scriptdir);
      if (o.initscript != nullptr) conf.setInitScript(o.initscript);
      if (o.tmpdir != nullptr) conf.setTmpDir(o.tmpdir);
      if (o.jwtkey != nullptr) conf.setJwtSecret(o.jwtkey);
      if (o.adminuser != nullptr) conf.setAdminUser(o.adminuser);
      if (o.adminpasswd != nullptr) conf.setPasswort(o.adminpasswd);// `setPasswort` is the real (typo'd) setter
      if (o.cache_dir != nullptr) conf.setCacheDir(o.cache_dir);
      if (o.cache_size != nullptr) conf.setCacheSize(Sipi::parseSizeString(o.cache_size));
      if (o.maxpost != nullptr) conf.setMaxPostSize(static_cast<size_t>(Sipi::parseSizeString(o.maxpost)));
      if (o.max_decode_memory != nullptr)
        conf.setMaxDecodeMemory(static_cast<size_t>(Sipi::parseSizeString(o.max_decode_memory)));
      if (o.decode_memory_mode != nullptr) conf.setDecodeMemoryMode(o.decode_memory_mode);
      if (o.rate_limit_mode != nullptr) conf.setRateLimitMode(o.rate_limit_mode);
      if (o.thumbsize != nullptr) conf.setThumbSize(o.thumbsize);
      if (o.knorapath != nullptr) conf.setKnoraPath(o.knorapath);
      if (o.knoraport != nullptr) conf.setKnoraPort(o.knoraport);
      if (o.docroot != nullptr) conf.setDocRoot(o.docroot);
      if (o.wwwroute != nullptr) conf.setWWWRoute(o.wwwroute);
      if (o.loglevel != nullptr) conf.setLogLevel(o.loglevel);
      if (o.subdirexcludes != nullptr && o.subdirexcludes_len > 0) {
        std::vector<std::string> excludes;
        excludes.reserve(o.subdirexcludes_len);
        for (size_t i = 0; i < o.subdirexcludes_len; ++i) excludes.emplace_back(o.subdirexcludes[i]);
        conf.setSubdirExcludes(excludes);
      }
      // Scalars (presence flag — 0 is a valid value, so gate on has_).
      if (o.has_serverport) conf.setPort(o.serverport);
      if (o.has_maxtmpage) conf.setMaxTempFileAge(o.maxtmpage);
      if (o.has_cache_nfiles) conf.setCacheNFiles(o.cache_nfiles);
      if (o.has_subdirlevels) conf.setSubdirLevels(o.subdirlevels);
      if (o.has_pathprefix) conf.setPrefixAsPath(o.pathprefix != 0);
      if (o.has_rate_limit_window) conf.setRateLimitWindow(o.rate_limit_window);
      if (o.has_max_pixel_limit) conf.setMaxPixelLimit(o.max_pixel_limit);
      if (o.has_rate_limit_max_pixels) conf.setRateLimitMaxPixels(o.rate_limit_max_pixels);
      if (o.has_rate_limit_pixel_threshold) conf.setRateLimitPixelThreshold(o.rate_limit_pixel_threshold);
    }

    // Engine services, mirroring run_server()'s construction (config values, with
    // the CLI/env overrides above already applied). A null service means
    // the corresponding feature is disabled, matching the legacy accessors.
    {
      const std::string cachedir = conf.getCacheDir();
      const long long cache_size = conf.getCacheSize();
      if (cache_size != 0 && !cachedir.empty()) {
        // Degrade to no-cache on a bad/unwritable cache dir rather than aborting
        // startup — parity with SipiHttpServer::cache() (which logs + continues).
        try {
          runtime->cache = std::make_unique<Sipi::SipiCache>(cachedir, cache_size, conf.getCacheNFiles());
        } catch (const shttps::Error &e) {
          log_warn("sipi_init: caching disabled — %s", e.what());
          runtime->cache = nullptr;
        }
      }
    }
    {
      const Sipi::RateLimitMode mode = Sipi::parse_rate_limit_mode(conf.getRateLimitMode());
      const std::size_t rl_max = conf.getRateLimitMaxPixels();
      if (mode != Sipi::RateLimitMode::OFF && rl_max > 0) {
        runtime->rate_limiter = std::make_unique<Sipi::SipiRateLimiter>(
          conf.getRateLimitWindow(), rl_max, mode, conf.getRateLimitPixelThreshold());
      }
    }
    {
      const Sipi::MemoryBudgetMode mode = Sipi::parse_memory_budget_mode(conf.getDecodeMemoryMode());
      if (mode != Sipi::MemoryBudgetMode::OFF) {
        std::size_t budget = conf.getMaxDecodeMemory();
        if (budget == 0) {
          const std::size_t detected = detect_available_memory();
          budget = (detected > 0) ? detected * 3 / 4 : (1ULL * 1024 * 1024 * 1024);
        }
        runtime->memory_budget = std::make_unique<Sipi::SipiMemoryBudget>(budget, mode);
        Sipi::observability::Metrics::instance().decode_memory_budget_bytes.Set(static_cast<double>(budget));
      }
    }

    // Resolve the image root (realpath) for path-traversal containment (R2),
    // mirroring SipiHttpServer's resolve at startup.
    const std::string imgroot = conf.getImgRoot();
    char resolved[PATH_MAX];
    if (realpath(imgroot.c_str(), resolved) == nullptr) {
      log_err("sipi_init: image root '%s' does not resolve", imgroot.c_str());
      return EXIT_FAILURE;
    }
    const std::string resolved_imgroot(resolved);

    // Read the init script — the last fallible step — BEFORE any install. The
    // engine context and Lua config install non-owning pointers into `runtime`;
    // installing them first and then failing here would leave the file-static
    // engine pointing into `runtime`, which is freed on this early return.
    std::string initscript_src;
    {
      std::ifstream initscript_in(conf.getInitScript());
      if (initscript_in.fail()) {
        log_err("sipi_init: initscript \"%s\" not found", conf.getInitScript().c_str());
        return EXIT_FAILURE;
      }
      initscript_src.assign(
        (std::istreambuf_iterator<char>(initscript_in)), std::istreambuf_iterator<char>());
    }

    // Install the engine context — non-owning pointers into g_server_runtime.
    Sipi::ffi::set_engine_context(Sipi::ffi::EngineContext{
      .cache = runtime->cache.get(),
      .rate_limiter = runtime->rate_limiter.get(),
      .memory_budget = runtime->memory_budget.get(),
      .imgroot = imgroot,
      .resolved_imgroot = resolved_imgroot,
      .prefix_as_path = conf.getPrefixAsPath(),
      .jpeg_quality = conf.getJpegQuality(),
      .scaling_quality = to_scaling_quality(conf.getScalingQuality()),
      .max_pixel_limit = conf.getMaxPixelLimit(),
      .nthreads = static_cast<int>(conf.getNThreads()),
      .max_post_size = conf.getMaxPostSize(),
    });

    // Install the engine-held Lua config (the per-call VM factory behind
    // sipi_preflight / sipi_run_lua_route). Same scriptdir, JWT secret, and
    // globals installers (in registration order) as run_server's set_lua_config.
    // The sipiConfGlobals installer captures &conf, which stays valid: `runtime`
    // is heap-allocated, so its SipiConf address is stable across the move into
    // g_server_runtime below.
    Sipi::ffi::set_lua_config(Sipi::ffi::LuaConfig{
      .init_script = std::move(initscript_src),
      .script_dir = conf.getScriptDir(),
      .jwt_secret = conf.getJwtSecret(),
      .globals = {
        { sipiConfGlobals, &conf },
        { shttps::sqliteGlobals, nullptr },
        { Sipi::sipiGlobals, nullptr },
      },
      .routes = conf.getRoutes(),
    });

    g_server_runtime = std::move(runtime);
    log_info("sipi_init: engine + Lua config installed (imgroot resolved: %s)", resolved_imgroot.c_str());
    return EXIT_SUCCESS;
  } catch (const shttps::Error &e) {
    log_err("sipi_init failed: %s", e.what());
    return EXIT_FAILURE;
  } catch (const std::exception &e) {
    log_err("sipi_init failed: %s", e.what());
    return EXIT_FAILURE;
  } catch (...) {
    log_err("sipi_init failed: unknown error");
    return EXIT_FAILURE;
  }
}

/*!
 * The CLI entry point, behind the FFI seam (`ffi/sipi_ffi.h`).
 *
 * Owns the CLI11 app and every subcommand (`server`, `convert`, `verify`,
 * `query`, `compare`, `health`). Lives in `//src/cli:cli_app` so the Rust
 * shell can link it and call `sipi_cli_main` without colliding with
 * the binary's own `main`. Returns the exit code rather than calling `exit()`
 * — the caller (the C++ `main`, or the Rust shell) owns process teardown.
 *
 * @param argc the number of command line arguments.
 * @param argv the command line arguments.
 * @return the exit code.
 */
extern "C" int sipi_cli_main(int argc, char **argv)
{
  // Fast path: print version and exit before any library initialisation.
  // Doing this after curl/exiv2/TIFF init leaks their static registries
  // at exit and trips the LSan gate in the sanitizer e2e suite.
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--version") {
      std::cout << "sipi " << VERSION << std::endl;
      return 0;
    }
  }

  // Set top-level exception handler. Unhandled C++ exceptions are sent to Sentry
  // as message events, then std::abort() is called. The resulting SIGABRT is caught
  // by sentry-native's inproc backend, which produces a proper crash event with
  // symbolicated stack traces.
  // Note: SIGSEGV/SIGABRT signal handlers are installed automatically by sentry_init()
  // when using the inproc backend — no manual signal() calls needed.
  std::set_terminate(my_terminate_handler);

  // Read SIPI_SENTRY_* env vars and initialize sentry-native.
  Sipi::observability::SentryConfig sentry_cfg;
  if (const char *p = getenv("SIPI_SENTRY_DSN"); p != nullptr) { sentry_cfg.dsn = p; }
  if (const char *p = getenv("SIPI_SENTRY_RELEASE"); p != nullptr) { sentry_cfg.release = p; }
  if (const char *p = getenv("SIPI_SENTRY_ENVIRONMENT"); p != nullptr) { sentry_cfg.environment = p; }
  Sipi::observability::init_sentry(sentry_cfg);


  //
  // first we initialize the libraries that sipi uses
  //
  try {
    LibraryInitialiser &sipi_init = LibraryInitialiser::instance();
    _unused(sipi_init);// Silence compiler warning about unused variable.
  } catch (shttps::Error &e) {
    log_err("Library initialization failed: %s", e.to_string().c_str());
    return EXIT_FAILURE;
  }

  // Shut down sentry's background transport thread (joined/drained by
  // sentry_close()) before static destructors run — in particular before
  // ~LibraryInitialiser calls curl_global_cleanup(). Registered AFTER the
  // static LibraryInitialiser has finished construction, so per
  // [basic.start.term] this handler runs BEFORE ~LibraryInitialiser at exit.
  // This covers every exit path: subcommand dispatch returning through
  // sipi_cli_main, a CLI11 parse error returning the parse-error code, and
  // normal return. Without it, curl teardown races the live sentry-http thread
  // and corrupts the heap.
  // (std::abort() from my_terminate_handler bypasses atexit, leaving
  // sentry-native's inproc crash reporting intact.)
  std::atexit([] { Sipi::observability::close_sentry(); });

  CLI::App sipiopt("SIPI is an IIIF image server and image format converter.");
  sipiopt.require_subcommand(1);

  // Exit code recorded by the matched subcommand callback, returned after
  // dispatch. Replaces the legacy `exit(run_X())` so no exit()/abort() crosses
  // the FFI boundary when the Rust shell calls sipi_cli_main. require_subcommand(1)
  // guarantees exactly one leaf callback assigns it (the `convert`/`verify`
  // parent callbacks self-skip when a nested subcommand matched).
  int sipi_exit_code = EXIT_SUCCESS;

  //
  // Option storage variables.
  //
  // Storage declarations live at the top of main so the helper lambdas
  // (attach_*_opts) and the body lambdas (run_query, run_compare,
  // run_convert, run_server) can all capture them by reference. Each
  // option's CLI11 registration happens on the appropriate subcommand
  // below.
  //
  std::string optConfigfile;
  std::string optInFile;
  std::string optOutFile;
  std::vector<std::string> optCompare;

  enum class OptFormat : int { jpx, jpg, tif, png };
  OptFormat optFormat = OptFormat::jpx;
  const std::vector<std::pair<std::string, OptFormat>> optFormatMap{ { "jpx", OptFormat::jpx },
    { "jp2", OptFormat::jpx },
    { "jpg", OptFormat::jpg },
    { "tif", OptFormat::tif },
    { "png", OptFormat::png } };

  enum class OptIcc : int { none, sRGB, AdobeRGB, GRAY };
  OptIcc optIcc = OptIcc::none;
  const std::vector<std::pair<std::string, OptIcc>> optIccMap{
    { "none", OptIcc::none }, { "sRGB", OptIcc::sRGB }, { "AdobeRGB", OptIcc::AdobeRGB }, { "GRAY", OptIcc::GRAY }
  };

  enum class OptMirror { none, horizontal, vertical };
  OptMirror optMirror = OptMirror::none;
  const std::vector<std::pair<std::string, OptMirror>> optMirrorMap{
    { "none", OptMirror::none }, { "horizontal", OptMirror::horizontal }, { "vertical", OptMirror::vertical }
  };

  int optJpegQuality = 60;
  std::vector<int> optRegion;
  int optReduce = 0;
  std::string optSize;
  int optScale = 0;
  bool optSkipMeta = false;
  float optRotate = 0.0;
  bool optSetTopleft = false;
  std::string optWatermark;
  bool optJsonOutput = false;
  int optPagenum = 0;

  // JPEG2000 / pyramidal-TIFF tuning knobs (only valid on `convert`).
  std::string j2k_Sprofile;
  std::vector<std::string> j2k_rates;
  int j2k_Clayers = 0;
  int j2k_Clevels = 0;
  std::string j2k_Corder;
  std::string j2k_Stiles;
  std::string j2k_Cprecincts;
  std::string j2k_Cblk;
  bool j2k_Cuse_sop = false;
  bool tiff_Pyramid = false;

  // Server options (only valid on the `server` subcommand).
  int optServerport = 80;
  int optSSLport = 443;
  std::string optHostname = "localhost";
  int optKeepAlive = 5;
  unsigned int optNThreads = 0;// 0 = auto-detect
  size_t optMaxWaiting = 0;    // 0 = unlimited (timeout-only)
  unsigned int optQueueTimeout = 10;
  std::string optMaxPostSize = "300M";
  std::string optImgroot = "./images";
  std::string optDocroot = "./server";
  std::string optWWWRoute = "/server";
  std::string optScriptDir = "./scripts";
  std::string optTmpdir = "./tmp";
  int optMaxTmpAge = 86400;
  bool optPathprefix = false;
  int optSubdirLevels = 0;
  std::vector<std::string> optSubdirExcludes = { "tmp", "thumb" };
  std::string optInitscript = "./config/sipi.init.lua";
  std::string optCachedir = "./cache";
  std::string optCacheSize = "200M";
  int optCacheNFiles = 200;
  double optCacheHysteresisIgnored = 0.0;
  size_t optMaxPixelLimit = 0;
  size_t optRateLimitMaxPixels = 0;
  unsigned optRateLimitWindow = 600;
  std::string optRateLimitMode = "off";
  size_t optRateLimitPixelThreshold = 2000000;
  std::string optMaxDecodeMemory = "0";
  std::string optDecodeMemoryMode = "off";
  unsigned optDrainTimeout = 30;
  std::string optThumbSize = "!128,128";
  std::string optSSLCertificatePath = "./certificate/certificate.pem";
  std::string optSSLKeyPath = "./certificate/key.pem";
  std::string optJWTKey = "UP 4888, nice 4-8-4 steam engine";
  std::string optAdminUser = "admin";
  std::string optAdminPassword = "Sipi-Admin";
  std::string optKnoraPath = "localhost";
  std::string optKnoraPort = "3434";
  std::string optLogfilePath = "Sipi";
  LogLevel optLogLevel = LL_DEBUG;
  const std::vector<std::pair<std::string, LogLevel>> logLevelMap{ { "DEBUG", LL_DEBUG },
    { "INFO", LL_INFO }, { "NOTICE", LL_NOTICE }, { "WARNING", LL_WARNING },
    { "ERR", LL_ERR }, { "CRIT", LL_CRIT }, { "ALERT", LL_ALERT }, { "EMERG", LL_EMERG } };

  // Sentry env-driven settings remain on `sipiopt` (top-level) so they
  // apply uniformly across every subcommand. CLI11 reads them from the
  // SIPI_SENTRY_* environment variables; no command-line override path
  // is needed.
  std::string optSipiSentryDsn;
  sipiopt.add_option("--sentry-dsn", optSipiSentryDsn)->envname("SIPI_SENTRY_DSN");
  std::string optSipiSentryRelease;
  sipiopt.add_option("--sentry-release", optSipiSentryRelease)->envname("SIPI_SENTRY_RELEASE");
  std::string optSipiSentryEnvironment;
  sipiopt.add_option("--sentry-environment", optSipiSentryEnvironment)->envname("SIPI_SENTRY_ENVIRONMENT");

  //
  // Body lambdas, each capturing the option storage.
  //
  // Each CLI mode's body is captured in a named lambda over the option
  // storage. The subcommand callbacks below invoke them and record the result
  // into `sipi_exit_code`.
  //
  auto run_query = [&]() -> int {
    set_cli_mode(true);
    Sipi::SipiImage img;
    img.read(optInFile);
    std::cout << img << std::endl;
    return 0;
  };

  //
  // Convert body. The `src` parameter points at the invoking subcommand
  // (cmd_convert today; a future access-file command will call this
  // body too). `user_set` queries
  // the subcommand's own option group to detect whether each flag was
  // explicitly set by the operator.
  //
  auto run_convert = [&](CLI::App *src) -> int {
    auto user_set = [&](const std::string &name) -> bool {
      auto *s = src->get_option_no_throw(name);
      return s != nullptr && !s->empty();
    };

    set_cli_mode(true);
    // Under --json, route all log output (info, warn, err) to stderr so stdout
    // stays reserved for the single JSON document emitted at the end of the
    // CLI run.
    if (optJsonOutput) { set_json_mode(true); }

    //
    // get the output format
    //
    std::string format("jpg");
    if (user_set("--format")) {
      switch (optFormat) {
      case OptFormat::jpx: format = "jpx"; break;
      case OptFormat::jpg: format = "jpg"; break;
      case OptFormat::tif: format = "tif"; break;
      case OptFormat::png: format = "png"; break;
      }
    } else {
      //
      // there is no format option given – we try to determine the format
      // from the output name extension
      //
      size_t pos = optOutFile.rfind('.');
      if (pos != std::string::npos) {
        std::string ext = optOutFile.substr(pos + 1);
        if ((ext == "jpx") || (ext == "jp2")) {
          format = "jpx";
        } else if ((ext == "tif") || (ext == "tiff")) {
          format = "tif";
        } else if ((ext == "jpg") || (ext == "jpeg")) {
          format = "jpg";
        } else if (ext == "png") {
          format = "png";
        } else {
          const std::string msg = "Not a supported filename extension: '" + ext + "'";
          log_err("%s", msg.c_str());
          if (optJsonOutput) { Sipi::emit_json_cli_arg_error(std::cout, msg); }
          return EXIT_FAILURE;
        }
      }
    }

    //
    // getting information about a region of interest
    //
    std::shared_ptr<Sipi::SipiRegion> region = nullptr;
    if (user_set("--region")) {
      region = std::make_shared<Sipi::SipiRegion>(optRegion.at(0), optRegion.at(1), optRegion.at(2), optRegion.at(3));
    }

    //
    // get the reduce parameter
    //
    std::shared_ptr<Sipi::SipiSize> size = nullptr;
    if (optReduce > 0) {
      size = std::make_shared<Sipi::SipiSize>(optReduce);
    } else if (user_set("--size")) {
      try {
        size = std::make_shared<Sipi::SipiSize>(optSize);
      } catch (std::exception &e) {
        const std::string msg = std::string{ "Error in size parameter: " } + e.what();
        log_err("%s", msg.c_str());
        if (optJsonOutput) { Sipi::emit_json_cli_arg_error(std::cout, msg); }
        return EXIT_FAILURE;
      }
    } else if (user_set("--scale")) {
      try {
        size = std::make_shared<Sipi::SipiSize>(optScale);
      } catch (std::exception &e) {
        const std::string msg = std::string{ "Error in scale parameter: " } + e.what();
        log_err("%s", msg.c_str());
        if (optJsonOutput) { Sipi::emit_json_cli_arg_error(std::cout, msg); }
        return EXIT_FAILURE;
      }
    }

    //
    // Prepare Sentry context for error reporting
    //
    Sipi::observability::ImageContext sentry_ctx;
    sentry_ctx.input_file = optInFile;
    sentry_ctx.output_file = optOutFile;
    sentry_ctx.output_format = format;
    sentry_ctx.file_size_bytes = Sipi::observability::get_file_size(optInFile);

    //
    // read the input image
    //
    Sipi::SipiImage img;
    try {
      img.readSource(optInFile, region, size);
      if (format == "jpg") {
        img.to8bps();
        img.convertToIcc(Sipi::Icc(Sipi::PredefinedProfiles::icc_sRGB), 8);
      }
    } catch (const Sipi::SipiImageError &err) {
      Sipi::observability::populate_from_image(sentry_ctx, img);
      Sipi::observability::capture_image_error(err.what(), "read", sentry_ctx);
      log_err("Error reading image: %s", err.what());
      if (optJsonOutput) { Sipi::emit_json_report(std::cout, sentry_ctx, err.what(), std::string{ "read" }); }
      return EXIT_FAILURE;
    } catch (const std::exception &err) {
      Sipi::observability::populate_from_image(sentry_ctx, img);
      Sipi::observability::capture_image_error(err.what(), "read", sentry_ctx);
      log_err("Error reading image: %s", err.what());
      if (optJsonOutput) { Sipi::emit_json_report(std::cout, sentry_ctx, err.what(), std::string{ "read" }); }
      return EXIT_FAILURE;
    }

    //
    // image processing: orientation, metadata, ICC, rotation, watermark
    //
    try {
      if (user_set("--topleft")) {
        Sipi::Orientation orientation = img.getOrientation();
        std::shared_ptr<Sipi::Exif> exif = img.getExif();
        if (exif != nullptr) {
          unsigned short ori;
          if (exif->getValByKey("Exif.Image.Orientation", ori)) { orientation = static_cast<Sipi::Orientation>(ori); }
        }
        switch (orientation) {
        case Sipi::TOPLEFT: break;
        case Sipi::TOPRIGHT: img.rotate(0., true); break;
        case Sipi::BOTRIGHT: img.rotate(180., false); break;
        case Sipi::BOTLEFT: img.rotate(180., true); break;
        case Sipi::LEFTTOP: img.rotate(270., true); break;
        case Sipi::RIGHTTOP: img.rotate(90., false); break;
        case Sipi::RIGHTBOT: img.rotate(90., true); break;
        case Sipi::LEFTBOT: img.rotate(270., false); break;
        default:;
        }
        exif->addKeyVal("Exif.Image.Orientation", static_cast<unsigned short>(Sipi::TOPLEFT));
        img.setOrientation(Sipi::TOPLEFT);
      }

      if (user_set("--skipmeta")) { img.setSkipMetadata(Sipi::SkipMetadata::SKIP_ALL); }

      if (user_set("--icc")) {
        Sipi::Icc icc;
        switch (optIcc) {
        case OptIcc::sRGB: icc = Sipi::Icc(Sipi::PredefinedProfiles::icc_sRGB); break;
        case OptIcc::AdobeRGB: icc = Sipi::Icc(Sipi::PredefinedProfiles::icc_AdobeRGB); break;
        case OptIcc::GRAY: icc = Sipi::Icc(Sipi::PredefinedProfiles::icc_GRAY_D50); break;
        case OptIcc::none: break;
        }
        img.convertToIcc(icc, img.getBps());
      }

      if (user_set("--mirror") || user_set("--rotate")) {
        switch (optMirror) {
        case OptMirror::vertical: img.rotate(optRotate + 180.0F, true); break;
        case OptMirror::horizontal: img.rotate(optRotate, true); break;
        case OptMirror::none:
          if (optRotate != 0.0F) { img.rotate(optRotate, false); }
          break;
        }
      }

      if (user_set("--watermark")) { img.add_watermark(optWatermark); }
    } catch (const Sipi::SipiImageError &err) {
      Sipi::observability::populate_from_image(sentry_ctx, img);
      Sipi::observability::capture_image_error(err.what(), "convert", sentry_ctx);
      log_err("Error processing image: %s", err.what());
      if (optJsonOutput) { Sipi::emit_json_report(std::cout, sentry_ctx, err.what(), std::string{ "convert" }); }
      return EXIT_FAILURE;
    } catch (const std::exception &err) {
      Sipi::observability::populate_from_image(sentry_ctx, img);
      Sipi::observability::capture_image_error(err.what(), "convert", sentry_ctx);
      log_err("Error processing image: %s", err.what());
      if (optJsonOutput) { Sipi::emit_json_report(std::cout, sentry_ctx, err.what(), std::string{ "convert" }); }
      return EXIT_FAILURE;
    }

    //
    // write the output file
    //
    Sipi::SipiCompressionParams comp_params;
    // SipiCompressionParams maps to std::string; optJpegQuality is an int, so it
    // MUST be stringified (like the J2K int params below). Assigning the int
    // directly bound to std::string::operator=(char), storing the byte 0x50
    // ('P') and making the JPEG writer's stoi() throw.
    if (user_set("--quality")) comp_params[Sipi::JPEG_QUALITY] = std::to_string(optJpegQuality);
    if (user_set("--Sprofile")) comp_params[Sipi::J2K_Sprofile] = j2k_Sprofile;
    if (user_set("--Clayers")) comp_params[Sipi::J2K_Clayers] = std::to_string(j2k_Clayers);
    if (user_set("--Clevels")) comp_params[Sipi::J2K_Clevels] = std::to_string(j2k_Clevels);
    if (user_set("--Corder")) comp_params[Sipi::J2K_Corder] = j2k_Corder;
    if (user_set("--Cprecincts")) comp_params[Sipi::J2K_Cprecincts] = j2k_Cprecincts;
    if (user_set("--Cblk")) comp_params[Sipi::J2K_Cblk] = j2k_Cblk;
    if (user_set("--Cuse_sop")) comp_params[Sipi::J2K_Cuse_sop] = j2k_Cuse_sop ? "yes" : "no";
    if (user_set("--Stiles")) comp_params[Sipi::J2K_Stiles] = j2k_Stiles;
    if (user_set("--Ctiff_pyramid")) comp_params[Sipi::TIFF_Pyramid] = tiff_Pyramid ? "yes" : "no";

    if (user_set("--rates")) {
      std::stringstream ss;
      for (auto &rate : j2k_rates) {
        if (rate == "X") {
          ss << "-1.0 ";
        } else {
          ss << rate << " ";
        }
      }
      comp_params[Sipi::J2K_rates] = ss.str();
    }

    try {
      img.write(format, optOutFile, &comp_params);
    } catch (const Sipi::SipiImageError &err) {
      Sipi::observability::populate_from_image(sentry_ctx, img);
      Sipi::observability::capture_image_error(err.what(), "write", sentry_ctx);
      log_err("Error writing image: %s", err.what());
      if (optJsonOutput) { Sipi::emit_json_report(std::cout, sentry_ctx, err.what(), std::string{ "write" }); }
      return EXIT_FAILURE;
    } catch (const std::exception &err) {
      Sipi::observability::populate_from_image(sentry_ctx, img);
      Sipi::observability::capture_image_error(err.what(), "write", sentry_ctx);
      log_err("Error writing image: %s", err.what());
      if (optJsonOutput) { Sipi::emit_json_report(std::cout, sentry_ctx, err.what(), std::string{ "write" }); }
      return EXIT_FAILURE;
    }

    // Successful CLI completion — emit the structured JSON report if --json was set.
    if (optJsonOutput) {
      Sipi::observability::populate_from_image(sentry_ctx, img);
      Sipi::emit_json_report(std::cout, sentry_ctx);
    }
    return EXIT_SUCCESS;
  };

  auto run_compare = [&]() -> int {
    set_cli_mode(true);
    if (!exists_file(optCompare[0])) {
      log_err("File not found: %s", optCompare[0].c_str());
      return EXIT_FAILURE;
    }
    if (!exists_file(optCompare[1])) {
      log_err("File not found: %s", optCompare[1].c_str());
      return EXIT_FAILURE;
    }

    Sipi::SipiImage img1, img2;
    img1.read(optCompare[0]);
    img2.read(optCompare[1]);

    if (img1 == img2) {
      log_info("Files identical!");
      return 0;
    }

    // Capture the per-channel delta from the original pixels before the
    // `img1 -= img2` visualization step below rewrites img1 into the
    // normalized diff (which would otherwise corrupt the reported avg/max).
    const std::optional<Sipi::PixelDelta> delta = img1.maxPixelDelta(img2);

    if (!delta.has_value()) {
      // Differing channel count / bit depth / photometric interpretation:
      // no meaningful per-channel delta, and `img1 -= img2` would throw.
      log_info("Files differ: dimensions or format not comparable.");
      return -1;
    }

    img1 -= img2;
    img1.write("tif", "diff.tif");
    log_info("Files differ: avg: %f max: %d (%zu, %zu) See diff.tif",
      delta->mean_abs,
      delta->max_abs,
      delta->max_x,
      delta->max_y);

    return -1;
  };

  //
  // Server body. Invoked from `cmd_server`'s callback. The `src`
  // parameter points at cmd_server so `user_set` can check the per-flag
  // "was explicitly set" state on the subcommand's option table.
  //
  auto run_server = [&](CLI::App *src) -> int {
    auto user_set = [&](const std::string &name) -> bool {
      auto *s = src->get_option_no_throw(name);
      return s != nullptr && !s->empty();
    };

    set_cli_mode(false);

    try {
      Sipi::SipiConf sipiConf;
      bool config_loaded = false;
      if (user_set("--config")) {
        // read and parse the config file (config file is a lua script)
        shttps::LuaServer luacfg(optConfigfile);
        sipiConf = Sipi::SipiConf(luacfg);
        config_loaded = true;
      }

      //
      // now we check for all commandline/environment variables and update sipiConf.
      //
      if (!config_loaded) {
        sipiConf.setPort(optServerport);
      } else {
        if (user_set("--serverport")) sipiConf.setPort(optServerport);
      }
      if (!config_loaded) {
        sipiConf.setSSLPort(optSSLport);
      } else {
        if (user_set("--sslport")) sipiConf.setSSLPort(optSSLport);
      }
      if (!config_loaded) {
        sipiConf.setHostname(optHostname);
      } else {
        if (user_set("--hostname")) sipiConf.setHostname(optHostname);
      }
      if (!config_loaded) {
        sipiConf.setKeepAlive(optKeepAlive);
      } else {
        if (user_set("--keepalive")) sipiConf.setKeepAlive(optKeepAlive);
      }
      if (!config_loaded) {
        sipiConf.setNThreads(optNThreads);
        sipiConf.setMaxWaitingConnections(optMaxWaiting);
        sipiConf.setQueueTimeout(optQueueTimeout);
      } else {
        if (user_set("--nthreads")) sipiConf.setNThreads(optNThreads);
        if (user_set("--max-waiting")) sipiConf.setMaxWaitingConnections(optMaxWaiting);
        if (user_set("--queue-timeout")) sipiConf.setQueueTimeout(optQueueTimeout);
      }

      size_t l = optMaxPostSize.length();
      char c = l > 0 ? optMaxPostSize[l - 1] : '\0';
      tsize_t maxpost_size;
      if (c == 'M') {
        maxpost_size = stoll(optMaxPostSize.substr(0, l - 1)) * 1024 * 1024;
      } else if (c == 'G') {
        maxpost_size = stoll(optMaxPostSize.substr(0, l - 1)) * 1024 * 1024 * 1024;
      } else {
        maxpost_size = stoll(optMaxPostSize);
      }
      if (!config_loaded) {
        sipiConf.setMaxPostSize(maxpost_size);
      } else {
        if (user_set("--maxpost")) sipiConf.setMaxPostSize(maxpost_size);
      }

      if (!config_loaded) {
        sipiConf.setImgRoot(optImgroot);
      } else {
        if (user_set("--imgroot")) sipiConf.setImgRoot(optImgroot);
      }
      if (!config_loaded) {
        sipiConf.setDocRoot(optDocroot);
      } else {
        if (user_set("--docroot")) sipiConf.setDocRoot(optDocroot);
      }
      if (!config_loaded) {
        sipiConf.setWWWRoute(optWWWRoute);
      } else {
        if (user_set("--wwwroute")) sipiConf.setWWWRoute(optWWWRoute);
      }
      if (!config_loaded) {
        sipiConf.setScriptDir(optScriptDir);
      } else {
        if (user_set("--scriptdir")) sipiConf.setScriptDir(optScriptDir);
      }
      if (!config_loaded) {
        sipiConf.setTmpDir(optTmpdir);
      } else {
        if (user_set("--tmpdir")) sipiConf.setTmpDir(optTmpdir);
      }
      if (!config_loaded) {
        sipiConf.setMaxTempFileAge(optMaxTmpAge);
      } else {
        if (user_set("--maxtmpage")) sipiConf.setMaxTempFileAge(optMaxTmpAge);
      }
      if (!config_loaded) {
        sipiConf.setPrefixAsPath(optPathprefix);
      } else {
        if (user_set("--pathprefix")) sipiConf.setPrefixAsPath(optPathprefix);
      }
      if (!config_loaded) {
        sipiConf.setSubdirLevels(optSubdirLevels);
      } else {
        if (user_set("--subdirlevels")) sipiConf.setSubdirLevels(optSubdirLevels);
      }
      if (!config_loaded) {
        sipiConf.setSubdirExcludes(optSubdirExcludes);
      } else {
        if (user_set("--subdirexcludes")) sipiConf.setSubdirExcludes(optSubdirExcludes);
      }
      if (!config_loaded) {
        sipiConf.setInitScript(optInitscript);
      } else {
        if (user_set("--initscript")) sipiConf.setInitScript(optInitscript);
      }

      // Cache dir — CLI overrides Lua config
      bool cachedir_from_cli = user_set("--cache-dir") || user_set("--cachedir");
      if (!config_loaded || cachedir_from_cli) { sipiConf.setCacheDir(optCachedir); }

      // Cache size — CLI overrides Lua config
      bool cachesize_from_cli = user_set("--cache-size") || user_set("--cachesize");
      if (!config_loaded || cachesize_from_cli) {
        long long cli_cache_size = Sipi::parseSizeString(optCacheSize);
        if (cli_cache_size < -1) {
          log_err("Invalid --cache-size value '%s'. Use '-1' (unlimited), '0' (disabled), or a positive value like '200M'.", optCacheSize.c_str());
          return EXIT_FAILURE;
        }
        sipiConf.setCacheSize(cli_cache_size);
      }

      // Cache nfiles — CLI overrides Lua config
      bool cachenfiles_from_cli = user_set("--cache-nfiles") || user_set("--cachenfiles");
      if (!config_loaded || cachenfiles_from_cli) { sipiConf.setCacheNFiles(optCacheNFiles); }

      // Warn if deprecated --cachehysteresis was used
      if (user_set("--cachehysteresis")) {
        log_warn("--cachehysteresis is no longer supported (replaced by built-in 80%% low-water mark). Ignoring.");
      }

      if (!config_loaded) {
        sipiConf.setThumbSize(optThumbSize);
      } else {
        if (user_set("--thumbsize")) sipiConf.setThumbSize(optThumbSize);
      }
      if (!config_loaded) {
        sipiConf.setSSLCertificate(optSSLCertificatePath);
      } else {
        if (user_set("--sslcert")) sipiConf.setSSLCertificate(optSSLCertificatePath);
      }
      if (!config_loaded) {
        sipiConf.setSSLKey(optSSLKeyPath);
      } else {
        if (user_set("--sslkey")) sipiConf.setSSLKey(optSSLKeyPath);
      }
      if (!config_loaded) {
        sipiConf.setJwtSecret(optJWTKey);
      } else {
        if (user_set("--jwtkey")) sipiConf.setJwtSecret(optJWTKey);
      }
      if (!config_loaded) {
        sipiConf.setAdminUser(optAdminUser);
      } else {
        if (user_set("--adminuser")) sipiConf.setAdminUser(optAdminUser);
      }
      if (!config_loaded) {
        sipiConf.setPasswort(optAdminPassword);
      } else {
        if (user_set("--adminpasswd")) sipiConf.setPasswort(optAdminPassword);
      }
      if (!config_loaded) {
        sipiConf.setKnoraPath(optKnoraPath);
      } else {
        if (user_set("--knorapath")) sipiConf.setKnoraPath(optKnoraPath);
      }
      if (!config_loaded) {
        sipiConf.setKnoraPort(optKnoraPort);
      } else {
        if (user_set("--knoraport")) sipiConf.setKnoraPort(optKnoraPort);
      }
      if (!config_loaded) {
        sipiConf.setLogfile(optLogfilePath);
      } else {
        if (user_set("--logfile")) sipiConf.setLogfile(optLogfilePath);
      }

      std::string loglevelstring;
      switch (optLogLevel) {
      case LL_DEBUG: loglevelstring = "DEBUG"; break;
      case LL_INFO: loglevelstring = "INFO"; break;
      case LL_NOTICE: loglevelstring = "NOTICE"; break;
      case LL_WARNING: loglevelstring = "WARNING"; break;
      case LL_ERR: loglevelstring = "ERR"; break;
      case LL_CRIT: loglevelstring = "CRIT"; break;
      case LL_ALERT: loglevelstring = "ALERT"; break;
      case LL_EMERG: loglevelstring = "EMERG"; break;
      }
      if (!config_loaded) {
        sipiConf.setLogLevel(loglevelstring);
      } else {
        if (user_set("--loglevel")) sipiConf.setLogLevel(loglevelstring);
      }

      // Apply the resolved log level to the Logger
      {
        const std::string &resolved = sipiConf.getLoglevel();
        LogLevel resolvedLevel = LL_INFO;
        if (resolved == "DEBUG") resolvedLevel = LL_DEBUG;
        else if (resolved == "INFO") resolvedLevel = LL_INFO;
        else if (resolved == "NOTICE") resolvedLevel = LL_NOTICE;
        else if (resolved == "WARNING") resolvedLevel = LL_WARNING;
        else if (resolved == "ERR") resolvedLevel = LL_ERR;
        else if (resolved == "CRIT") resolvedLevel = LL_CRIT;
        else if (resolved == "ALERT") resolvedLevel = LL_ALERT;
        else if (resolved == "EMERG") resolvedLevel = LL_EMERG;
        set_log_level(resolvedLevel);
      }

      //
      // here we check the levels... and migrate if necessary
      //
      if (sipiConf.getPrefixAsPath()) {
        std::vector<std::string> dirs_to_exclude = sipiConf.getSubdirExcludes();
        DIR *dirp = opendir(sipiConf.getImgRoot().c_str());
        if (dirp == nullptr) {
          throw shttps::Error(std::string("Couldn't read directory content! Path: ") + sipiConf.getImgRoot(), errno);
        }
        struct dirent *dp;
        while ((dp = readdir(dirp)) != nullptr) {
          if (dp->d_type == DT_DIR) {
            if (strcmp(dp->d_name, ".") == 0) continue;
            if (strcmp(dp->d_name, "..") == 0) continue;
            bool exclude = false;
            for (auto direx : dirs_to_exclude) {
              if (direx == dp->d_name) exclude = true;
            }
            if (exclude) continue;
            std::string path = sipiConf.getImgRoot() + "/" + dp->d_name;
            int levels = SipiFilenameHash::check_levels(path);
            int new_levels = sipiConf.getSubdirLevels();
            if (levels != new_levels) {
              log_info("Subdir migration of %s...", path.c_str());
              SipiFilenameHash::migrateToLevels(path, new_levels);
            }
          }
        }
        closedir(dirp);
      } else {
        int levels = SipiFilenameHash::check_levels(sipiConf.getImgRoot());
        int new_levels = sipiConf.getSubdirLevels();
        if (levels != new_levels) {
          log_info("Subdir migration of %s...", sipiConf.getImgRoot().c_str());
          SipiFilenameHash::migrateToLevels(sipiConf.getImgRoot(), new_levels);
        }
      }
      SipiFilenameHash::setLevels(sipiConf.getSubdirLevels());

      if (!optSipiSentryDsn.empty()) log_info("SIPI_SENTRY_DSN: %s", optSipiSentryDsn.c_str());
      if (!optSipiSentryEnvironment.empty()) log_info("SIPI_SENTRY_ENVRONMENT: %s", optSipiSentryEnvironment.c_str());
      if (!optSipiSentryRelease.empty()) log_info("SIPI_SENTRY_Release: %s", optSipiSentryRelease.c_str());

      // Create object SipiHttpServer
      auto nthreads = sipiConf.getNThreads();
      if (nthreads < 1) {
        auto cores = detect_available_cores();
        nthreads = std::max(2u, cores > 1 ? cores - 1 : cores);
        log_info("Auto-detected %u CPU cores, starting %u worker threads", cores, nthreads);
      }
      Sipi::SipiHttpServer server(
        sipiConf.getPort(), nthreads, sipiConf.getUseridStr(), sipiConf.getLogfile(), sipiConf.getLoglevel());

      // Install the SIPI-side telemetry adapter on the shttps server before
      // any other configuration runs, so early-startup events (queue depth,
      // rejections during warm-up) reach Metrics. shttps owns the
      // ConnectionMetrics strategy interface; SIPI plugs in the adapter that
      // bridges to its singleton — this is the seam that keeps shttps free
      // of any reverse dependency on Sipi:: types (see CONTEXT-MAP.md).
      server.setMetrics(std::make_shared<Sipi::observability::ConnectionMetricsAdapter>());

      log_info("BUILD_TIMESTAMP: %s", BUILD_TIMESTAMP);
      log_info("BUILD_SCM_TAG: %s", BUILD_SCM_TAG);
      log_info("BUILD_SCM_REVISION: %s", BUILD_SCM_REVISION);

      server.ssl_port(sipiConf.getSSLPort());
      std::string tmps = sipiConf.getSSLCertificate();
      server.ssl_certificate(tmps);
      tmps = sipiConf.getSSLKey();
      server.ssl_key(tmps);
      server.jwt_secret(sipiConf.getJwtSecret());

      server.tmpdir(sipiConf.getTmpDir());
      server.max_post_size(sipiConf.getMaxPostSize());
      server.scriptdir(sipiConf.getScriptDir());
      server.luaRoutes(sipiConf.getRoutes());
      server.add_lua_globals_func(sipiConfGlobals, &sipiConf);
      server.add_lua_globals_func(shttps::sqliteGlobals);
      server.add_lua_globals_func(Sipi::sipiGlobals);
      server.prefix_as_path(sipiConf.getPrefixAsPath());
      server.dirs_to_exclude(sipiConf.getSubdirExcludes());
      server.scaling_quality(sipiConf.getScalingQuality());
      server.jpeg_quality(sipiConf.getJpegQuality());

      // max pixel limit — CLI/env overrides config file
      {
        size_t pixel_limit = sipiConf.getMaxPixelLimit();
        if (user_set("--max-pixel-limit")) pixel_limit = optMaxPixelLimit;
        server.max_pixel_limit(pixel_limit);
        if (pixel_limit > 0) { log_info("Max output pixel limit: %zu", pixel_limit); }
      }

      // Rate limiter — CLI/env overrides config file
      {
        size_t rl_max = sipiConf.getRateLimitMaxPixels();
        if (user_set("--rate-limit-max-pixels")) rl_max = optRateLimitMaxPixels;
        unsigned rl_window = sipiConf.getRateLimitWindow();
        if (user_set("--rate-limit-window")) rl_window = optRateLimitWindow;
        std::string rl_mode_str = sipiConf.getRateLimitMode();
        if (user_set("--rate-limit-mode")) rl_mode_str = optRateLimitMode;
        size_t rl_threshold = sipiConf.getRateLimitPixelThreshold();
        if (user_set("--rate-limit-pixel-threshold")) rl_threshold = optRateLimitPixelThreshold;

        auto rl_mode = Sipi::parse_rate_limit_mode(rl_mode_str);
        if (rl_mode != Sipi::RateLimitMode::OFF && rl_max > 0) {
          server.rate_limiter(std::make_unique<Sipi::SipiRateLimiter>(rl_window, rl_max, rl_mode, rl_threshold));
          log_info("Rate limiting enabled: mode=%s, %zu pixels per %u seconds, threshold=%zu",
            rl_mode_str.c_str(), rl_max, rl_window, rl_threshold);
        } else {
          log_info("Rate limiting disabled");
        }
      }

      // Memory budget — CLI/env overrides config file
      {
        size_t budget_bytes = sipiConf.getMaxDecodeMemory();
        if (user_set("--max-decode-memory")) {
          long long parsed = Sipi::parseSizeString(optMaxDecodeMemory);
          budget_bytes = (parsed > 0) ? static_cast<size_t>(parsed) : 0;
        }
        std::string mb_mode_str = sipiConf.getDecodeMemoryMode();
        if (user_set("--decode-memory-mode")) mb_mode_str = optDecodeMemoryMode;

        auto mb_mode = Sipi::parse_memory_budget_mode(mb_mode_str);
        if (mb_mode != Sipi::MemoryBudgetMode::OFF) {
          if (budget_bytes == 0) {
            size_t detected = detect_available_memory();
            if (detected > 0) {
              budget_bytes = detected * 3 / 4;
              log_info("Auto-detected memory: %zu bytes, budget set to 75%%: %zu bytes", detected, budget_bytes);
            } else {
              log_warn("Could not detect available memory, using 1 GB default budget");
              budget_bytes = 1ULL * 1024 * 1024 * 1024;
            }
          }
          server.memory_budget(std::make_unique<Sipi::SipiMemoryBudget>(budget_bytes, mb_mode));
          Sipi::observability::Metrics::instance().decode_memory_budget_bytes.Set(static_cast<double>(budget_bytes));
          log_info("Memory budget enabled: mode=%s, budget=%zu bytes", mb_mode_str.c_str(), budget_bytes);
        } else {
          log_info("Memory budget disabled");
        }
      }

      // Graceful shutdown drain timeout
      {
        unsigned dt = sipiConf.getDrainTimeout();
        if (user_set("--drain-timeout")) dt = optDrainTimeout;
        server.drain_timeout(dt);
        log_info("Drain timeout: %u seconds", dt);
      }

      //
      // cache parameter...
      //
      std::string cachedir = sipiConf.getCacheDir();
      long long conf_cache_size = sipiConf.getCacheSize();

      if (conf_cache_size == 0) {
        log_info("Caching is disabled (cache_size = 0)");
      } else if (!cachedir.empty()) {
        if (conf_cache_size == -1) { log_info("Cache size is unlimited"); }
        size_t nfiles = sipiConf.getCacheNFiles();
        server.cache(cachedir, conf_cache_size, nfiles);
      }

      server.imgroot(sipiConf.getImgRoot());
      server.initscript(sipiConf.getInitScript());
      server.keep_alive_timeout(sipiConf.getKeepAlive());
      server.max_waiting_connections(sipiConf.getMaxWaitingConnections());
      server.queue_timeout(sipiConf.getQueueTimeout());
      server.docroot(sipiConf.getDocRoot());
      server.wwwroute(sipiConf.getWWWRoute());

      // Install the engine-held Lua config the connection-less FFI entries
      // (sipi_preflight / sipi_run_lua_route) build their per-call VM from. It
      // mirrors the per-request VM shttps::Server builds: same init-script
      // source, scriptdir, JWT secret, and globals installers in registration
      // order. This is the parity install; sipi_init takes it over at the
      // cutover (this block and the server both go away then).
      {
        std::ifstream initscript_in(sipiConf.getInitScript());
        if (initscript_in.fail()) {
          throw shttps::Error("initscript \"" + sipiConf.getInitScript() + "\" not found!");
        }
        std::string initscript_src(
          (std::istreambuf_iterator<char>(initscript_in)), std::istreambuf_iterator<char>());
        Sipi::ffi::set_lua_config(Sipi::ffi::LuaConfig{
          .init_script = std::move(initscript_src),
          .script_dir = sipiConf.getScriptDir(),
          .jwt_secret = sipiConf.getJwtSecret(),
          .globals = {
            { sipiConfGlobals, &sipiConf },
            { shttps::sqliteGlobals, nullptr },
            { Sipi::sipiGlobals, nullptr },
          },
        });
      }

      // start the server
      log_info("SIPI server starting on port %d...", sipiConf.getPort());
      server.run();
    } catch (shttps::Error &err) {
      log_err("Error starting server: %s", err.what());
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  };

  //
  // Subcommand surface.
  //
  // Two tiers per ADR-0010:
  //   - generic verbs: `convert <in> <out>`, `verify <file>`, `query`,
  //     `compare` (anyone-use, ImageMagick-style).
  //   - pipeline-stage verbs under `convert` / `verify`: `access-file`,
  //     `service-file`, `preservation-file` (DSP preservation-chain
  //     semantics).
  //
  // `sipiopt.require_subcommand(1)` (set near the top of main) gates
  // every invocation through one of these subcommands; a bare `sipi`
  // exits with a usage error. The `preservation-file` callbacks under
  // `convert` and `verify` remain stubs pending ADR-0012.
  //
  auto stub_preservation_file = [](const std::string &name) {
    log_err("`sipi %s preservation-file` awaits ADR-0012; not yet implemented.", name.c_str());
    return EXIT_FAILURE;
  };

  // ----- Option-group helpers (D5 matrix) ------------------------------
  // Each helper attaches a logical group of options to the given
  // subcommand. CLI11 rejects options at parse time on subcommands the
  // group isn't attached to — the option-availability matrix is
  // enforced by which subcommands each helper is invoked on, below.
  // All groups bind to the legacy storage variables so the
  // if-else body chain keeps reading them transparently.
  auto attach_generic_transform_opts = [&](CLI::App *cmd) {
    cmd->add_option("-r,--region", optRegion, "Select region of interest, where x y w h are integer values.")
      ->expected(4);
    cmd->add_option("-R,--reduce", optReduce, "Reduce image size by factor (cannot be used together with --size and --scale).");
    cmd->add_option("-s,--size", optSize, "Resize image to given size (cannot be used together with --reduce and --scale).");
    cmd->add_option("-S,--scale", optScale,
      "Resize image by the given percentage Value (cannot be used together with --size and --reduce).");
    cmd->add_option("-o,--rotate", optRotate, "Rotate the image by degree Value, angle between (0.0 - 360.0).");
    cmd->add_option("-m,--mirror", optMirror, "Mirror the image: 'none', 'horizontal', 'vertical'.")
      ->transform(CLI::CheckedTransformer(optMirrorMap, CLI::ignore_case));
    cmd->add_option("-w,--watermark", optWatermark, "Add a watermark to the image.");
    cmd->add_option("-q,--quality", optJpegQuality, "Quality (compression).")->check(CLI::Range(1, 100));
    cmd->add_option("-F,--format", optFormat, "Output format.")
      ->transform(CLI::CheckedTransformer(optFormatMap, CLI::ignore_case));
  };
  auto attach_color_space_opts = [&](CLI::App *cmd) {
    cmd->add_option("-I,--icc", optIcc, "Convert to ICC profile.")
      ->transform(CLI::CheckedTransformer(optIccMap, CLI::ignore_case));
  };
  auto attach_normalize_opts = [&](CLI::App *cmd) {
    cmd->add_flag("--topleft", optSetTopleft, "Enforce orientation TOPLEFT.");
  };
  auto attach_strip_opts = [&](CLI::App *cmd) {
    cmd->add_flag("-k,--skipmeta", optSkipMeta, "Skip metadata of original file if flag is present.");
  };
  auto attach_output_opts = [&](CLI::App *cmd) {
    cmd->add_flag("--json", optJsonOutput,
      "Emit a structured JSON report (success or error) to stdout instead of human-readable messages.");
  };

  // ----- Format-specific options ---------------------------------------
  // J2K + pyramidal-TIFF tuning knobs from `kdu_compress`. Attached only
  // to `convert` since that is the verb that can produce JP2 or pyramidal
  // TIFF outputs. `convert service-file` deliberately omits these — that
  // command bakes in good baseline defaults, not operator-controlled.
  auto attach_j2k_opts = [&](CLI::App *cmd) {
    cmd->add_option("--Sprofile", j2k_Sprofile,
      "Restricted profile to which the code-stream conforms [Default: PART2].")
      ->check(CLI::IsMember({ "PROFILE0", "PROFILE1", "PROFILE2", "PART2", "CINEMA2K", "CINEMA4K",
                               "BROADCAST", "CINEMA2S", "CINEMA4S", "CINEMASS", "IMF" },
        CLI::ignore_case));
    cmd->add_option("--rates", j2k_rates,
      "One or more bit-rates (see kdu_compress help!). A value \"-1\" may be used in place of the "
      "first bit-rate in the list to indicate that the final quality layer should include all "
      "compressed bits.");
    cmd->add_option("--Clayers", j2k_Clayers, "J2K: Number of quality layers [Default: 8].");
    cmd->add_option("--Clevels", j2k_Clevels,
      "J2K: Number of wavelet decomposition levels, or stages [default: 8].");
    cmd->add_option("--Corder", j2k_Corder,
      "J2K: Progression order: LRCP, RLCP, RPCL (default), PCRL, CPRL.")
      ->check(CLI::IsMember({ "LRCP", "RLCP", "RPCL", "PCRL", "CPRL" }, CLI::ignore_case));
    cmd->add_option("--Stiles", j2k_Stiles, "J2K: Tiles dimensions \"{tx,ty}\" [Default: {256,256}].");
    cmd->add_option("--Cprecincts", j2k_Cprecincts,
      "J2K: Precinct dimensions \"{px,py}\" (powers of 2) [Default: {256,256}].");
    cmd->add_option("--Cblk", j2k_Cblk,
      "J2K: Nominal code-block dimensions (powers of 2, 4..1024, product <= 4096) [Default: {64,64}].");
    cmd->add_option("--Cuse_sop", j2k_Cuse_sop,
      "J2K Cuse_sop: Include SOP markers (resync markers) [Default: yes].");
    cmd->add_option("--Ctiff_pyramid", tiff_Pyramid,
      "TIFF: store in Pyramidal TIFF format [Default: no].");
  };

  // ----- Server options -------------------------------------------------
  // All ~40 server options live on the `server` subcommand. Sentry env
  // settings remain on `sipiopt` (top-level) so they apply to every
  // subcommand invocation.
  auto attach_server_opts = [&](CLI::App *cmd) {
    cmd->add_option("-c,--config", optConfigfile, "Configuration file for web server.")
      ->envname("SIPI_CONFIGFILE")
      ->check(CLI::ExistingFile);
    cmd->add_option("--serverport", optServerport, "Port of SIPI web server.")
      ->envname("SIPI_SERVERPORT");
    cmd->add_option("--sslport", optSSLport, "SSL-port of the SIPI server.")->envname("SIPI_SSLPORT");
    cmd->add_option("--hostname", optHostname, "Hostname to use for HTTP server.")
      ->envname("SIPI_HOSTNAME");
    cmd->add_option("--keepalive", optKeepAlive, "Number of seconds for the keep-alive option of HTTP 1.1.")
      ->envname("SIPI_KEEPALIVE");
    cmd->add_option("-t,--nthreads", optNThreads,
        "Number of threads for SIPI server (0 = auto-detect from CPU cores).")
      ->envname("SIPI_NTHREADS");
    cmd->add_option("--max-waiting", optMaxWaiting,
        "Max waiting connections before 503 (0 = unlimited, timeout-only).")
      ->envname("SIPI_MAX_WAITING");
    cmd->add_option("--queue-timeout", optQueueTimeout,
        "Max seconds a request waits in queue before 503.")
      ->envname("SIPI_QUEUE_TIMEOUT");
    cmd->add_option("--maxpost", optMaxPostSize,
        "Maximal size of a POST request, e.g. '300M'.")
      ->envname("SIPI_MAXPOSTSIZE");
    cmd->add_option("--imgroot", optImgroot,
        "Root directory containing the images for the web server.")
      ->envname("SIPI_IMGROOT")
      ->check(CLI::ExistingDirectory);
    cmd->add_option("--docroot", optDocroot, "Path to document root for normal webserver.")
      ->envname("SIPI_DOCROOT")
      ->check(CLI::ExistingDirectory);
    cmd->add_option("--wwwroute", optWWWRoute, "URL route for standard webserver.")
      ->envname("SIPI_WWWROUTE");
    cmd->add_option("--scriptdir", optScriptDir,
        "Path to directory containing Lua scripts to implement routes.")
      ->envname("SIPI_SCRIPTDIR")
      ->check(CLI::ExistingDirectory);
    cmd->add_option("--tmpdir", optTmpdir,
        "Path to the temporary directory (e.g. for uploads etc.).")
      ->envname("SIPI_TMPDIR")
      ->check(CLI::ExistingDirectory);
    cmd->add_option("--maxtmpage", optMaxTmpAge,
        "The maximum allowed age of temporary files (in seconds) before they are deleted.")
      ->envname("SIPI_MAXTMPAGE");
    cmd->add_flag("--pathprefix", optPathprefix,
        "Flag: IIIF prefix is part of the path to the image file (deprecated).")
      ->envname("SIPI_PATHPREFIX");
    cmd->add_option("--subdirlevels", optSubdirLevels, "Number of subdir levels (deprecated).")
      ->envname("SIPI_SUBDIRLEVELS");
    cmd->add_option("--subdirexcludes", optSubdirExcludes,
        "Directories not included in subdir calculations.")
      ->envname("SIPI_SUBDIREXCLUDES");
    cmd->add_option("--initscript", optInitscript, "Path to init script (Lua).")
      ->envname("SIPI_INITSCRIPT")
      ->check(CLI::ExistingFile);
    cmd->add_option("--cache-dir", optCachedir, "Path to cache folder.")->envname("SIPI_CACHE_DIR");
    cmd->add_option("--cachedir", optCachedir, "DEPRECATED: use --cache-dir.")->envname("SIPI_CACHEDIR");
    cmd->add_option("--cache-size", optCacheSize,
        "Cache size: '-1' (unlimited), '0' (disabled), or e.g. '200M'.")
      ->envname("SIPI_CACHE_SIZE");
    cmd->add_option("--cachesize", optCacheSize, "DEPRECATED: use --cache-size.")
      ->envname("SIPI_CACHESIZE");
    cmd->add_option("--cache-nfiles", optCacheNFiles, "Max number of files to cache (0=no limit).")
      ->envname("SIPI_CACHE_NFILES");
    cmd->add_option("--cachenfiles", optCacheNFiles, "DEPRECATED: use --cache-nfiles.")
      ->envname("SIPI_CACHENFILES");
    cmd->add_option("--cachehysteresis", optCacheHysteresisIgnored,
        "DEPRECATED: no longer supported (replaced by built-in 80% low-water mark).")
      ->envname("SIPI_CACHEHYSTERESIS");
    cmd->add_option("--max-pixel-limit", optMaxPixelLimit,
        "Max output pixels (width*height) per IIIF request. 0 = unlimited.")
      ->envname("SIPI_MAX_PIXEL_LIMIT");
    cmd->add_option("--rate-limit-max-pixels", optRateLimitMaxPixels,
        "Max output pixels per client per window. 0 = disabled.")
      ->envname("SIPI_RATE_LIMIT_MAX_PIXELS");
    cmd->add_option("--rate-limit-window", optRateLimitWindow,
        "Rate limit sliding window in seconds (default 600).")
      ->envname("SIPI_RATE_LIMIT_WINDOW");
    cmd->add_option("--rate-limit-mode", optRateLimitMode,
        "Rate limit mode: off, monitor, enforce (default off).")
      ->envname("SIPI_RATE_LIMIT_MODE");
    cmd->add_option("--rate-limit-pixel-threshold", optRateLimitPixelThreshold,
        "Requests below this pixel count are free (default 2000000).")
      ->envname("SIPI_RATE_LIMIT_PIXEL_THRESHOLD");
    cmd->add_option("--max-decode-memory", optMaxDecodeMemory,
        "Max concurrent decode memory budget (e.g., 2G, 500M). 0 = auto (75% of detected memory).")
      ->envname("SIPI_MAX_DECODE_MEMORY");
    cmd->add_option("--decode-memory-mode", optDecodeMemoryMode,
        "Decode memory mode: off, monitor, enforce (default off).")
      ->envname("SIPI_DECODE_MEMORY_MODE");
    cmd->add_option("--drain-timeout", optDrainTimeout,
        "Seconds to wait for in-flight requests during graceful shutdown (default 30).")
      ->envname("SIPI_DRAIN_TIMEOUT");
    cmd->add_option("--thumbsize", optThumbSize, "Size of the thumbnails (to be used within Lua).")
      ->envname("SIPI_THUMBSIZE");
    cmd->add_option("--sslcert", optSSLCertificatePath, "Path to SSL certificate.")
      ->envname("SIPI_SSLCERTIFICATE");
    cmd->add_option("--sslkey", optSSLKeyPath, "Path to the SSL key file.")->envname("SIPI_SSLKEY");
    cmd->add_option("--jwtkey", optJWTKey,
        "Secret for generating JWTs (exactly 42 characters).")
      ->envname("SIPI_JWTKEY");
    cmd->add_option("--adminuser", optAdminUser, "Username for SIPI admin user.")
      ->envname("SIPI_ADMIINUSER");
    cmd->add_option("--adminpasswd", optAdminPassword, "Password of the admin user.")
      ->envname("SIPI_ADMINPASSWD");
    cmd->add_option("--knorapath", optKnoraPath, "Path to Knora server.")->envname("SIPI_KNORAPATH");
    cmd->add_option("--knoraport", optKnoraPort, "Portnumber for Knora.")->envname("SIPI_KNORAPORT");
    cmd->add_option("--logfile", optLogfilePath, "Name of the logfile (NYI).")->envname("SIPI_LOGFILE");
    cmd->add_option("--loglevel", optLogLevel,
        "Logging level: 'DEBUG', 'INFO', 'WARNING', 'ERR', 'CRIT', 'ALERT', 'EMERG'.")
      ->transform(CLI::CheckedTransformer(logLevelMap, CLI::ignore_case))
      ->envname("SIPI_LOGLEVEL");
  };

  // ----- server ----------------------------------------------------------
  CLI::App *cmd_server = sipiopt.add_subcommand("server", "Run sipi as a high-performance IIIF server.");
  attach_server_opts(cmd_server);
  cmd_server->callback([&]() { sipi_exit_code = run_server(cmd_server); });

  // ----- convert (generic, ImageMagick-style) ----------------------------
  CLI::App *cmd_convert =
    sipiopt.add_subcommand("convert", "Generic format conversion (Access File output, no Essentials).");
  cmd_convert->add_option("input", optInFile, "Input file to be converted.")->check(CLI::ExistingFile);
  cmd_convert->add_option("output", optOutFile, "Output file.");
  attach_generic_transform_opts(cmd_convert);
  attach_color_space_opts(cmd_convert);
  attach_normalize_opts(cmd_convert);
  attach_strip_opts(cmd_convert);
  attach_output_opts(cmd_convert);
  attach_j2k_opts(cmd_convert);
  cmd_convert->add_option("-n,--pagenum", optPagenum, "Page number for PDF documents or multipage TIFFs.");
  cmd_convert->callback([&]() {
    // Bare `convert <in> <out>` only fires if no nested subcommand matched.
    if (cmd_convert->get_subcommands().empty()) { sipi_exit_code = run_convert(cmd_convert); }
  });

  // ----- convert access-file (DSP-opinionated; Access File output) ------
  CLI::App *cmd_convert_access = cmd_convert->add_subcommand(
    "access-file", "Produce an Access File from a Service File input (validates input has Essentials).");
  cmd_convert_access->add_option("input", optInFile, "Input Service File.")->check(CLI::ExistingFile);
  cmd_convert_access->add_option("output", optOutFile, "Output Access File.");
  attach_generic_transform_opts(cmd_convert_access);
  attach_color_space_opts(cmd_convert_access);
  attach_normalize_opts(cmd_convert_access);
  attach_output_opts(cmd_convert_access);
  cmd_convert_access->callback([&]() {
    auto user_set = [&](const std::string &name) -> bool {
      auto *s = cmd_convert_access->get_option_no_throw(name);
      return s != nullptr && !s->empty();
    };

    Sipi::cli::ConvertAccessFileArgs req;
    req.input_path = optInFile;
    req.output_path = optOutFile;
    if (user_set("--format")) {
      switch (optFormat) {
      case OptFormat::jpx: req.format = "jpx"; break;
      case OptFormat::jpg: req.format = "jpg"; break;
      case OptFormat::tif: req.format = "tif"; break;
      case OptFormat::png: req.format = "png"; break;
      }
    }
    if (user_set("--region")) { req.region = optRegion; }
    if (user_set("--size")) { req.size = optSize; }
    if (user_set("--scale")) { req.scale = optScale; }
    if (optReduce > 0) { req.reduce = optReduce; }
    if (user_set("--rotate")) { req.rotate = optRotate; }
    if (user_set("--mirror")) {
      switch (optMirror) {
      case OptMirror::horizontal: req.mirror = "horizontal"; break;
      case OptMirror::vertical: req.mirror = "vertical"; break;
      case OptMirror::none: break;
      }
    }
    if (user_set("--watermark")) { req.watermark = optWatermark; }
    if (user_set("--quality")) { req.jpeg_quality = optJpegQuality; }
    if (user_set("--icc")) {
      switch (optIcc) {
      case OptIcc::sRGB: req.icc = "sRGB"; break;
      case OptIcc::AdobeRGB: req.icc = "AdobeRGB"; break;
      case OptIcc::GRAY: req.icc = "GRAY"; break;
      case OptIcc::none: break;
      }
    }
    req.set_topleft = optSetTopleft;
    req.json_output = optJsonOutput;
    sipi_exit_code = Sipi::cli::cmd_convert_access_file(req);
  });

  // ----- convert service-file (Service File creation) -------------------------
  CLI::App *cmd_convert_service = cmd_convert->add_subcommand(
    "service-file", "Create a Service File (writes Essentials packet); restricted option set.");
  cmd_convert_service->add_option("input", optInFile, "Input source file.")->check(CLI::ExistingFile);
  cmd_convert_service->add_option("output", optOutFile, "Output Service File.");
  attach_normalize_opts(cmd_convert_service);
  cmd_convert_service->callback([&]() {
    Sipi::cli::ConvertServiceFileArgs req;
    req.input_path = optInFile;
    req.output_path = optOutFile;
    req.set_topleft = optSetTopleft;
    sipi_exit_code = Sipi::cli::cmd_convert_service_file(req);
  });

  // ----- convert preservation-file (stub) -------------------------------
  CLI::App *cmd_convert_preservation = cmd_convert->add_subcommand(
    "preservation-file", "(stub) Awaits ADR-0012; not yet implemented.");
  cmd_convert_preservation->callback([&]() { sipi_exit_code = stub_preservation_file("convert"); });

  // ----- verify (generic decoder check) --------------------------------
  auto run_verify_with_mode = [&](Sipi::cli::VerifyMode mode) {
    Sipi::cli::VerifyArgs req;
    req.mode = mode;
    req.input_path = optInFile;
    req.json_output = optJsonOutput;
    return Sipi::cli::cmd_verify(req);
  };
  CLI::App *cmd_verify = sipiopt.add_subcommand("verify", "Generic decoder-coverage check (no stage assertions).");
  cmd_verify->add_option("file", optInFile, "File to verify.")->check(CLI::ExistingFile);
  attach_output_opts(cmd_verify);
  cmd_verify->callback([&]() {
    // Bare `verify <file>` only fires if no nested subcommand matched.
    if (cmd_verify->get_subcommands().empty()) {
      sipi_exit_code = run_verify_with_mode(Sipi::cli::VerifyMode::Generic);
    }
  });

  // ----- verify access-file / service-file / preservation-file ---------
  CLI::App *cmd_verify_access = cmd_verify->add_subcommand(
    "access-file", "Assert file is a valid Access File (no Essentials; well-formed XMP).");
  cmd_verify_access->add_option("file", optInFile, "Access File to verify.")->check(CLI::ExistingFile);
  attach_output_opts(cmd_verify_access);
  cmd_verify_access->callback([&]() { sipi_exit_code = run_verify_with_mode(Sipi::cli::VerifyMode::AccessFile); });

  CLI::App *cmd_verify_service = cmd_verify->add_subcommand(
    "service-file", "Assert Essentials parses, hash matches, shape consistent.");
  cmd_verify_service->add_option("file", optInFile, "Service File to verify.")->check(CLI::ExistingFile);
  attach_output_opts(cmd_verify_service);
  cmd_verify_service->callback([&]() { sipi_exit_code = run_verify_with_mode(Sipi::cli::VerifyMode::ServiceFile); });

  CLI::App *cmd_verify_preservation = cmd_verify->add_subcommand(
    "preservation-file", "(stub) Awaits ADR-0012; not yet implemented.");
  cmd_verify_preservation->callback([&]() { sipi_exit_code = stub_preservation_file("verify"); });

  // ----- query -----------------------------------------------------------
  CLI::App *cmd_query = sipiopt.add_subcommand("query", "Dump image information.");
  cmd_query->add_option("file", optInFile, "File to query.")->check(CLI::ExistingFile);
  attach_output_opts(cmd_query);
  cmd_query->callback([&]() { sipi_exit_code = run_query(); });

  // ----- compare ---------------------------------------------------------
  CLI::App *cmd_compare = sipiopt.add_subcommand("compare", "Byte/pixel comparison of two files.");
  cmd_compare->add_option("files", optCompare, "Two files to compare.")->expected(2);
  attach_output_opts(cmd_compare);
  cmd_compare->callback([&]() { sipi_exit_code = run_compare(); });

  // ----- health ----------------------------------------------------------
  // Self-contained liveness probe for container/orchestrator healthchecks:
  // GET http://127.0.0.1:<port>/health, exit 0 if healthy, 1 otherwise. The
  // caller passes the port the server was configured with (`--port`); a
  // separate process can't discover it from config/env/flags.
  int optHealthPort = 1024;
  CLI::App *cmd_health = sipiopt.add_subcommand(
    "health", "Probe the local /health endpoint; exit 0 if healthy, 1 otherwise.");
  cmd_health->add_option("--port", optHealthPort, "Port the sipi server listens on.")
    ->check(CLI::Range(1, 65535));
  cmd_health->callback([&]() { sipi_exit_code = Sipi::cli::cmd_health({ optHealthPort }); });

  // Catch-all around dispatch: a subcommand body (e.g. query/compare's
  // img.read()/write()) can throw SipiImageError, which is NOT a CLI::Error, so
  // CLI11_PARSE would let it unwind out of this `extern "C"` entry into the Rust
  // caller — UB across the FFI (sipi_ffi.h's no-exception contract). Map any
  // escaped exception to EXIT_FAILURE here, mirroring the engine's sipi_guard.
  // (CLI11_PARSE's own try/catch still handles CLI::ParseError + returns its code.)
  try {
    CLI11_PARSE(sipiopt, argc, argv);
  } catch (const std::exception &e) {
    log_err("sipi: unhandled exception: %s", e.what());
    return EXIT_FAILURE;
  } catch (...) {
    log_err("sipi: unhandled non-standard exception");
    return EXIT_FAILURE;
  }

  // `require_subcommand(1)` means exactly one leaf subcommand callback fired
  // and recorded its result into sipi_exit_code. Sentry teardown runs via the
  // std::atexit(close_sentry) registered above on process exit; the caller
  // (the C++ main, or the Rust shell) owns when that happens.
  return sipi_exit_code;
}
