/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/lua_config.h"

#include <string>
#include <utility>

#include "SipiConf.h"// Sipi::SipiConf
#include "logging/logger.h"// LogLevel (LL_EMERG … LL_DEBUG)

namespace Sipi::ffi {
namespace {
  // Process-wide engine Lua config. Installed once at startup — in production by
  // `sipi_init` (`ffi/init.cpp`), and in the oracle transport by the still-living
  // C++ server before the cutover — and only read thereafter, so the per-call VM
  // factory needs no synchronization on the config itself. (Each call still gets
  // its own LuaServer — the Lua VM is never shared across threads.)
  LuaConfig g_lua_config;
}// namespace

void set_lua_config(LuaConfig cfg) { g_lua_config = std::move(cfg); }

const LuaConfig &lua_config() { return g_lua_config; }

std::unique_ptr<shttps::LuaServer> make_lua_server(shttps::RequestContext &ctx)
{
  const LuaConfig &cfg = g_lua_config;
  ctx.jwt_secret = cfg.jwt_secret;

  // Mirror Server::process_request: package.path base, then the init script
  // (createGlobals installs server.* and the script defines the preflight
  // hooks), then the remaining globals in registration order.
  const std::string lua_scriptdir = cfg.script_dir + "/?.lua";
  auto vm = std::make_unique<shttps::LuaServer>(ctx, cfg.init_script, /*iscode=*/true, lua_scriptdir);
  for (const auto &g : cfg.globals) { g.func(vm->lua(), ctx, g.user_data); }
  return vm;
}

void sipiConfGlobals(lua_State *L, shttps::RequestContext & /*ctx*/, void *user_data)
{
  auto *conf = static_cast<Sipi::SipiConf *>(user_data);

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

}// namespace Sipi::ffi
