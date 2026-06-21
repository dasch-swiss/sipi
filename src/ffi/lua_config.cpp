/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/lua_config.h"

#include <utility>

namespace Sipi::ffi {
namespace {
  // Process-wide engine Lua config. Set once at startup (Phase B-L: by the
  // server; Phase C: by sipi_init) and only read thereafter, so the per-call VM
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

}// namespace Sipi::ffi
