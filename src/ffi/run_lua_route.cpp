/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/run_lua_route.h"

#include <unistd.h>// access, R_OK

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "ffi/lua_config.h"// make_lua_server
#include "ffi/response_sink.h"// FfiResponseSink
#include "logging/logger.h"
#include "shttps/lua/LuaServer.h"

namespace Sipi::ffi {
namespace {

  using shttps::LuaServer;
  using shttps::ResponseSink;

  // The Connection's default response status; the route's server.sendStatus
  // overrides it (parity with the transport, where Connection starts at 200).
  constexpr int DEFAULT_STATUS = 200;

  // Embedded-Lua (.elua): alternate raw HTML and <lua>…</lua> chunks, mirroring
  // the transport script_handler's elua branch. HTML is written to the sink;
  // each chunk runs in the per-request VM. Returns 0, or 500 on a Lua error.
  int run_elua(LuaServer &vm, ResponseSink &sink, const std::string &code, const std::string &path)
  {
    std::size_t pos = 0;
    std::size_t end = 0;// end of the last </lua>
    while ((pos = code.find("<lua>", end)) != std::string::npos) {
      const std::string html = code.substr(end, pos - end);
      pos += 5;// past "<lua>"
      if (!html.empty()) { sink.write(html.data(), html.size()); }

      std::string luastr;
      if ((end = code.find("</lua>", pos)) != std::string::npos) {
        luastr = code.substr(pos, end - pos);
        end += 6;// past "</lua>"
      } else {
        luastr = code.substr(pos);
      }

      try {
        vm.executeChunk(luastr, path);
      } catch (const shttps::Error &e) {
        log_err("sipi_run_lua_route: error executing chunk in %s: %s", path.c_str(), e.to_string().c_str());
        return 500;
      }
    }
    const std::string html = code.substr(end);
    if (!html.empty()) { sink.write(html.data(), html.size()); }
    return 0;
  }

}// namespace

int run_lua_route(const char *script_path, shttps::RequestContext &rc, const SipiResponse &resp)
{
  if (script_path == nullptr || access(script_path, R_OK) != 0) {
    log_err("sipi_run_lua_route: script '%s' not readable", script_path != nullptr ? script_path : "(null)");
    return 404;
  }
  const std::string path = script_path;
  const std::size_t dot = path.find_last_of('.');
  const std::string ext = (dot != std::string::npos) ? path.substr(dot + 1) : std::string{};

  std::ifstream inf(path);
  if (!inf.is_open()) {
    // access(R_OK) passed above, so an open failure here means the script was
    // removed between the check and the open (TOCTOU). Fail like not-readable
    // rather than running an empty chunk and returning an empty 200.
    log_err("sipi_run_lua_route: script '%s' became unreadable after the access() check", path.c_str());
    return 404;
  }
  std::stringstream sstr;
  sstr << inf.rdbuf();
  const std::string code = sstr.str();

  // The single response seam: the route's output drives the same SipiResponse
  // the serve paths do. make_lua_server runs the init script (defining server.*)
  // and sets rc.jwt_secret; the route then reads rc through server.*.
  FfiResponseSink sink(resp);
  rc.response = &sink;
  std::unique_ptr<LuaServer> vm = make_lua_server(rc);

  sink.set_status(DEFAULT_STATUS);

  if (ext == "lua") {
    try {
      vm->executeChunk(code, path);
    } catch (const shttps::Error &e) {
      log_err("sipi_run_lua_route: error executing %s: %s", path.c_str(), e.to_string().c_str());
      return 500;
    }
    return 0;
  }
  if (ext == "elua") { return run_elua(*vm, sink, code, path); }

  log_err("sipi_run_lua_route: script %s has no valid extension '%s'", path.c_str(), ext.c_str());
  return 500;
}

}// namespace Sipi::ffi
