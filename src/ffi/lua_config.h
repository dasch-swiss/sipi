/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * The engine-owned Lua configuration the connection-less FFI entry points read.
 *
 * `sipi_preflight` / `sipi_run_lua_route` are C ABIs that carry no LuaServer and
 * no config — they need a fully-configured Lua VM but have no socket and no
 * `shttps::Server` to build one from. `LuaConfig` is that durable Lua state: the
 * init-script source (which defines `pre_flight` / `file_pre_flight`), the Lua
 * package path, the JWT secret, and the `config.*` / `db` / `sipi.*` globals
 * installers — the engine-held parallel to `shttps::Server`'s `_initscript` /
 * `_scriptdir` / `jwt_secret` + `lua_globals`.
 *
 * `make_lua_server` rebuilds, connection-less, exactly the per-request VM
 * `Server::process_request` constructs: the init script runs (installing
 * `server.*` and defining the preflight hooks), then the remaining globals are
 * applied. Eventually `sipi_init` constructs and installs the `LuaConfig`;
 * while the C++ transport still owns the socket, the still-living server
 * installs it via `set_lua_config` — the same throwaway-installer shape as
 * `set_engine_context`, gone with the server at the cutover.
 */
#ifndef SIPI_FFI_LUA_CONFIG_H
#define SIPI_FFI_LUA_CONFIG_H

#include <memory>
#include <string>
#include <vector>

#include "shttps/lua/LuaServer.h"// shttps::LuaServer, LuaSetGlobalsFunc, RequestContext

namespace Sipi::ffi {

/*! One Lua globals installer + its user_data, mirroring an entry of
 *  `shttps::Server`'s per-request `lua_globals` list
 *  (`sipiConfGlobals`/`sqliteGlobals`/`sipiGlobals`). */
struct LuaGlobalsInstaller
{
  shttps::LuaSetGlobalsFunc func = nullptr;
  void *user_data = nullptr;
};

/*! Engine-held Lua configuration. The globals installers are applied in order
 *  after the init script runs (matching the registration order in the parity
 *  path). The `user_data` pointers and installer functions are non-owning and
 *  must outlive every Lua call (the installer outlives all requests). */
struct LuaConfig
{
  std::string init_script;//!< init-script SOURCE (defines pre_flight / file_pre_flight)
  std::string script_dir;//!< Lua package.path base (the `/?.lua` suffix is added by the factory)
  std::string jwt_secret;//!< server.generate_jwt / server.decode_jwt secret
  std::vector<LuaGlobalsInstaller> globals;//!< config.* / db / sipi.* installers, applied in order
  std::vector<shttps::LuaRoute> routes;//!< configured Lua routes (method/route/script), enumerated by sipi_routes
};

/*! Install the engine Lua config (copied into a file-static). Called once at
 *  server startup on the current transport path; superseded once `sipi_init`
 *  takes over. */
void set_lua_config(LuaConfig cfg);

/*! The installed Lua config. Returns a default-constructed (empty) config if
 *  `set_lua_config` was never called. */
[[nodiscard]] const LuaConfig &lua_config();

/*! Build a fully-configured, connection-less `LuaServer` for one request: runs
 *  the init script (so `server.*` is installed and the preflight hooks are
 *  defined), then applies the `config.*` / `db` / `sipi.*` globals — exactly the
 *  per-request VM `Server::process_request` builds, minus the `Connection`. Sets
 *  `ctx.jwt_secret` from the config. `ctx` MUST outlive the returned VM
 *  (`createGlobals` stores `&ctx` as a light-userdata global). Throws
 *  `shttps::Error` if the init script fails to load (caught by the caller's
 *  `sipi_guard`). */
[[nodiscard]] std::unique_ptr<shttps::LuaServer> make_lua_server(shttps::RequestContext &ctx);

}// namespace Sipi::ffi

#endif// SIPI_FFI_LUA_CONFIG_H
