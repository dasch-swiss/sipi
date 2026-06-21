/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ffi/preflight.h"

#include <exception>
#include <memory>
#include <optional>

#include "ffi/lua_config.h"
#include "logging/logger.h"
#include "shttps/lua/LuaServer.h"
#include "shttps/lua/request_context.h"

namespace Sipi::ffi {
namespace {

  using shttps::LuaServer;
  using shttps::LuaValstruct;
  using shttps::RequestContext;

  // Deliberate FFI-boundary normalization: the seam's char* inputs are
  // contractually non-null, but this is a C ABI surface (Phase C feeds it from
  // Rust), so a null collapses to an empty Lua arg rather than UB.
  std::shared_ptr<LuaValstruct> string_param(const char *s)
  {
    auto v = std::make_shared<LuaValstruct>();
    v->type = LuaValstruct::STRING_TYPE;
    v->value.s = (s != nullptr) ? s : "";
    return v;
  }

  std::shared_ptr<LuaValstruct> string_param(const std::string &s)
  {
    auto v = std::make_shared<LuaValstruct>();
    v->type = LuaValstruct::STRING_TYPE;
    v->value.s = s;
    return v;
  }

  // The raw `Cookie` header value, the Lua preflight hooks' `cookie` argument.
  // Snapshotted into ctx.headers (lower-cased) by make_request_context, matching
  // the legacy conn_obj.header("cookie").
  std::string cookie_header(const RequestContext &ctx)
  {
    const auto it = ctx.headers.find("cookie");
    return it != ctx.headers.end() ? it->second : std::string{};
  }

  std::optional<SipiPermType> perm_from_string(const std::string &s, bool extended)
  {
    if (s == "allow") { return SIPI_ALLOW; }
    if (s == "login") { return SIPI_LOGIN; }
    if (s == "restrict") { return SIPI_RESTRICT; }
    if (s == "deny") { return SIPI_DENY; }
    if (extended) {
      if (s == "clickthrough") { return SIPI_CLICKTHROUGH; }
      if (s == "kiosk") { return SIPI_KIOSK; }
      if (s == "external") { return SIPI_EXTERNAL; }
    }
    return std::nullopt;
  }

  // Parse the Lua return values into a PreflightDecision, mirroring the legacy
  // call_{iiif,file}_preflight parsing exactly. The permission is either a bare
  // string or a table carrying `type` plus extra string keys (watermark, size,
  // cookieUrl, …). `extended` adds the IIIF-only permissions
  // (clickthrough/kiosk/external) to the base allow/login/restrict/deny set.
  // Every failure logs the detail (the status alone crosses the C ABI) and maps
  // to 500, matching the legacy handler.
  std::expected<PreflightDecision, SipiStatus> parse_preflight(
    const std::vector<std::shared_ptr<LuaValstruct>> &rvals,
    const std::string &funcname,
    bool extended)
  {
    if (rvals.empty()) {
      log_err("Lua function %s must return at least one value", funcname.c_str());
      return std::unexpected(SipiStatus::InternalError);
    }

    PreflightDecision decision;
    std::string type_str;

    const auto &perm_val = rvals.at(0);
    if (perm_val->type == LuaValstruct::STRING_TYPE) {
      type_str = perm_val->value.s;
    } else if (perm_val->type == LuaValstruct::TABLE_TYPE) {
      const auto it = perm_val->value.table.find("type");
      if (it == perm_val->value.table.end()) {
        log_err("The permission value returned by Lua function %s has no type field!", funcname.c_str());
        return std::unexpected(SipiStatus::InternalError);
      }
      if (it->second->type != LuaValstruct::STRING_TYPE) {
        log_err("The permission 'type' returned by Lua function %s must be a string", funcname.c_str());
        return std::unexpected(SipiStatus::InternalError);
      }
      type_str = it->second->value.s;
      for (const auto &[key, val] : perm_val->value.table) {
        if (key == "type") { continue; }
        if (val->type != LuaValstruct::STRING_TYPE) {
          log_err("The '%s' value returned by Lua function %s must be a string", key.c_str(), funcname.c_str());
          return std::unexpected(SipiStatus::InternalError);
        }
        decision.kv.emplace_back(key, val->value.s);
      }
    } else {
      log_err("The permission value returned by Lua function %s was not valid", funcname.c_str());
      return std::unexpected(SipiStatus::InternalError);
    }

    const auto perm = perm_from_string(type_str, extended);
    if (!perm) {
      log_err("The permission returned by Lua function %s is not valid: %s", funcname.c_str(), type_str.c_str());
      return std::unexpected(SipiStatus::InternalError);
    }
    decision.type = *perm;

    if (decision.type == SIPI_DENY) {
      decision.kv.emplace_back("infile", "");
    } else {
      if (rvals.size() < 2) {
        log_err("Lua function %s returned other permission than 'deny', but it did not return a file path",
          funcname.c_str());
        return std::unexpected(SipiStatus::InternalError);
      }
      const auto &infile_val = rvals.at(1);
      if (infile_val->type != LuaValstruct::STRING_TYPE) {
        log_err("The file path returned by Lua function %s was not a string", funcname.c_str());
        return std::unexpected(SipiStatus::InternalError);
      }
      decision.kv.emplace_back("infile", infile_val->value.s);
    }

    return decision;
  }

  // Build a per-call VM from the engine Lua config bound to the caller's request
  // context, run the named hook, and parse its result. The init script defines
  // the hook; createGlobals (bound to ctx) + the config.*/db/sipi.* installers
  // give it server.*/config/db/sipi reading the real request.
  std::expected<PreflightDecision, SipiStatus> run_preflight(
    RequestContext &ctx,
    const std::string &funcname,
    const std::vector<std::shared_ptr<LuaValstruct>> &lvals,
    bool extended)
  {
    std::vector<std::shared_ptr<LuaValstruct>> rvals;
    try {
      auto vm = make_lua_server(ctx);
      rvals = vm->executeLuafunction(funcname, lvals);
    } catch (const std::exception &e) {
      log_err("Lua function %s failed: %s", funcname.c_str(), e.what());
      return std::unexpected(SipiStatus::InternalError);
    }
    return parse_preflight(rvals, funcname, extended);
  }

}// namespace

std::expected<PreflightDecision, SipiStatus>
  build_preflight(const char *prefix, const char *identifier, RequestContext &ctx)
{
  const std::vector<std::shared_ptr<LuaValstruct>> lvals = {
    string_param(prefix),
    string_param(identifier),
    string_param(cookie_header(ctx)),
  };
  return run_preflight(ctx, "pre_flight", lvals, /*extended=*/true);
}

std::expected<PreflightDecision, SipiStatus> build_file_preflight(const char *filepath, RequestContext &ctx)
{
  const std::vector<std::shared_ptr<LuaValstruct>> lvals = {
    string_param(filepath),
    string_param(cookie_header(ctx)),
  };
  return run_preflight(ctx, "file_pre_flight", lvals, /*extended=*/false);
}

void apply_preflight(PreflightDecision &&decision, SipiPermType *type, SipiKVFn emit_kv, void *kv_ctx)
{
  if (type != nullptr) { *type = decision.type; }
  if (emit_kv != nullptr) {
    for (const auto &[key, value] : decision.kv) { emit_kv(kv_ctx, key.c_str(), value.c_str()); }
  }
}

const char *perm_type_to_string(SipiPermType type)
{
  switch (type) {
  case SIPI_ALLOW:
    return "allow";
  case SIPI_LOGIN:
    return "login";
  case SIPI_CLICKTHROUGH:
    return "clickthrough";
  case SIPI_KIOSK:
    return "kiosk";
  case SIPI_EXTERNAL:
    return "external";
  case SIPI_RESTRICT:
    return "restrict";
  case SIPI_DENY:
    return "deny";
  }
  return "deny";// unreachable; the enum is exhaustively handled above
}

}// namespace Sipi::ffi
