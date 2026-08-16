/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * `sipi_init` — the production server-mode engine install behind the FFI seam.
 *
 * The Rust shell calls this once at startup before serving: it parses the Lua
 * config (or, on the TOML path, takes every value from `overrides`), layers the
 * CLI/env overrides on top, builds the cache / decode-memory-budget services,
 * points `engine_context()` at them, and installs the engine-held Lua config VM
 * factory. It is a production entry point, so it lives in the seam package
 * (`src/ffi`), not in the CLI package (`src/cli`).
 */

#include <climits>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "scripting/LuaServer.h"// shttps::LuaServer (parse the Lua config)
#include "scripting/LuaSqlite.h"// shttps::sqliteGlobals
#include "util/Error.h"// shttps::Error

#include "SipiCache.h"
#include "SipiConf.h"// Sipi::SipiConf, Sipi::parseSizeString
#include "SipiIO.h"// Sipi::ScalingMethod, Sipi::ScalingQuality
#include "throttling/SipiMemoryBudget.h"// Sipi::SipiMemoryBudget, MemoryBudgetMode, parse_memory_budget_mode
#include "logging/logger.h"// log_warn / log_err / log_info
#include "observability/metrics.h"// Sipi::observability::Metrics

#include "ffi/SipiLua.h"// Sipi::sipiGlobals
#include "ffi/engine_context.h"// Sipi::ffi::set_engine_context, EngineContext
#include "ffi/lua_config.h"// Sipi::ffi::set_lua_config, LuaConfig, sipiConfGlobals
#include "ffi/sipi_ffi.h"// the extern "C" sipi_init contract + SipiServerConfig
#include "ffi/startup.h"// Sipi::ffi::LibraryInitialiser, detect_available_memory

namespace {

/*! Map a config scaling-quality string to a ScalingMethod; unknown/missing → HIGH. */
Sipi::ScalingMethod parse_scaling_method(const std::string &v)
{
  if (v == "medium") { return Sipi::ScalingMethod::MEDIUM; }
  if (v == "low") { return Sipi::ScalingMethod::LOW; }
  return Sipi::ScalingMethod::HIGH;
}

/*! Convert SipiConf's `map<string,string>` scaling-quality table into the
 *  `ScalingQuality` struct EngineContext holds (jk2 ← the "jpk" key). */
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
 * Process-wide server runtime installed by `sipi_init`.
 *
 * The Rust shell has no server object to own the engine services, so `sipi_init`
 * parks them here for the process lifetime.
 * `engine_context()` stores non-owning pointers into this holder, so it must
 * outlive every serve call — hence file-static. The held `SipiConf` also backs
 * the `sipiConfGlobals` installer captured in the Lua config (the per-request VM
 * factory reads it on every preflight / route call).
 */
struct ServerRuntime
{
  Sipi::SipiConf conf;
  std::unique_ptr<Sipi::SipiCache> cache;
  std::unique_ptr<Sipi::SipiMemoryBudget> memory_budget;
};
std::unique_ptr<ServerRuntime> g_server_runtime;

}// namespace

/*!
 * Parse the Lua config and install the engine + Lua config from scratch. The
 * Rust shell calls it once at startup before serving. Builds the cache / memory
 * budget into `g_server_runtime`, points `engine_context()` at them, and installs
 * the engine-held Lua config VM factory. Returns 0 on success or `EXIT_FAILURE`;
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
    // A null/empty config path selects the Lua-less init path: a TOML config,
    // parsed Rust-side, supplies every value through `overrides`. Otherwise the
    // Lua config is the base the overrides layer onto.
    const bool has_lua_config = (lua_config_path != nullptr && lua_config_path[0] != '\0');

    // Initialise the codec libraries (curl / Exiv2 / TIFF) the decode pipeline
    // needs. The Rust server path does not go through sipi_cli_main's
    // LibraryInitialiser, so sipi_init owns it here; the singleton is idempotent.
    Sipi::ffi::LibraryInitialiser::instance();

    auto runtime = std::make_unique<ServerRuntime>();

    // Parse the Lua config into SipiConf.
    // Skipped on the Lua-less path: `runtime->conf` stays default-constructed (an
    // all-defaults config via SipiConf's in-class initializers) and the overrides
    // below supply imgroot / scriptdir / etc.
    if (has_lua_config) {
      shttps::LuaServer luacfg(lua_config_path);
      runtime->conf = Sipi::SipiConf(luacfg);
    }
    Sipi::SipiConf &conf = runtime->conf;

    // CLI/env overrides: layer the present overrides onto the
    // Lua-parsed SipiConf BEFORE the cache / memory-budget
    // services below are built from `conf`, so an override reaches the engine.
    // Setter names are SipiConf's verbatim (incl. the `setPasswort` typo). Sized
    // strings (cache_size/maxpost/max_decode_memory) carry the raw "300M" text;
    // parseSizeString expands the suffix engine-side. A negative maxpost /
    // max-decode-memory clamps to 0 (matching the SipiConf ctor) so
    // it cannot become SIZE_MAX and skip the budget auto-detect; cache_size keeps
    // -1 as its valid "unlimited" sentinel. cache_nfiles is `unsigned` (0 =
    // unlimited; a negative is rejected by CLI11 on both binaries / by clap u32),
    // so it forwards straight to setCacheNFiles(size_t) with no signed wrap.
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
      if (o.maxpost != nullptr) {
        const long long v = Sipi::parseSizeString(o.maxpost);
        conf.setMaxPostSize(v > 0 ? static_cast<size_t>(v) : 0);// <=0 -> 0 (unlimited)
      }
      if (o.max_decode_memory != nullptr) {
        const long long v = Sipi::parseSizeString(o.max_decode_memory);
        conf.setMaxDecodeMemory(v > 0 ? static_cast<size_t>(v) : 0);// <=0 -> 0 (auto-detect 75% RAM)
      }
      if (o.decode_memory_mode != nullptr) conf.setDecodeMemoryMode(o.decode_memory_mode);
      if (o.thumbsize != nullptr) conf.setThumbSize(o.thumbsize);
      if (o.knorapath != nullptr) conf.setKnoraPath(o.knorapath);
      if (o.knoraport != nullptr) conf.setKnoraPort(o.knoraport);
      if (o.docroot != nullptr) conf.setDocRoot(o.docroot);
      if (o.wwwroute != nullptr) conf.setWWWRoute(o.wwwroute);
      if (o.loglevel != nullptr) conf.setLogLevel(o.loglevel);
      // Scaling-quality per codec (TOML-config-only — no CLI flag). Merge the
      // present codecs onto the base map so a partial override keeps the others.
      // The "j2k" key is stored as the config writes it; to_scaling_quality reads
      // it under "jpk" (a legacy engine quirk), so j2k scaling falls to the
      // default on both the Lua and TOML paths alike — parity, not a new bug.
      if (o.scaling_quality_jpeg != nullptr || o.scaling_quality_tiff != nullptr
          || o.scaling_quality_png != nullptr || o.scaling_quality_j2k != nullptr) {
        std::map<std::string, std::string> sq = conf.getScalingQuality();
        if (o.scaling_quality_jpeg != nullptr) sq["jpeg"] = o.scaling_quality_jpeg;
        if (o.scaling_quality_tiff != nullptr) sq["tiff"] = o.scaling_quality_tiff;
        if (o.scaling_quality_png != nullptr) sq["png"] = o.scaling_quality_png;
        if (o.scaling_quality_j2k != nullptr) sq["j2k"] = o.scaling_quality_j2k;
        conf.setScalingQuality(sq);
      }
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
      if (o.has_max_pixel_limit) conf.setMaxPixelLimit(o.max_pixel_limit);
      if (o.has_jpeg_quality) conf.setJpegQuality(o.jpeg_quality);
    }

    // Engine services built from the config values (with the CLI/env overrides
    // above already applied). A null service means the corresponding feature is
    // disabled.
    {
      const std::string cachedir = conf.getCacheDir();
      const long long cache_size = conf.getCacheSize();
      if (cache_size != 0 && !cachedir.empty()) {
        // Degrade to no-cache on a bad/unwritable cache dir rather than aborting
        // startup — cache init failure is non-fatal, so log the error and continue.
        try {
          runtime->cache = std::make_unique<Sipi::SipiCache>(cachedir, cache_size, conf.getCacheNFiles());
        } catch (const shttps::Error &e) {
          log_warn("sipi_init: caching disabled — %s", e.what());
          runtime->cache = nullptr;
        }
      }
    }
    {
      const Sipi::MemoryBudgetMode mode = Sipi::parse_memory_budget_mode(conf.getDecodeMemoryMode());
      if (mode != Sipi::MemoryBudgetMode::OFF) {
        std::size_t budget = conf.getMaxDecodeMemory();
        if (budget == 0) {
          const std::size_t detected = Sipi::ffi::detect_available_memory();
          budget = (detected > 0) ? detected * 3 / 4 : (1ULL * 1024 * 1024 * 1024);
        }
        runtime->memory_budget = std::make_unique<Sipi::SipiMemoryBudget>(budget, mode);
        Sipi::observability::Metrics::instance().decode_memory_budget_bytes.Set(static_cast<double>(budget));
      }
    }

    // Resolve the image root (realpath) for path-traversal containment (R2).
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
    if (!conf.getInitScript().empty()) {
      std::ifstream initscript_in(conf.getInitScript());
      if (initscript_in.fail()) {
        log_err("sipi_init: initscript \"%s\" not found", conf.getInitScript().c_str());
        return EXIT_FAILURE;
      }
      initscript_src.assign(
        (std::istreambuf_iterator<char>(initscript_in)), std::istreambuf_iterator<char>());
    }
    // else (Lua-less / TOML, no initscript set): leave initscript_src empty —
    // set_lua_config accepts it and the sipiGlobals installers still register the
    // `server` Lua table, so configured route scripts run.

    // Install the engine context — non-owning pointers into g_server_runtime.
    Sipi::ffi::set_engine_context(Sipi::ffi::EngineContext{
      .cache = runtime->cache.get(),
      .memory_budget = runtime->memory_budget.get(),
      .imgroot = imgroot,
      .resolved_imgroot = resolved_imgroot,
      .docroot = conf.getDocRoot(),
      .wwwroute = conf.getWWWRoute(),
      .prefix_as_path = conf.getPrefixAsPath(),
      .jpeg_quality = conf.getJpegQuality(),
      .scaling_quality = to_scaling_quality(conf.getScalingQuality()),
      .max_pixel_limit = conf.getMaxPixelLimit(),
      .nthreads = static_cast<int>(conf.getNThreads()),
      .port = conf.getPort(),
      .max_post_size = conf.getMaxPostSize(),
    });

    // Install the engine-held Lua config (the per-call VM factory behind
    // sipi_preflight / sipi_run_lua_route): the scriptdir, JWT secret, and
    // globals installers (in registration order) the factory applies.
    // The sipiConfGlobals installer captures &conf, which stays valid: `runtime`
    // is heap-allocated, so its SipiConf address is stable across the move into
    // g_server_runtime below.
    Sipi::ffi::set_lua_config(Sipi::ffi::LuaConfig{
      .init_script = std::move(initscript_src),
      .script_dir = conf.getScriptDir(),
      .jwt_secret = conf.getJwtSecret(),
      .globals = {
        { Sipi::ffi::sipiConfGlobals, &conf },
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
