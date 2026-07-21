/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "shttps/transport/Connection.h"
#include "shttps/lua/LuaServer.h"
#include "shttps/util/Parsing.h"
#include "shttps/util/UrlDecode.h"

#include "SipiError.h"
#include "SipiImage.h"
#include "SipiImageError.h"
#include "iiifparser/SipiIdentifier.h"
#include "iiifparser/SipiQualityFormat.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiRotation.h"
#include "iiifparser/SipiSize.h"

#include "logging/logger.h"
#include "SipiHttpServer.h"
#include "observability/metrics.h"
#include "observability/profiling.h"
#include "generated/SipiVersion.h"
#include "SipiMemoryBudget.h"
#include "SipiPeakMemory.h"
#include "SipiRateLimiter.h"
#include "iiifparser/SipiDecodeDims.h"
#include "favicon.h"
#include "ffi/engine_context.h"
#include "ffi/preflight.h"
#include "ffi/sipi_ffi.h"
#include "shttps/lua/request_context.h"
#include "shttps/transport/connection_request_context.h"
#include "handlers/iiif_handler.h"
#include "jansson.h"

using namespace shttps;

namespace Sipi {

using observability::Metrics;

/*!
 * The name of the Lua function that checks permissions before a file is returned to an HTTP client.
 */
static const std::string iiif_preflight_funcname = "pre_flight";
static const std::string file_preflight_funcname = "file_pre_flight";

using IiifParams = enum {
  iiif_prefix = 0,//!< http://{url}/*{prefix}*/{id}/{region}/{size}/{rotation}/{quality}.{format}
  iiif_identifier = 1,//!< http://{url}/{prefix}/*{id}*/{region}/{size}/{rotation}/{quality}.{format}
  iiif_region = 2,//!< http://{url}/{prefix}/{id}/{region}/{size}/{rotation}/{quality}.{format}
  iiif_size = 3,//!< http://{url}/{prefix}/{id}/{region}/*{size}*/{rotation}/{quality}.{format}
  iiif_rotation = 4,//!< http://{url}/{prefix}/{id}/{region}/{size}/*{rotation}*/{quality}.{format}
  iiif_qualityformat = 5,//!< http://{url}/{prefix}/{id}/{region}/{size}/{rotation}/*{quality}.{format}*
};

/*!
 * Sends an HTTP error response to the client, and logs the error if appropriate.
 *
 * \param conn_obj the server connection.
 * \param code the HTTP status code to be returned.
 * \param errmsg the error message to be returned.
 */
static void send_error(Connection &conn_obj, Connection::StatusCodes code, const std::string &errmsg)
{
  conn_obj.status(code);
  conn_obj.setBuffer();
  conn_obj.header("Content-Type", "text/plain");

  std::string http_err_name;
  constexpr bool log_err_b(true);// True if the error should be logged.

  switch (code) {
  case Connection::BAD_REQUEST:
    http_err_name = "Bad Request";
    // log_err = false;
    break;
  case Connection::FORBIDDEN:
    http_err_name = "Forbidden";
    // log_err = false;
    break;
  case Connection::UNAUTHORIZED:
    http_err_name = "Unauthorized";
    break;
  case Connection::NOT_FOUND:
    http_err_name = "Not Found";
    // log_err = false;
    break;
  case Connection::INTERNAL_SERVER_ERROR:
    http_err_name = "Internal Server Error";
    break;
  case Connection::NOT_IMPLEMENTED:
    http_err_name = "Not Implemented";
    // log_err = false;
    break;
  case Connection::SERVICE_UNAVAILABLE:
    http_err_name = "Service Unavailable";
    break;
  case Connection::TOO_MANY_REQUESTS:
    http_err_name = "Too Many Requests";
    break;
  default:
    http_err_name = "Unknown error";
    break;
  }

  // Send an error message to the client.
  conn_obj << http_err_name;
  if (!errmsg.empty()) { conn_obj << ": " << errmsg; }

  conn_obj.flush();
  // Log the error if appropriate.
  if (log_err_b) {
    std::stringstream log_msg_stream;
    log_msg_stream << "GET " << conn_obj.uri() << " failed (" << http_err_name << ")";

    if (!errmsg.empty()) { log_msg_stream << ": " << errmsg; }
    log_err("%s", log_msg_stream.str().c_str());
  }
}

//=========================================================================

/*!
 * Sends an HTTP error response to the client, and logs the error if appropriate.
 *
 * \param conn_obj the server connection.
 * \param code the HTTP status code to be returned.
 * \param err an exception describing the error.
 */
static void send_error(Connection &conn_obj, Connection::StatusCodes code, const SipiError &err)
{
  send_error(conn_obj, code, err.to_string());
}

//=========================================================================

/*!
 * Sends an HTTP error response to the client, and logs the error if appropriate.
 *
 * \param conn_obj the server connection.
 * \param code the HTTP status code to be returned.
 * \param err an exception describing the error.
 */
static void send_error(Connection &conn_obj, Connection::StatusCodes code, const Error &err)
{
  send_error(conn_obj, code, err.to_string());
}

//=========================================================================

/*!
 * Sends an HTTP error response to the client, and logs the error if appropriate.
 *
 * \param conn_obj the server connection.
 * \param code the HTTP status code to be returned.
 */
static void send_error(Connection &conn_obj, Connection::StatusCodes code) { send_error(conn_obj, code, ""); }

//=========================================================================

/*!
 * R1: Check if a decoded string contains path traversal components.
 *
 * Checks for ".." as a path component (between '/' delimiters, at start, or at end).
 * The input should already be URL-decoded. For double-encoded inputs (%252e%252e),
 * callers should decode once more and re-check.
 *
 * \param decoded The URL-decoded string to check.
 * \return true if traversal components are found.
 */
[[nodiscard]] static bool contains_traversal(std::string_view decoded)
{
  // Check for ".." as a complete path component
  if (decoded == "..") { return true; }
  if (decoded.starts_with("../")) { return true; }
  if (decoded.ends_with("/..")) { return true; }
  if (decoded.find("/../") != std::string_view::npos) { return true; }

  // Also check for percent-encoded variants in case the input was not fully decoded.
  // Case-insensitive check for %2e%2e (single-encoded dot-dot)
  std::string lower(decoded.size(), '\0');
  std::transform(decoded.begin(), decoded.end(), lower.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower.find("%2e%2e") != std::string::npos) { return true; }
  // Double-encoded: %252e%252e
  if (lower.find("%252e%252e") != std::string::npos) { return true; }

  return false;
}

/// Result of path validation: resolved path, file-not-found, or traversal detected.
enum class PathValidation { OK, NOT_FOUND, TRAVERSAL };

struct PathValidationResult {
  PathValidation status;
  std::string resolved_path;// only valid when status == OK
};

/*!
 * R2: Validate that a resolved file path is within the image root directory.
 *
 * Resolves the file path via realpath() and verifies it starts with the
 * resolved imgroot prefix. The imgroot must already be resolved via realpath()
 * at server startup.
 *
 * Returns NOT_FOUND if the file doesn't exist (let caller handle 404),
 * TRAVERSAL if the resolved path escapes imgroot (caller should return 400),
 * or OK with the resolved path on success.
 */
[[nodiscard]] static PathValidationResult
validate_resolved_path(std::string_view file_path, std::string_view resolved_imgroot)
{
  char resolved[PATH_MAX];
  if (realpath(std::string(file_path).c_str(), resolved) == nullptr) {
    // File doesn't exist — not a traversal, let the caller handle 404
    return { PathValidation::NOT_FOUND, {} };
  }

  std::string_view resolved_view(resolved);
  // Verify the resolved path starts with the resolved imgroot
  if (!resolved_view.starts_with(resolved_imgroot)) {
    return { PathValidation::TRAVERSAL, {} };
  }
  // Ensure the path is either exactly imgroot or continues with '/'
  // (prevents imgroot="/foo/bar" matching "/foo/barbaz/file")
  if (resolved_view.size() > resolved_imgroot.size()
      && resolved_view[resolved_imgroot.size()] != '/') {
    return { PathValidation::TRAVERSAL, {} };
  }

  return { PathValidation::OK, std::string(resolved) };
}

//=========================================================================

/*!
 * R8: Strip CR, LF, null, and control characters from a string intended for use
 * in HTTP headers. Preserves non-ASCII bytes (>= 0x80) so that the subsequent
 * is_ascii check can route to RFC 6266 filename* encoding.
 */
[[nodiscard]] static std::string sanitize_header_value(std::string_view input)
{
  std::string result;
  result.reserve(input.size());
  for (unsigned char c : input) {
    if (c == '\r' || c == '\n' || c == '\0' || (c < 0x20) || c == 0x7F) {
      continue;
    }
    result += static_cast<char>(c);
  }
  return result;
}

/*!
 * Check if a string contains only ASCII printable characters.
 */
[[nodiscard]] static bool is_ascii(std::string_view s)
{
  return std::all_of(s.begin(), s.end(),
    [](unsigned char c) { return c >= 0x20 && c < 0x7F; });
}

/*!
 * R9: Escape '"' and '\' per RFC 2616 quoted-string rules for use in
 * Content-Disposition filename="..." parameters.
 */
[[nodiscard]] static std::string escape_quoted_string(std::string_view input)
{
  std::string result;
  result.reserve(input.size());
  for (char c : input) {
    if (c == '"' || c == '\\') {
      result += '\\';
    }
    result += c;
  }
  return result;
}

/*!
 * R9: Percent-encode a string for use in RFC 6266 filename*=UTF-8''...
 * Encodes all bytes except unreserved characters (RFC 3986 section 2.3).
 */
[[nodiscard]] static std::string percent_encode_rfc6266(std::string_view input)
{
  static const char hex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(input.size() * 3);
  for (unsigned char c : input) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || c == '-' || c == '.' || c == '_' || c == '~') {
      result += static_cast<char>(c);
    } else {
      result += '%';
      result += hex[c >> 4];
      result += hex[c & 0x0F];
    }
  }
  return result;
}

//=========================================================================

/*!
 * R25: Resolve client identity for rate limiting.
 * Priority: X-Forwarded-For (rightmost) > peer IP.
 */
static std::string resolve_client_id(Connection &conn_obj)
{
  std::string xff = conn_obj.header("X-Forwarded-For");
  if (!xff.empty()) {
    auto pos = xff.rfind(',');
    auto ip = (pos != std::string::npos) ? xff.substr(pos + 1) : xff;
    // Trim leading whitespace
    size_t start = ip.find_first_not_of(" \t");
    return (start != std::string::npos) ? ip.substr(start) : ip;
  }
  return conn_obj.peer_ip();
}

//=========================================================================

// emit_kv target for the preflight adapters below: fold the seam's open
// key/value channel (infile + restrict extras: watermark/size/cookieUrl/…) back
// into the string-keyed map the existing handlers consume.
static void preflight_collect_kv(void *ctx, const char *key, const char *value)
{
  static_cast<std::unordered_map<std::string, std::string> *>(ctx)->insert_or_assign(key, value);
}

/*!
 * Runs the Lua `pre_flight` hook for an IIIF request through the C-ABI seam
 * (`::sipi_preflight`) and returns the permission + file path (+ any restrict
 * extras) as the string-keyed map the handlers consume.
 *
 * The Lua VM is built behind the FFI from the engine Lua config, bound to a
 * RequestContext snapshot of conn_obj (so `server.header` / `server.cookies`
 * resolve from the real request) — the path the Rust shell drives.
 * The caller still gates on `luaserver.luaFunctionExists(pre_flight)`; only the
 * execution moved behind the seam. Throws SipiError if the hook fails (the
 * detail is logged engine-side; only the status crosses the seam).
 *
 * \param conn_obj the server connection (request fields + response sink).
 * \param prefix the IIIF prefix.
 * \param identifier the IIIF identifier.
 * \return permission map (type + infile + any restrict extras)
 */
static std::unordered_map<std::string, std::string>
  call_iiif_preflight(Connection &conn_obj, const std::string &prefix, const std::string &identifier)
{
  std::unordered_map<std::string, std::string> preflight_info;
  SipiPermType perm{};
  shttps::ConnectionResponseSink sink(conn_obj);
  shttps::RequestContext ctx = shttps::make_request_context(conn_obj, sink);
  // resp is NULL: ctx.response is already the live ConnectionResponseSink built
  // above, so sipi_preflight leaves it untouched rather than overwriting it.
  const int rc = ::sipi_preflight(prefix.c_str(),
    identifier.c_str(),
    reinterpret_cast<SipiRequestContext *>(&ctx),
    &perm,
    preflight_collect_kv,
    &preflight_info,
    nullptr);
  if (rc != 0) { throw SipiError("pre_flight failed"); }
  preflight_info["type"] = Sipi::ffi::perm_type_to_string(perm);
  return preflight_info;
}

//=========================================================================

/*!
 * Runs the Lua `file_pre_flight` hook for the `/file` media-serving path
 * (audio / video / PDF / any non-IIIF file) through `::sipi_file_preflight`.
 * Same contract as call_iiif_preflight; narrower permission set.
 *
 * \param conn_obj the server connection (request fields + response sink).
 * \param filepath the resolved on-disk file path.
 * \return permission map (type + infile)
 */
static std::unordered_map<std::string, std::string>
  call_file_preflight(Connection &conn_obj, const std::string &filepath)
{
  std::unordered_map<std::string, std::string> preflight_info;
  SipiPermType perm{};
  shttps::ConnectionResponseSink sink(conn_obj);
  shttps::RequestContext ctx = shttps::make_request_context(conn_obj, sink);
  // resp is NULL: ctx.response is already the live ConnectionResponseSink built
  // above, so sipi_file_preflight leaves it untouched rather than overwriting it.
  const int rc = ::sipi_file_preflight(filepath.c_str(),
    reinterpret_cast<SipiRequestContext *>(&ctx),
    &perm,
    preflight_collect_kv,
    &preflight_info,
    nullptr);
  if (rc != 0) { throw SipiError("file_pre_flight failed"); }
  preflight_info["type"] = Sipi::ffi::perm_type_to_string(perm);
  return preflight_info;
}

//=========================================================================


//
// ToDo: Prepare for IIIF Authentication API !!!!
//
/**
 * This internal method checks if the image file is readable and
 * uses the pre_flight script to check permissions.
 *
 * @param conn_obj Connection object
 * @param serv The Server instance
 * @param luaserver The Lua server instance
 * @param params the IIIF parameters
 * @param prefix_as_path
 * @return Pair of strings with permissions and filepath
 */
static std::unordered_map<std::string, std::string> check_file_access(Connection &conn_obj,
  SipiHttpServer *serv,
  shttps::LuaServer &luaserver,
  std::vector<std::string> &params,
  bool prefix_as_path)
{
  std::string infile;

  SipiIdentifier sid = SipiIdentifier(params[iiif_identifier]);

  // R1: Early rejection of path traversal in identifier and prefix
  const auto decoded_identifier = urldecode(sid.getIdentifier());
  if (contains_traversal(decoded_identifier)) {
    log_warn("Path traversal blocked: client=%s identifier=%s",
      conn_obj.peer_ip().c_str(), params[iiif_identifier].c_str());
    throw SipiError("Invalid IIIF identifier");
  }
  if (prefix_as_path && contains_traversal(urldecode(params[iiif_prefix]))) {
    log_warn("Path traversal blocked in prefix: client=%s prefix=%s",
      conn_obj.peer_ip().c_str(), params[iiif_prefix].c_str());
    throw SipiError("Invalid IIIF identifier");
  }

  std::unordered_map<std::string, std::string> pre_flight_info;
  if (luaserver.luaFunctionExists(iiif_preflight_funcname)) {
    pre_flight_info = call_iiif_preflight(conn_obj,
      urldecode(params[iiif_prefix]),
      sid.getIdentifier());// may throw SipiError
    infile = pre_flight_info["infile"];
  } else {
    if (prefix_as_path) {
      infile = serv->imgroot() + "/" + urldecode(params[iiif_prefix]) + "/" + sid.getIdentifier();
    } else {
      infile = serv->imgroot() + "/" + urldecode(sid.getIdentifier());
    }
    pre_flight_info["type"] = "allow";
  }

  // R2: Validate resolved path is within imgroot (covers Lua paths, prefix paths, and direct paths)
  auto validated = validate_resolved_path(infile, serv->resolved_imgroot());
  if (validated.status == PathValidation::TRAVERSAL) {
    log_warn("Path traversal blocked (realpath): client=%s identifier=%s",
      conn_obj.peer_ip().c_str(), params[iiif_identifier].c_str());
    // R3: Error responses contain no internal filesystem paths
    throw SipiError("Invalid IIIF identifier");
  }
  if (validated.status == PathValidation::OK) {
    infile = validated.resolved_path;
  }
  // If NOT_FOUND, keep original infile — access() below will fail with proper error

  //
  // test if we have access to the file
  //
  if (access(infile.c_str(), R_OK) != 0) {
    // R3: Do not leak internal filesystem path in error message
    throw SipiError("Image file not accessible");
  }
  pre_flight_info["infile"] = infile;
  return pre_flight_info;
}

//=========================================================================

static void serve_redirect(Connection &conn_obj, const std::vector<std::string> &params)
{
  conn_obj.setBuffer();
  conn_obj.status(Connection::SEE_OTHER);
  const std::string host = conn_obj.host();
  std::string redirect;

  if (conn_obj.secure()) {
    redirect += "https://" + host + "/";
  } else {
    redirect += "http://" + host + "/";
  }

  if (!params[iiif_prefix].empty()) {
    redirect += params[iiif_prefix] + "/" + params[iiif_identifier] + "/info.json";
  } else {
    redirect += params[iiif_identifier] + "/info.json";
  }

  // R10: Sanitize Location header to prevent CRLF injection
  conn_obj.header("Location", sanitize_header_value(redirect));
  conn_obj.header("Content-Type", "text/plain");
  conn_obj << "Redirect to " << sanitize_header_value(redirect);
  log_info("GET: redirect to %s", redirect.c_str());
  conn_obj.flush();
}

//=========================================================================

//
// ToDo: Prepare for IIIF Authentication API !!!!
//
/**
 * This internal method serves the IIIF info.json file.
 *
 * @param conn_obj Connection object
 * @param serv The Server instance
 * @param luaserver The Lua server instance
 * @param params the IIIF parameters
 * @param prefix_as_path
 */
static void serve_info_json_file(Connection &conn_obj,
  SipiHttpServer *serv,
  shttps::LuaServer &luaserver,
  std::vector<std::string> &params,
  bool prefix_as_path)
{
  Connection::StatusCodes http_status = Connection::StatusCodes::OK;

  //
  // here we start the lua script which checks for permissions
  //
  std::unordered_map<std::string, std::string> access;
  try {
    access = check_file_access(conn_obj, serv, luaserver, params, prefix_as_path);
  } catch (SipiError &err) {
    send_error(conn_obj, Connection::NOT_FOUND, err);
    return;
  }

  std::string actual_mimetype = shttps::Parsing::getBestFileMimetype(access["infile"]);

  bool is_image_file =
    ((actual_mimetype == "image/tiff") || (actual_mimetype == "image/jpeg") || (actual_mimetype == "image/png")
      || (actual_mimetype == "image/jpx") || (actual_mimetype == "image/jp2"));

  json_t *root = json_object();
  SipiIdentifier sid = SipiIdentifier(params[iiif_identifier]);
  int pagenum = sid.getPage();

  if (is_image_file) {
    json_object_set_new(root, "@context", json_string("http://iiif.io/api/image/3/context.json"));
  } else {
    json_object_set_new(root, "@context", json_string("http://sipi.io/api/file/3/context.json"));
  }

  std::string proto = conn_obj.secure() ? std::string("https://") : std::string("http://");
  std::string host = conn_obj.header("host");
  std::string id;
  if (params[iiif_prefix] == "") {
    id = proto + host + "/" + params[iiif_identifier];
  } else {
    id = proto + host + "/" + params[iiif_prefix] + "/" + params[iiif_identifier];
  }
  json_object_set_new(root, "id", json_string(id.c_str()));

  if (is_image_file) {
    json_object_set_new(root, "type", json_string("ImageService3"));
    json_object_set_new(root, "protocol", json_string("http://iiif.io/api/image"));
    json_object_set_new(root, "profile", json_string("level2"));
  } else {
    json_object_set_new(root, "internalMimeType", json_string(actual_mimetype.c_str()));

    struct stat fstatbuf;
    if (stat(access["infile"].c_str(), &fstatbuf) != 0) { throw Error("Cannot fstat file!"); }
    json_object_set_new(root, "fileSize", json_integer(fstatbuf.st_size));
  }

  //
  // IIIF Authentication API stuff
  //
  if ((access["type"] == "login") || (access["type"] == "clickthrough") || (access["type"] == "kiosk")
      || (access["type"] == "external")) {
    json_t *service = json_object();
    try {
      std::string cookieUrl = access.at("cookieUrl");
      json_object_set_new(service, "@context", json_string("http://iiif.io/api/auth/1/context.json"));
      json_object_set_new(service, "@id", json_string(cookieUrl.c_str()));
      if (access["type"] == "login") {
        json_object_set_new(service, "profile", json_string("http://iiif.io/api/auth/1/login"));
      } else if (access["type"] == "clickthrough") {
        json_object_set_new(service, "profile", json_string("http://iiif.io/api/auth/1/clickthrough"));
      } else if (access["type"] == "kiosk") {
        json_object_set_new(service, "profile", json_string("http://iiif.io/api/auth/1/kiosk"));
      } else if (access["type"] == "external") {
        json_object_set_new(service, "profile", json_string("http://iiif.io/api/auth/1/external"));
      }
      for (auto &item : access) {
        if (item.first == "cookieUrl") continue;
        if (item.first == "tokenUrl") continue;
        if (item.first == "logoutUrl") continue;
        if (item.first == "infile") continue;
        if (item.first == "type") continue;
        json_object_set_new(service, item.first.c_str(), json_string(item.second.c_str()));
      }
      json_t *subservices = json_array();
      try {
        std::string tokenUrl = access.at("tokenUrl");
        json_t *token_service = json_object();
        json_object_set_new(token_service, "@id", json_string(tokenUrl.c_str()));
        json_object_set_new(token_service, "profile", json_string("http://iiif.io/api/auth/1/token"));
        json_array_append_new(subservices, token_service);
      } catch (const std::out_of_range &err) {
        send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, "Pre_flight_script has login type but no tokenUrl!");
        return;
      }
      try {
        std::string logoutUrl = access.at("logoutUrl");
        json_t *logout_service = json_object();
        json_object_set_new(logout_service, "@id", json_string(logoutUrl.c_str()));
        json_object_set_new(logout_service, "profile", json_string("http://iiif.io/api/auth/1/logout"));
        json_array_append_new(subservices, logout_service);
      } catch (const std::out_of_range &err) {}
      json_object_set_new(service, "service", subservices);
    } catch (const std::out_of_range &err) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, "Pre_flight_script has login type but no cookieUrl!");
      return;
    }
    json_t *services = json_array();
    json_array_append_new(services, service);
    json_object_set_new(root, "service", services);
    http_status = Connection::StatusCodes::UNAUTHORIZED;
  }

  if (is_image_file) {
    size_t width, height;
    size_t t_width, t_height;
    int clevels;
    int numpages = 0;

    // Image shape used to come from `cache->getSize()` keyed on the original
    // filepath; that parasitic memoization was deleted (DEV-6538) because
    // `read_shape`'s fast path now hits the same packet (ADR-0004 / DEV-6379).
    // Always go through the format handler.
    Sipi::SipiImage tmpimg;
    Sipi::SipiImgInfo info;
    try {
      info = tmpimg.read_shape(access["infile"]);
    } catch (SipiImageError &err) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err.to_string());
      return;
    }
    if (info.success == SipiImgInfo::FAILURE) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, "Error getting image dimensions!");
      return;
    }
    width = info.width;
    height = info.height;
    t_width = info.tile_width;
    t_height = info.tile_height;
    clevels = info.clevels;
    numpages = info.numpages;

    //
    // basic info
    //
    json_object_set_new(root, "width", json_integer(width));
    json_object_set_new(root, "height", json_integer(height));
    if (numpages > 0) { json_object_set_new(root, "numpages", json_integer(numpages)); }

    json_t *sizes = json_array();
    const int cnt = clevels > 0 ? clevels : 5;
    for (int i = 1; i < cnt; i++) {
      SipiSize size(i);
      size_t w, h;
      int r;
      bool ro;
      size.get_size(width, height, w, h, r, ro);
      if ((w < 128) && (h < 128)) break;
      json_t *sobj = json_object();
      json_object_set_new(sobj, "width", json_integer(w));
      json_object_set_new(sobj, "height", json_integer(h));
      json_array_append_new(sizes, sobj);
    }
    json_object_set_new(root, "sizes", sizes);

    if (t_width > 0 && t_height > 0) {
      json_t *tiles = json_array();
      json_t *tileobj = json_object();
      json_object_set_new(tileobj, "width", json_integer(t_width));
      json_object_set_new(tileobj, "height", json_integer(t_height));
      json_t *scaleFactors = json_array();
      for (int i = 1; i < cnt; i++) { json_array_append_new(scaleFactors, json_integer(i)); }
      json_object_set_new(tileobj, "scaleFactors", scaleFactors);
      json_array_append_new(tiles, tileobj);
      json_object_set_new(root, "tiles", tiles);
    }

    const char *extra_formats_str[] = { "tif", "jp2" };
    json_t *extra_formats = json_array();
    for (unsigned int i = 0; i < sizeof(extra_formats_str) / sizeof(char *); i++) {
      json_array_append_new(extra_formats, json_string(extra_formats_str[i]));
    }
    json_object_set_new(root, "extraFormats", extra_formats);

    json_t *prefformats = json_array();// ToDo: should be settable from LUA preflight (get info from DB)
    json_array_append_new(prefformats, json_string("jpg"));
    json_array_append_new(prefformats, json_string("tif"));
    json_array_append_new(prefformats, json_string("jp2"));
    json_array_append_new(prefformats, json_string("png"));
    json_object_set_new(root, "preferredFormats", prefformats);

    //
    // extra features
    //
    const char *extraFeaturesList[] = { "baseUriRedirect",
      "canonicalLinkHeader",
      "cors",
      "jsonldMediaType",
      "mirroring",
      "profileLinkHeader",
      "regionByPct",
      "regionByPx",
      "regionSquare",
      "rotationArbitrary",
      "rotationBy90s",
      "sizeByConfinedWh",
      "sizeByH",
      "sizeByPct",
      "sizeByW",
      "sizeByWh",
      "sizeUpscaling" };
    json_t *extraFeatures = json_array();
    for (unsigned int i = 0; i < sizeof(extraFeaturesList) / sizeof(char *); i++) {
      json_array_append_new(extraFeatures, json_string(extraFeaturesList[i]));
    }

    json_object_set_new(root, "extraFeatures", extraFeatures);
  }
  conn_obj.status(http_status);
  conn_obj.setBuffer();// we want buffered output, since we send JSON text...

  conn_obj.header("Access-Control-Allow-Origin", "*");
  const std::string contenttype = conn_obj.header("accept");
  if (is_image_file) {
    if (!contenttype.empty() && (contenttype == "application/ld+json")) {
      conn_obj.header("Content-Type", "application/ld+json;profile=\"http://iiif.io/api/image/3/context.json\"");
    } else {
      conn_obj.header("Content-Type", "application/json");
      conn_obj.header("Link",
        "<http://iiif.io/api/image/3/context.json>; rel=\"http://www.w3.org/ns/json-ld#context\"; "
        "type=\"application/ld+json\"");
    }
  } else {
    if (!contenttype.empty() && (contenttype == "application/ld+json")) {
      conn_obj.header("Content-Type", "application/ld+json;profile=\"http://sipi.io/api/file/3/context.json\"");
    } else {
      conn_obj.header("Content-Type", "application/json");
      conn_obj.header("Link",
        "<http://sipi.io/api/file/3/context.json>; rel=\"http://www.w3.org/ns/json-ld#context\"; "
        "type=\"application/ld+json\"");
    }
  }

  char *json_str = json_dumps(root, JSON_INDENT(3));
  conn_obj.sendAndFlush(json_str, strlen(json_str));
  free(json_str);
  json_decref(root);
}

//=========================================================================

/**
 * \brief This internal method serves the knora.json file, e.g., https://server/prefix/identifier.jp2/knora.json.
 * \param conn_obj
 * \param serv
 * \param luaserver
 * \param params
 * \param prefix_as_path
 */
static void serve_knora_json_file(Connection &conn_obj,
  SipiHttpServer *serv,
  shttps::LuaServer &luaserver,
  std::vector<std::string> &params,
  bool prefix_as_path)
{
  conn_obj.setBuffer();// we want buffered output, since we send JSON text...

  // set the origin
  const std::string origin = conn_obj.header("origin");
  log_debug("knora_send_info: host header %s", origin.c_str());
  if (origin.empty()) {
    conn_obj.header("Access-Control-Allow-Origin", "*");
  } else {
    conn_obj.header("Access-Control-Allow-Origin", origin);
  }

  //
  // here we start the lua script which checks for permissions
  //
  std::unordered_map<std::string, std::string> access;
  try {
    access = check_file_access(conn_obj, serv, luaserver, params, prefix_as_path);
  } catch (SipiError &err) {
    send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err);
    return;
  }

  std::string infile = access["infile"];

  SipiIdentifier sid = SipiIdentifier(params[iiif_identifier]);

  conn_obj.header("Content-Type", "application/json");

  json_t *root = json_object();
  json_object_set_new(root, "@context", json_string("http://sipi.io/api/file/3/context.json"));

  std::string proto = conn_obj.secure() ? std::string("https://") : std::string("http://");
  std::string host = conn_obj.header("host");
  std::string id;

  if (params[iiif_prefix] == "") {
    id = proto + host + "/" + params[iiif_identifier];
  } else {
    id = proto + host + "/" + params[iiif_prefix] + "/" + params[iiif_identifier];
  }
  json_object_set_new(root, "id", json_string(id.c_str()));

  //
  // read sidecar file if available
  //
  size_t pos = infile.rfind(".");
  std::string sidecarname = infile.substr(0, pos) + ".info";

  std::ifstream sidecar(sidecarname);
  std::string orig_filename = "";
  std::string orig_checksum = "";
  std::string derivative_checksum = "";

  std::double_t sidecar_duration = -1;
  std::double_t sidecar_fps = -1;
  std::double_t sidecar_height = -1;
  std::double_t sidecar_width = -1;

  if (sidecar.good()) {
    std::stringstream ss;
    ss << sidecar.rdbuf();// read the file
    json_t *scroot;
    json_error_t error;
    scroot = json_loads(ss.str().c_str(), 0, &error);
    const char *key;
    json_t *value;
    if (scroot) {
      void *iter = json_object_iter(scroot);
      while (iter) {
        key = json_object_iter_key(iter);
        value = json_object_iter_value(iter);
        if (std::strcmp("originalFilename", key) == 0) {
          orig_filename = json_string_value(value);
        } else if (std::strcmp("checksumOriginal", key) == 0) {
          orig_checksum = json_string_value(value);
        } else if (std::strcmp("checksumDerivative", key) == 0) {
          derivative_checksum = json_string_value(value);
        } else if (std::strcmp("duration", key) == 0) {
          sidecar_duration = json_number_value(value);
        } else if (std::strcmp("fps", key) == 0) {
          sidecar_fps = json_number_value(value);
        } else if (std::strcmp("height", key) == 0) {
          sidecar_height = json_number_value(value);
        } else if (std::strcmp("width", key) == 0) {
          sidecar_width = json_number_value(value);
        }

        iter = json_object_iter_next(scroot, iter);
      }
    } else {
      orig_filename = infile;
    }
    json_decref(scroot);
  }

  if (!orig_checksum.empty()) { json_object_set_new(root, "checksumOriginal", json_string(orig_checksum.c_str())); }
  if (!derivative_checksum.empty()) {
    json_object_set_new(root, "checksumDerivative", json_string(derivative_checksum.c_str()));
  }

  std::string actual_mimetype = shttps::Parsing::getBestFileMimetype(infile);
  if ((actual_mimetype == "image/tiff") || (actual_mimetype == "image/jpeg") || (actual_mimetype == "image/png")
      || (actual_mimetype == "image/jpx") || (actual_mimetype == "image/jp2")) {
    int width, height;
    //
    // get cache info
    //
    // std::shared_ptr<SipiCache> cache = serv->cache();

    Sipi::SipiImage tmpimg;
    Sipi::SipiImgInfo info;
    try {
      info = tmpimg.read_shape(infile);
    } catch (SipiImageError &err) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err.to_string());
      return;
    }
    if (info.success == SipiImgInfo::FAILURE) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, "Error getting image dimensions!");
      return;
    }
    width = info.width;
    height = info.height;

    json_object_set_new(root, "width", json_integer(width));
    json_object_set_new(root, "height", json_integer(height));
    if (info.numpages > 0) { json_object_set_new(root, "numpages", json_integer(info.numpages)); }
    // json_object_set_new(root, "internalMimeType", json_string(info.internalmimetype.c_str()));
    json_object_set_new(root, "internalMimeType", json_string(actual_mimetype.c_str()));
    if (info.success == SipiImgInfo::ALL) {
      json_object_set_new(root, "originalMimeType", json_string(info.origmimetype.c_str()));
      json_object_set_new(root, "originalFilename", json_string(info.origname.c_str()));
    }
    char *json_str = json_dumps(root, JSON_INDENT(3));
    conn_obj.sendAndFlush(json_str, strlen(json_str));
    free(json_str);
  } else if (actual_mimetype == "video/mp4") {
    json_object_set_new(root, "internalMimeType", json_string(actual_mimetype.c_str()));

    struct stat fstatbuf;
    if (stat(infile.c_str(), &fstatbuf) != 0) { throw Error("Cannot fstat file!"); }
    json_object_set_new(root, "fileSize", json_integer(fstatbuf.st_size));

    if (!orig_filename.empty()) { json_object_set_new(root, "originalFilename", json_string(orig_filename.c_str())); }

    if (sidecar_duration >= 0) { json_object_set_new(root, "duration", json_real(sidecar_duration)); }

    if (sidecar_fps >= 0) { json_object_set_new(root, "fps", json_real(sidecar_fps)); }

    if (sidecar_height >= 0) { json_object_set_new(root, "height", json_real(sidecar_height)); }

    if (sidecar_width >= 0) { json_object_set_new(root, "width", json_real(sidecar_width)); }

    char *json_str = json_dumps(root, JSON_INDENT(3));
    conn_obj.sendAndFlush(json_str, strlen(json_str));
    free(json_str);
  } else {
    json_object_set_new(root, "internalMimeType", json_string(actual_mimetype.c_str()));

    struct stat fstatbuf;
    if (stat(infile.c_str(), &fstatbuf) != 0) { throw Error("Cannot fstat file!"); }
    json_object_set_new(root, "fileSize", json_integer(fstatbuf.st_size));
    json_object_set_new(root, "originalFilename", json_string(orig_filename.c_str()));

    char *json_str = json_dumps(root, JSON_INDENT(3));
    conn_obj.sendAndFlush(json_str, strlen(json_str));
    free(json_str);
  }
  json_decref(root);
}

//=========================================================================

/**
 * \brief The internal function handels serving of raw files part for the iiif_handler.
 * This is an extension of the IIIF Image API, which allows for the delivery of raw files.
 * This is useful for delivering files that are not images, such as PDFs, audio, video, etc., and that
 * cannot be accesed otherwise through the IIIF Image API.
 * \param conn_obj
 * \param luaserver
 * \param serv
 * \param prefix_as_path
 * \param params
 */
// ── Connection → SipiResponse adapter (strangler parity glue; while the C++ transport still owns the socket) ───
// Presents a live shttps::Connection as the C-ABI response sink the FFI core
// (src/ffi/sipi_ffi.h) drives. Temporary: at the cutover the Rust shell
// supplies SipiResponse directly and Connection is deleted, so this stays a
// file-local helper rather than a durable abstraction. The ctx is a small
// struct (not the bare Connection) so conn_write can track that
// setChunkedTransfer has run once before the first chunk.
struct ConnResponseCtx
{
  Connection *conn;
  bool chunked_started = false;
};

static Connection *conn_of(void *ctx) { return static_cast<ConnResponseCtx *>(ctx)->conn; }

static void conn_set_status(void *ctx, int status)
{
  conn_of(ctx)->status(static_cast<Connection::StatusCodes>(status));
}

static void conn_add_header(void *ctx, const char *name, const char *value) { conn_of(ctx)->header(name, value); }

// Known-length body: Connection::sendFile sends Content-Length + streams the
// region (no whole-file buffering), the legacy /file framing. `length` is the
// byte count; sendFile takes an inclusive `to`.
static int conn_send_file(void *ctx, const char *path, uint64_t offset, uint64_t length)
{
  try {
    const size_t from = static_cast<size_t>(offset);
    const size_t to = from + static_cast<size_t>(length) - 1;
    conn_of(ctx)->sendFile(path, 8192, from, to);
    return 0;
  } catch (...) {
    return 1;// peer gone / socket error
  }
}

// Unknown-length body (the image encoder): chunked framing. setChunkedTransfer
// throws once the response header has been sent, so it must run exactly once,
// before the first chunk — hence the chunked_started flag on the ctx.
static int conn_write(void *ctx, const uint8_t *data, size_t len)
{
  try {
    auto *rctx = static_cast<ConnResponseCtx *>(ctx);
    if (!rctx->chunked_started) {
      rctx->conn->setChunkedTransfer();
      rctx->chunked_started = true;
    }
    rctx->conn->sendAndFlush(data, len);
    return 0;
  } catch (...) {
    return 1;// peer gone / socket error
  }
}

static int conn_cancelled(void *ctx) { return conn_of(ctx)->peerConnected() ? 0 : 1; }

static ::SipiResponse make_connection_response(ConnResponseCtx &ctx)
{
  return ::SipiResponse{ &ctx, conn_set_status, conn_add_header, conn_write, conn_send_file, conn_cancelled };
}

static void serve_file_download(Connection &conn_obj,
  shttps::LuaServer &luaserver,
  SipiHttpServer *serv,
  bool prefix_as_path,
  std::vector<std::string> params)
{
  // R1: Early rejection of path traversal in identifier and prefix
  if (contains_traversal(urldecode(params[iiif_identifier]))) {
    log_warn("Path traversal blocked: client=%s identifier=%s",
      conn_obj.peer_ip().c_str(), params[iiif_identifier].c_str());
    send_error(conn_obj, Connection::BAD_REQUEST, "Invalid IIIF identifier");
    return;
  }
  if (prefix_as_path && contains_traversal(urldecode(params[iiif_prefix]))) {
    log_warn("Path traversal blocked in prefix: client=%s prefix=%s",
      conn_obj.peer_ip().c_str(), params[iiif_prefix].c_str());
    send_error(conn_obj, Connection::BAD_REQUEST, "Invalid IIIF identifier");
    return;
  }

  std::string requested_file;
  if (prefix_as_path && (!params[iiif_prefix].empty())) {
    requested_file = serv->imgroot() + "/" + urldecode(params[iiif_prefix]) + "/" + urldecode(params[iiif_identifier]);
  } else {
    requested_file = serv->imgroot() + "/" + urldecode(params[iiif_identifier]);
  }
  if (luaserver.luaFunctionExists(file_preflight_funcname)) {
    std::unordered_map<std::string, std::string> pre_flight_info;
    try {
      pre_flight_info = call_file_preflight(conn_obj, requested_file);
    } catch (SipiError &err) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err);
      return;
    }
    if (pre_flight_info["type"] == "allow") {
      requested_file = pre_flight_info["infile"];
    } else if (pre_flight_info["type"] == "restrict") {
      requested_file = pre_flight_info["infile"];
    } else {
      send_error(conn_obj, Connection::UNAUTHORIZED, "Unauthorized access");
      return;
    }
  }

  // R2: Validate resolved path is within imgroot
  auto validated = validate_resolved_path(requested_file, serv->resolved_imgroot());
  if (validated.status == PathValidation::TRAVERSAL) {
    log_warn("Path traversal blocked (realpath): client=%s identifier=%s",
      conn_obj.peer_ip().c_str(), params[iiif_identifier].c_str());
    send_error(conn_obj, Connection::BAD_REQUEST, "Invalid IIIF identifier");
    return;
  }
  if (validated.status == PathValidation::OK) {
    requested_file = validated.resolved_path;
  }

  // The raw byte passthrough (stat, MIME, Range/206 parsing) lives behind the
  // FFI core (sipi_serve_file), which delegates byte delivery back to the
  // transport via send_file. The Rust shell drives the same seam.
  // Content-Disposition derives from the IIIF identifier (an edge/input concern
  // with R8/R9 sanitization), so it stays caller-side and is set only on a Range
  // request — matching the legacy 206 path.
  std::string range = conn_obj.header("range");
  if (!range.empty()) {
    auto safe_name = sanitize_header_value(urldecode(params[iiif_identifier]));
    if (is_ascii(safe_name)) {
      auto quoted = escape_quoted_string(safe_name);
      conn_obj.header("Content-Disposition", "inline; filename=\"" + quoted + "\"");
    } else {
      conn_obj.header("Content-Disposition", "inline; filename*=UTF-8''" + percent_encode_rfc6266(safe_name));
    }
  }

  ConnResponseCtx rctx{ &conn_obj };
  ::SipiResponse resp = make_connection_response(rctx);
  const int rc = ::sipi_serve_file(requested_file.c_str(), range.empty() ? nullptr : range.c_str(), &resp);
  if (rc != 0) {
    if (rc == 404) { log_warn("GET: %s not accessible", requested_file.c_str()); }
    send_error(conn_obj, static_cast<Connection::StatusCodes>(rc));
    return;
  }
  conn_obj.flush();
}

/**
 * @brief The internal function handels the serving of IIIFs part for the iiif_handler.
 * This function gets the parameters from the request, calls the preflight function, which checks for permissions,
 * deals with watermarks and size restrictions, gets the mimetype of the file, gets the cache info, gets the image
 * and sends the image to the client
 * @param conn_obj
 * @param luaserver
 * @param server
 * @param prefix_as_path
 * @param uri the raw URI from the request.
 * @param params the parsed parameters from the URI.
 */
static void serve_iiif(Connection &conn_obj,
  shttps::LuaServer &luaserver,
  SipiHttpServer *server,
  bool prefix_as_path,
  const std::string &uri,
  std::vector<std::string> params)
{
  SIPI_ZONE_N("serve_iiif");

  // getting the identifier (which in case of a PDF or multipage TIFF may contain
  // a page id: identifier@pagenum)
  SipiIdentifier sid = urldecode(params[iiif_identifier]);

  // R1: Early rejection of path traversal in identifier and prefix
  if (contains_traversal(sid.getIdentifier())) {
    log_warn("Path traversal blocked: client=%s identifier=%s",
      conn_obj.peer_ip().c_str(),
      params[iiif_identifier].c_str());
    send_error(conn_obj, Connection::BAD_REQUEST, "Invalid IIIF identifier");
    return;
  }
  if (prefix_as_path && contains_traversal(urldecode(params[iiif_prefix]))) {
    log_warn("Path traversal blocked in prefix: client=%s prefix=%s",
      conn_obj.peer_ip().c_str(),
      params[iiif_prefix].c_str());
    send_error(conn_obj, Connection::BAD_REQUEST, "Invalid IIIF identifier");
    return;
  }

  // Parse + validate the IIIF parameters. A syntax error is a 400 here, before
  // the engine sees the request; the parsed objects are flattened into the FFI
  // request below, and sipi_serve_image reconstructs them from the typed fields.
  auto region = std::make_shared<SipiRegion>();
  auto size = std::make_shared<SipiSize>();
  SipiRotation rotation;
  SipiQualityFormat quality_format;
  try {
    region = std::make_shared<SipiRegion>(params[iiif_region]);
    size = std::make_shared<SipiSize>(params[iiif_size]);
    rotation = SipiRotation(params[iiif_rotation]);
    quality_format = SipiQualityFormat(params[iiif_qualityformat]);
  } catch (Sipi::SipiError &err) {
    send_error(conn_obj, Connection::BAD_REQUEST, err);
    return;
  }

  // Preflight (Lua) — resolves the on-disk file and any restrict size/watermark.
  // Stays inline in the C++ server (the Lua preflight call hasn't moved behind
  // the FFI seam yet); only the decode pipeline moves behind sipi_serve_image.
  std::string infile;// path to the input file on the server
  std::string watermark;// watermark file path, or empty
  std::string restricted_size_str;// restrict downscale spec, or empty

  if (luaserver.luaFunctionExists(iiif_preflight_funcname)) {
    std::unordered_map<std::string, std::string> pre_flight_info;
    try {
      pre_flight_info = call_iiif_preflight(conn_obj, params[iiif_prefix], sid.getIdentifier());
    } catch (SipiError &err) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err);
      return;
    }
    infile = pre_flight_info["infile"];

    if (pre_flight_info["type"] != "allow") {
      if (pre_flight_info["type"] == "restrict") {
        bool ok = false;
        try {
          watermark = pre_flight_info.at("watermark");
          ok = true;
        } catch (const std::out_of_range &err) {
          ;// do nothing, no watermark...
        }
        try {
          restricted_size_str = pre_flight_info.at("size");
          ok = true;
        } catch (const std::out_of_range &err) {
          ;// do nothing, no size restriction
        }
        if (!ok) {
          send_error(conn_obj, Connection::UNAUTHORIZED, "Unauthorized access");
          return;
        }
      } else {
        send_error(conn_obj, Connection::UNAUTHORIZED, "Unauthorized access");
        return;
      }
    }
  } else {
    if (prefix_as_path && (!params[iiif_prefix].empty())) {
      infile = server->imgroot() + "/" + params[iiif_prefix] + "/" + sid.getIdentifier();
    } else {
      infile = server->imgroot() + "/" + sid.getIdentifier();
    }
  }

  // R2: Validate resolved path is within imgroot
  auto validated = validate_resolved_path(infile, server->resolved_imgroot());
  if (validated.status == PathValidation::TRAVERSAL) {
    log_warn("Path traversal blocked (realpath): client=%s identifier=%s",
      conn_obj.peer_ip().c_str(),
      params[iiif_identifier].c_str());
    send_error(conn_obj, Connection::BAD_REQUEST, "Invalid IIIF identifier");
    return;
  }
  if (validated.status == PathValidation::OK) { infile = validated.resolved_path; }

  // Flatten the parsed params into the typed FFI seam (the enum values mirror
  // the iiifparser enums 1:1). The decode/transform/encode pipeline,
  // admission (cache/rate-limit/budget), and canonical-URL building all live
  // behind sipi_serve_image now.
  ::SipiIiifParams p{};
  p.region_type = static_cast<SipiRegionType>(region->getType());
  region->get_coords(p.region[0], p.region[1], p.region[2], p.region[3]);
  p.size_type = static_cast<SipiSizeType>(size->getType());
  {
    bool upscaling = false;
    float percent = 0.F;
    int reduce = 0;
    size_t nx = 0, ny = 0;
    size->get_params(upscaling, percent, reduce, nx, ny);
    p.size_upscaling = upscaling ? 1 : 0;
    p.size_percent = percent;
    p.size_reduce = reduce;
    p.size_nx = nx;
    p.size_ny = ny;
  }
  {
    float angle = 0.F;
    const bool mirror = rotation.get_rotation(angle);
    p.rotation = angle;
    p.rotation_mirror = mirror ? 1 : 0;
  }
  p.quality_type = static_cast<SipiQualityType>(quality_format.quality());
  p.format_type = static_cast<SipiFormatType>(quality_format.format());

  const std::string client_id = resolve_client_id(conn_obj);
  const std::string host = conn_obj.host();

  ::SipiServeRequest req{};
  req.resolved_path = infile.c_str();
  req.prefix = params[iiif_prefix].c_str();
  req.identifier = params[iiif_identifier].c_str();
  req.client_ip = client_id.c_str();
  req.params = p;
  req.restricted_size = restricted_size_str.empty() ? nullptr : restricted_size_str.c_str();
  req.watermark_path = watermark.empty() ? nullptr : watermark.c_str();
  req.forwarded_proto = nullptr;
  req.forwarded_host = host.c_str();
  req.request_uri = uri.c_str();
  req.is_head = (conn_obj.method() == Connection::HEAD) ? 1 : 0;

  ConnResponseCtx rctx{ &conn_obj };
  ::SipiResponse resp = make_connection_response(rctx);
  const int rc = ::sipi_serve_image(&req, &resp);
  if (rc == 499) {
    // SipiStatus::ClientGone — the client vanished mid-decode, the engine already
    // counted it, and nothing was committed. Render no error.
    return;
  }
  if (rc != 0) {
    if (rc == 404) { log_warn("GET: %s not accessible", infile.c_str()); }
    send_error(conn_obj, static_cast<Connection::StatusCodes>(rc));
    return;
  }
  conn_obj.flush();
}

/**
 * \brief The iiif_handler function is the main entry point for the IIIF route.
 * It parses the URI and calls one of the appropriate functionss (serve_iiif, serve_info, serve_knora_info, ) to handle
 * the request. \param conn_obj \param luaserver \param user_data \param dummy
 */
static void iiif_handler(Connection &conn_obj, shttps::LuaServer &luaserver, void *user_data, void *dummy)
{
  SIPI_ZONE_N("iiif_handler");
  auto *serv = static_cast<SipiHttpServer *>(user_data);
  const bool prefix_as_path = serv->prefix_as_path();
  const auto uri = conn_obj.uri();// has form "/pre/fix/es.../BAU_1_000441077_2_1.j2k/full/,1000/0/default.jpg"

  std::vector<std::string> params{};
  auto request_type{ handlers::iiif_handler::UNDEFINED };

  {
    SIPI_ZONE_N("parse_iiif_uri");
    if (auto parse_url_result = handlers::iiif_handler::parse_iiif_uri(uri); parse_url_result.has_value()) {
      params = parse_url_result.value().params;
      request_type = parse_url_result.value().request_type;
    } else {
      send_error(conn_obj, Connection::BAD_REQUEST, parse_url_result.error());
      return;
    }
  }

  switch (request_type) {
  case handlers::iiif_handler::IIIF: {
    serve_iiif(conn_obj, luaserver, serv, prefix_as_path, uri, params);
    return;
  }
  case handlers::iiif_handler::INFO_JSON: {
    serve_info_json_file(conn_obj, serv, luaserver, params, prefix_as_path);
    return;
  }
  case handlers::iiif_handler::KNORA_JSON: {
    serve_knora_json_file(conn_obj, serv, luaserver, params, prefix_as_path);
    return;
  }
  case handlers::iiif_handler::REDIRECT: {
    serve_redirect(conn_obj, params);
    return;
  }
  case handlers::iiif_handler::FILE_DOWNLOAD: {
    serve_file_download(conn_obj, luaserver, serv, prefix_as_path, params);
    return;
  }
  case handlers::iiif_handler::UNDEFINED: {
    send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, "Unknown internal error!");
    return;
  }
  }
}

//=========================================================================

// R31-R33: Health endpoint handler
static void health_handler(Connection &conn_obj, shttps::LuaServer &, void *user_data, void *)
{
  auto *server = static_cast<SipiHttpServer *>(user_data);
  auto uptime = std::chrono::steady_clock::now() - server->start_time();
  auto uptime_sec = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();

  std::string json = R"({"status":"ok","version":")" + std::string(VERSION)
                   + R"(","uptime_seconds":)" + std::to_string(uptime_sec) + "}";

  conn_obj.status(Connection::OK);
  conn_obj.header("Content-Type", "application/json");
  conn_obj.setBuffer();
  conn_obj << json;
  conn_obj.flush();
}

//=========================================================================

static void metrics_handler(Connection &conn_obj, shttps::LuaServer &luaserver, void *user_data, void *dummy)
{
  std::string body = Metrics::instance().serialize();
  conn_obj.status(Connection::OK);
  conn_obj.header("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
  conn_obj.send(body.data(), body.size());
}

//=========================================================================

static void favicon_handler(Connection &conn_obj, shttps::LuaServer &luaserver, void *user_data, void *dummy)
{
  conn_obj.status(Connection::OK);
  conn_obj.header("Content-Type", "image/x-icon");
  conn_obj.send(favicon_ico, favicon_ico_len);
}

//=========================================================================

SipiHttpServer::SipiHttpServer(int port_p,
  const size_t nthreads_p,
  const std::string userid_str,
  const std::string &logfile_p,
  const std::string &loglevel_p)
  : Server::Server(port_p, nthreads_p, userid_str, logfile_p, loglevel_p)
{
  _cache = nullptr;
  _scaling_quality = { ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH };
}

//=========================================================================

void SipiHttpServer::cache(const std::string &cachedir_p,
  long long max_cache_size_p,
  unsigned max_nfiles_p)
{
  try {
    _cache = std::make_shared<SipiCache>(cachedir_p, max_cache_size_p, max_nfiles_p);
  } catch (const SipiError &err) {
    _cache = nullptr;
    log_warn("Couldn't open cache directory %s: %s", cachedir_p.c_str(), err.to_string().c_str());
  }
}

//=========================================================================

// here we add the main IIIF route to the server (iiif_handler)
void SipiHttpServer::run()
{
  log_info("SipiHttpServer starting ...");
  //
  // setting the image root — resolve via realpath() for path traversal prevention (R2)
  //
  char resolved_root[PATH_MAX];
  if (realpath(_imgroot.c_str(), resolved_root) == nullptr) {
    log_err("Cannot resolve imgroot path: %s", _imgroot.c_str());
    throw SipiError("Cannot resolve imgroot path: " + _imgroot);
  }
  _resolved_imgroot = std::string(resolved_root);

  log_info("Serving images from %s (resolved: %s)", _imgroot.c_str(), _resolved_imgroot.c_str());

  _start_time = std::chrono::steady_clock::now();

  add_route(Connection::GET, "/health", health_handler);
  add_route(Connection::GET, "/metrics", metrics_handler);
  add_route(Connection::GET, "/favicon.ico", favicon_handler);
  add_route(Connection::GET, "/", iiif_handler);
  add_route(Connection::HEAD, "/", iiif_handler);

  if (!_wwwroute.empty() && !_docroot.empty()) {
    _filehandler_info = { _wwwroute, _docroot };
    add_route(Connection::GET, _wwwroute, shttps::file_handler, &_filehandler_info);
    add_route(Connection::POST, _wwwroute, shttps::file_handler, &_filehandler_info);
  }

  user_data(this);

  // Install the engine context the FFI image pipeline (sipi_serve_image) reads.
  // While this server still owns cache/rate-limiter/memory-budget, it hands
  // the FFI non-owning pointers + the config knobs. At the cutover
  // sipi_init takes over and this install (and SipiHttpServer) go away.
  Sipi::ffi::set_engine_context(Sipi::ffi::EngineContext{
    .cache = _cache.get(),
    .rate_limiter = _rate_limiter.get(),
    .memory_budget = _memory_budget.get(),
    .imgroot = _imgroot,
    .resolved_imgroot = _resolved_imgroot,
    .jpeg_quality = _jpeg_quality,
    .scaling_quality = _scaling_quality,
    .max_pixel_limit = _max_pixel_limit,
  });

  // in shttps::Server::run(), add additional routes are added, namely the ones for the LUA scripts
  Server::run();
}

//=========================================================================
}// namespace Sipi
