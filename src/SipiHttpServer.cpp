/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "shttps/Connection.h"
#include "shttps/LuaServer.h"
#include "shttps/Parsing.h"

#include "SipiError.hpp"
#include "SipiImage.hpp"
#include "SipiImageError.hpp"
#include "iiifparser/SipiIdentifier.h"
#include "iiifparser/SipiQualityFormat.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiRotation.h"
#include "iiifparser/SipiSize.h"

#include "Logger.h"
#include "SipiHttpServer.hpp"
#include "favicon.h"
#include "handlers/iiif_handler.hpp"
#include "jansson.h"

using namespace shttps;

namespace Sipi {
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
 * Gets the IIIF prefix, IIIF identifier, and cookie from the HTTP request, and passes them to the Lua pre-flight
 * function (whose name is given by the constant pre_flight_func_name).
 *
 * Returns the return values of the pre-flight function as a std::pair containing a permission string and (optionally) a
 * file path. Throws SipiError if an error occurs.
 *
 * \param conn_obj the server connection.
 * \param luaserver the Lua server that will be used to call the function.
 * \param prefix the IIIF prefix.
 * \param identifier the IIIF identifier.
 * \return Pair of permission string and filepath
 */
static std::unordered_map<std::string, std::string> call_iiif_preflight(Connection &conn_obj,
  shttps::LuaServer &luaserver,
  const std::string &prefix,
  const std::string &identifier)
{
  // The permission and optional file path that the pre_fight function returns.
  std::unordered_map<std::string, std::string> preflight_info;
  // std::string permission;
  // std::string infile;

  // The paramters to be passed to the pre-flight function.
  std::vector<std::shared_ptr<LuaValstruct>> lvals;

  // The first parameter is the IIIF prefix.
  std::shared_ptr<LuaValstruct> iiif_prefix_param = std::make_shared<LuaValstruct>();
  iiif_prefix_param->type = LuaValstruct::STRING_TYPE;
  iiif_prefix_param->value.s = prefix;
  lvals.push_back(iiif_prefix_param);

  // The second parameter is the IIIF identifier.
  std::shared_ptr<LuaValstruct> iiif_identifier_param = std::make_shared<LuaValstruct>();
  iiif_identifier_param->type = LuaValstruct::STRING_TYPE;
  iiif_identifier_param->value.s = identifier;
  lvals.push_back(iiif_identifier_param);

  // The third parameter is the HTTP cookie.
  std::shared_ptr<LuaValstruct> cookie_param = std::make_shared<LuaValstruct>();
  std::string cookie = conn_obj.header("cookie");
  cookie_param->type = LuaValstruct::STRING_TYPE;
  cookie_param->value.s = cookie;
  lvals.push_back(cookie_param);

  // Call the pre-flight function.
  std::vector<std::shared_ptr<LuaValstruct>> rvals = luaserver.executeLuafunction(iiif_preflight_funcname, lvals);

  // If it returned nothing, that's an error.
  if (rvals.empty()) {
    std::ostringstream err_msg;
    err_msg << "Lua function " << iiif_preflight_funcname << " must return at least one value";
    throw SipiError(err_msg.str());
  }

  // The first return value is the permission code.
  auto permission_return_val = rvals.at(0);

  // The permission code can be a string or a table
  if (permission_return_val->type == LuaValstruct::STRING_TYPE) {
    preflight_info["type"] = permission_return_val->value.s;
  } else if (permission_return_val->type == LuaValstruct::TABLE_TYPE) {
    std::shared_ptr<LuaValstruct> tmpv;
    try {
      tmpv = permission_return_val->value.table.at("type");
    } catch (const std::out_of_range &err) {
      std::ostringstream err_msg;
      err_msg << "The permission value returned by Lua function " << iiif_preflight_funcname << " has no type field!";
      throw SipiError(err_msg.str());
    }
    if (tmpv->type != LuaValstruct::STRING_TYPE) { throw SipiError("String value expected!"); }
    preflight_info["type"] = tmpv->value.s;
    for (const auto &keyval : permission_return_val->value.table) {
      if (keyval.first == "type") continue;
      if (keyval.second->type != LuaValstruct::STRING_TYPE) { throw SipiError("String value expected!"); }
      preflight_info[keyval.first] = keyval.second->value.s;
    }
  } else {
    std::ostringstream err_msg;
    err_msg << "The permission value returned by Lua function " << iiif_preflight_funcname << " was not valid";
    throw SipiError(err_msg.str());
  }

  //
  // check if permission type is valid
  //
  if ((preflight_info["type"] != "allow") && (preflight_info["type"] != "login")
      && (preflight_info["type"] != "clickthrough") && (preflight_info["type"] != "kiosk")
      && (preflight_info["type"] != "external") && (preflight_info["type"] != "restrict")
      && (preflight_info["type"] != "deny")) {
    std::ostringstream err_msg;
    err_msg << "The permission returned by Lua function " << iiif_preflight_funcname
            << " is not valid: " << preflight_info["type"];
    throw SipiError(err_msg.str());
  }

  if (preflight_info["type"] == "deny") {
    preflight_info["infile"] = "";
  } else {
    if (rvals.size() < 2) {
      std::ostringstream err_msg;
      err_msg << "Lua function " << iiif_preflight_funcname
              << " returned other permission than 'deny', but it did not return a file path";
      throw SipiError(err_msg.str());
    }

    auto infile_return_val = rvals.at(1);

    // The file path must be a string.
    if (infile_return_val->type == LuaValstruct::STRING_TYPE) {
      preflight_info["infile"] = infile_return_val->value.s;
    } else {
      std::ostringstream err_msg;
      err_msg << "The file path returned by Lua function " << iiif_preflight_funcname << " was not a string";
      throw SipiError(err_msg.str());
    }
  }

  // Return the permission code and file path, if any, as a std::pair.
  return preflight_info;
}

//=========================================================================

static std::unordered_map<std::string, std::string>
  call_file_preflight(Connection &conn_obj, shttps::LuaServer &luaserver, const std::string &filepath)
{
  // The permission and optional file path that the pre_fight function returns.
  std::unordered_map<std::string, std::string> preflight_info;
  // std::string permission;
  // std::string infile;

  // The paramters to be passed to the pre-flight function.
  std::vector<std::shared_ptr<LuaValstruct>> lvals;

  // The first parameter is the filepath.
  std::shared_ptr<LuaValstruct> file_path_param = std::make_shared<LuaValstruct>();
  file_path_param->type = LuaValstruct::STRING_TYPE;
  file_path_param->value.s = filepath;
  lvals.push_back(file_path_param);

  // The second parameter is the HTTP cookie.
  std::shared_ptr<LuaValstruct> cookie_param = std::make_shared<LuaValstruct>();
  std::string cookie = conn_obj.header("cookie");
  cookie_param->type = LuaValstruct::STRING_TYPE;
  cookie_param->value.s = cookie;
  lvals.push_back(cookie_param);

  // Call the pre-flight function.
  std::vector<std::shared_ptr<LuaValstruct>> rvals = luaserver.executeLuafunction(file_preflight_funcname, lvals);

  // If it returned nothing, that's an error.
  if (rvals.empty()) {
    std::ostringstream err_msg;
    err_msg << "Lua function " << file_preflight_funcname << " must return at least one value";
    throw SipiError(err_msg.str());
  }

  // The first return value is the permission code.
  auto permission_return_val = rvals.at(0);

  // The permission code must be a string or a table.
  if (permission_return_val->type == LuaValstruct::STRING_TYPE) {
    preflight_info["type"] = permission_return_val->value.s;
  } else if (permission_return_val->type == LuaValstruct::TABLE_TYPE) {
    std::shared_ptr<LuaValstruct> tmpv;
    try {
      tmpv = permission_return_val->value.table.at("type");
    } catch (const std::out_of_range &err) {
      std::ostringstream err_msg;
      err_msg << "The permission value returned by Lua function " << file_preflight_funcname << " has no type field!";
      throw SipiError(err_msg.str());
    }
    if (tmpv->type != LuaValstruct::STRING_TYPE) { throw SipiError("String value expected!"); }
    preflight_info["type"] = tmpv->value.s;
    for (const auto &keyval : permission_return_val->value.table) {
      if (keyval.first == "type") continue;
      if (keyval.second->type != LuaValstruct::STRING_TYPE) { throw SipiError("String value expected!"); }
      preflight_info[keyval.first] = keyval.second->value.s;
    }
  } else {
    std::ostringstream err_msg;
    err_msg << "The permission value returned by Lua function " << file_preflight_funcname << " was not valid";
    throw SipiError(err_msg.str());
  }

  //
  // check if permission type is valid
  //
  if ((preflight_info["type"] != "allow") && (preflight_info["type"] != "login")
      && (preflight_info["type"] != "restrict") && (preflight_info["type"] != "deny")) {
    std::ostringstream err_msg;
    err_msg << "The permission returned by Lua function " << file_preflight_funcname
            << " is not valid: " << preflight_info["type"];
    throw SipiError(err_msg.str());
  }

  if (preflight_info["type"] == "deny") {
    preflight_info["infile"] = "";
  } else {
    if (rvals.size() < 2) {
      std::ostringstream err_msg;
      err_msg << "Lua function " << file_preflight_funcname
              << " returned other permission than 'deny', but it did not return a file path";
      throw SipiError(err_msg.str());
    }

    auto infile_return_val = rvals.at(1);

    // The file path must be a string.
    if (infile_return_val->type == LuaValstruct::STRING_TYPE) {
      preflight_info["infile"] = infile_return_val->value.s;
    } else {
      std::ostringstream err_msg;
      err_msg << "The file path returned by Lua function " << file_preflight_funcname << " was not a string";
      throw SipiError(err_msg.str());
    }
  }

  // Return the permission code and file path, if any, as a std::pair.
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
  std::unordered_map<std::string, std::string> pre_flight_info;
  if (luaserver.luaFunctionExists(iiif_preflight_funcname)) {
    pre_flight_info = call_iiif_preflight(conn_obj,
      luaserver,
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
  //
  // test if we have access to the file
  //
  if (access(infile.c_str(), R_OK) != 0) {
    // test, if file exists
    throw SipiError("Cannot read image file: " + infile);
  }
  pre_flight_info["infile"] = infile;
  return pre_flight_info;
}

//=========================================================================

std::pair<std::string, std::string> SipiHttpServer::get_canonical_url(size_t tmp_w,
  size_t tmp_h,
  const std::string &host,
  const std::string &prefix,
  const std::string &identifier,
  std::shared_ptr<SipiRegion> region,
  std::shared_ptr<SipiSize> size,
  SipiRotation &rotation,
  SipiQualityFormat &quality_format,
  int pagenum,
  const std::string &cannonical_watermark)
{
  static constexpr int canonical_len = 127;

  char canonical_region[canonical_len + 1];
  char canonical_size[canonical_len + 1];

  int tmp_r_x = 0, tmp_r_y = 0, tmp_red = 0;
  size_t tmp_r_w = 0, tmp_r_h = 0;
  bool tmp_ro = false;

  if (region->getType() != SipiRegion::FULL) { region->crop_coords(tmp_w, tmp_h, tmp_r_x, tmp_r_y, tmp_r_w, tmp_r_h); }

  region->canonical(canonical_region, canonical_len);

  if (size->getType() != SipiSize::FULL) {
    try {
      size->get_size(tmp_w, tmp_h, tmp_r_w, tmp_r_h, tmp_red, tmp_ro);
    } catch (Sipi::SipiSizeError &err) {
      throw SipiError("SipiSize error!");
    }
  }

  size->canonical(canonical_size, canonical_len);
  float angle;
  const bool mirror = rotation.get_rotation(angle);
  char canonical_rotation[canonical_len + 1];

  if (mirror || (angle != 0.0)) {
    if ((angle - floorf(angle)) < 1.0e-6) {
      // it's an integer
      if (mirror) {
        (void)snprintf(canonical_rotation, canonical_len, "!%ld", std::lround(angle));
      } else {
        (void)snprintf(canonical_rotation, canonical_len, "%ld", std::lround(angle));
      }
    } else {
      if (mirror) {
        (void)snprintf(canonical_rotation, canonical_len, "!%1.1f", angle);
      } else {
        (void)snprintf(canonical_rotation, canonical_len, "%1.1f", angle);
      }
    }
  } else {
    (void)snprintf(canonical_rotation, canonical_len, "0");
  }

  constexpr unsigned canonical_header_len = 511;
  char canonical_header[canonical_header_len + 1];
  char ext[5];

  switch (quality_format.format()) {
  case SipiQualityFormat::JPG: {
    ext[0] = 'j';
    ext[1] = 'p';
    ext[2] = 'g';
    ext[3] = '\0';
    break;// jpg
  }
  case SipiQualityFormat::JP2: {
    ext[0] = 'j';
    ext[1] = 'p';
    ext[2] = '2';
    ext[3] = '\0';
    break;// jp2
  }
  case SipiQualityFormat::TIF: {
    ext[0] = 't';
    ext[1] = 'i';
    ext[2] = 'f';
    ext[3] = '\0';
    break;// tif
  }
  case SipiQualityFormat::PNG: {
    ext[0] = 'p';
    ext[1] = 'n';
    ext[2] = 'g';
    ext[3] = '\0';
    break;// png
  }
  default: {
    throw SipiError("Unsupported file format requested! Supported are .jpg, .jp2, .tif, .png");
  }
  }

  std::string format;
  if (quality_format.quality() != SipiQualityFormat::DEFAULT) {
    switch (quality_format.quality()) {
    case SipiQualityFormat::COLOR: {
      format = "/color.";
      break;
    }
    case SipiQualityFormat::GRAY: {
      format = "/gray.";
      break;
    }
    case SipiQualityFormat::BITONAL: {
      format = "/bitonal.";
      break;
    }
    default: {
      format = "/default.";
    }
    }
  } else {
    format = "/default.";
  }

  std::string fullid = identifier;
  if (pagenum > 0) fullid += "@" + std::to_string(pagenum);
  (void)snprintf(canonical_header,
    canonical_header_len,
    "<http://%s/%s/%s/%s/%s/%s/default.%s/%s>;rel=\"canonical\"",
    host.c_str(),
    prefix.c_str(),
    fullid.c_str(),
    canonical_region,
    canonical_size,
    canonical_rotation,
    ext,
    cannonical_watermark.c_str());

  // Here we are creating the canonical URL. Attention: We have added the watermark to the URL, which is not part of the
  // IIIF standard. This is necessary for correct caching, as the watermark is not part of the image, but is added
  // by the server.
  std::string canonical = host + "/" + prefix + "/" + fullid + "/" + std::string(canonical_region) + "/"
                          + std::string(canonical_size) + "/" + std::string(canonical_rotation) + format
                          + std::string{ ext } + "/" + std::string{ cannonical_watermark };

  return make_pair(std::string(canonical_header), canonical);
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

  conn_obj.header("Location", redirect);
  conn_obj.header("Content-Type", "text/plain");
  conn_obj << "Redirect to " << redirect;
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

    //
    // get cache info
    //
    std::shared_ptr<SipiCache> cache = serv->cache();
    if ((cache == nullptr) || !cache->getSize(access["infile"], width, height, t_width, t_height, clevels, pagenum)) {
      Sipi::SipiImage tmpimg;
      Sipi::SipiImgInfo info;
      try {
        info = tmpimg.getDim(access["infile"]);
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
    }

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
      info = tmpimg.getDim(infile);
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
static void serve_file_download(Connection &conn_obj,
  shttps::LuaServer &luaserver,
  SipiHttpServer *serv,
  bool prefix_as_path,
  std::vector<std::string> params)
{
  std::string requested_file;
  if (prefix_as_path && (!params[iiif_prefix].empty())) {
    requested_file = serv->imgroot() + "/" + urldecode(params[iiif_prefix]) + "/" + urldecode(params[iiif_identifier]);
  } else {
    requested_file = serv->imgroot() + "/" + urldecode(params[iiif_identifier]);
  }
  if (luaserver.luaFunctionExists(file_preflight_funcname)) {
    std::unordered_map<std::string, std::string> pre_flight_info;
    try {
      pre_flight_info = call_file_preflight(conn_obj, luaserver, requested_file);
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
  if (access(requested_file.c_str(), R_OK) == 0) {
    std::string actual_mimetype = shttps::Parsing::getBestFileMimetype(requested_file);
    //
    // first we get the filesize and time using fstat
    //
    struct stat fstatbuf
    {
    };

    if (stat(requested_file.c_str(), &fstatbuf) != 0) {
      log_err("Cannot fstat file %s ", requested_file.c_str());
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR);
    }
    size_t fsize = fstatbuf.st_size;
#ifdef __APPLE__
    struct timespec rawtime = fstatbuf.st_mtimespec;
#else
    struct timespec rawtime = fstatbuf.st_mtim;
#endif
    char timebuf[100];
    std::strftime(timebuf, sizeof timebuf, "%a, %d %b %Y %H:%M:%S %Z", std::gmtime(&rawtime.tv_sec));

    std::string range = conn_obj.header("range");
    if (range.empty()) {
      // no "Content-Length" since send_file() will add this
      conn_obj.header("Content-Type", actual_mimetype);
      conn_obj.header("Cache-Control", "public, must-revalidate, max-age=0");
      conn_obj.header("Pragma", "no-cache");
      conn_obj.header("Accept-Ranges", "bytes");
      conn_obj.header("Last-Modified", timebuf);
      conn_obj.header("Content-Transfer-Encoding: binary");
      conn_obj.sendFile(requested_file);
    } else {
      //
      // now we parse the range
      //
      std::regex re(R"(bytes=\s*(\d+)-(\d*)[\D.*]?)");
      std::cmatch m;
      size_t start = 0;// lets assume beginning of file
      size_t end = fsize - 1;// lets assume whole file
      if (std::regex_match(range.c_str(), m, re)) {
        if (m.size() < 2) { throw Error("Range expression invalid!"); }
        start = std::stoull(m[1]);
        if ((m.size() > 1) && !m[2].str().empty()) { end = std::stoull(m[2]); }
      } else {
        throw Error("Range expression invalid!");
      }

      // no "Content-Length" since send_file() will add this
      conn_obj.status(Connection::PARTIAL_CONTENT);
      conn_obj.header("Content-Type", actual_mimetype);
      conn_obj.header("Cache-Control", "public, must-revalidate, max-age=0");
      conn_obj.header("Pragma", "no-cache");
      conn_obj.header("Accept-Ranges", "bytes");
      std::stringstream ss;
      ss << "bytes " << start << "-" << end << "/" << fsize;
      conn_obj.header("Content-Range", ss.str());
      conn_obj.header("Content-Disposition", std::string("inline; filename=") + urldecode(params[iiif_identifier]));
      conn_obj.header("Content-Transfer-Encoding: binary");
      conn_obj.header("Last-Modified", timebuf);
      conn_obj.sendFile(requested_file, 8192, start, end);
    }
    conn_obj.flush();
  } else {
    log_warn("GET: %s not accessible", requested_file.c_str());
    send_error(conn_obj, Connection::NOT_FOUND);
    conn_obj.flush();
  }
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
  auto not_head_request = conn_obj.method() != Connection::HEAD;
  //
  // getting the identifier (which in case of a PDF or multipage TIFF my contain a page id (identifier@pagenum)
  //
  SipiIdentifier sid = urldecode(params[iiif_identifier]);

  //
  // getting IIIF parameters
  //
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

  //
  // here we start the lua script which checks for permissions
  //
  std::string infile;// path to the input file on the server
  std::string watermark;// path to watermark file, or empty, if no watermark required
  auto restricted_size = std::make_shared<SipiSize>();// size of restricted image. (SizeType::FULL if unrestricted)

  if (luaserver.luaFunctionExists(iiif_preflight_funcname)) {
    std::unordered_map<std::string, std::string> pre_flight_info;
    try {
      pre_flight_info = call_iiif_preflight(conn_obj, luaserver, params[iiif_prefix], sid.getIdentifier());
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
          std::string raw_size_str = pre_flight_info.at("size");
          restricted_size = std::make_shared<SipiSize>(raw_size_str);
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

  //
  // determine the mimetype of the file in the SIPI repo
  //
  SipiQualityFormat::FormatType in_format = SipiQualityFormat::UNSUPPORTED;

  std::string actual_mimetype = shttps::Parsing::getFileMimetype(infile).first;
  if (actual_mimetype == "image/tiff") in_format = SipiQualityFormat::TIF;
  if (actual_mimetype == "image/jpeg") in_format = SipiQualityFormat::JPG;
  if (actual_mimetype == "image/png") in_format = SipiQualityFormat::PNG;
  if ((actual_mimetype == "image/jpx") || (actual_mimetype == "image/jp2")) in_format = SipiQualityFormat::JP2;

  if (access(infile.c_str(), R_OK) != 0) {
    // test, if file exists
    log_info("File %s not found", infile.c_str());
    send_error(conn_obj, Connection::NOT_FOUND);
    return;
  }

  float angle;
  bool mirror = rotation.get_rotation(angle);

  //
  // get cache info
  //
  std::shared_ptr<SipiCache> cache = server->cache();
  size_t img_w = 0, img_h = 0;
  size_t tile_w = 0, tile_h = 0;
  int clevels = 0;
  int numpages = 0;

  //
  // get image dimensions by accessing the file, needed for get_canonical...
  //
  if ((cache == nullptr) || !cache->getSize(infile, img_w, img_h, tile_w, tile_h, clevels, numpages)) {
    Sipi::SipiImgInfo info;
    try {
      Sipi::SipiImage img;
      info = img.getDim(infile);
    } catch (SipiImageError &err) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err.to_string());
      return;
    }
    if (info.success == SipiImgInfo::FAILURE) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, "Couldn't get image dimensions!");
      return;
    }
    img_w = info.width;
    img_h = info.height;
    tile_w = info.tile_width;
    tile_h = info.tile_height;
    clevels = info.clevels;
    numpages = info.numpages;
  }

  size_t tmp_r_w{ 0L }, tmp_r_h{ 0L };
  int tmp_red{ 0 };
  bool tmp_ro{ false };
  try {
    size->get_size(img_w, img_h, tmp_r_w, tmp_r_h, tmp_red, tmp_ro);
    restricted_size->get_size(img_w, img_h, tmp_r_w, tmp_r_h, tmp_red, tmp_ro);
  } catch (Sipi::SipiSizeError &err) {
    send_error(conn_obj, Connection::BAD_REQUEST, err.to_string());
    return;
  } catch (Sipi::SipiError &err) {
    send_error(conn_obj, Connection::BAD_REQUEST, err);
    return;
  }

  // if restricted size is set and smaller, we use it
  if (!restricted_size->undefined() && (*size > *restricted_size)) { size = restricted_size; }

  std::string cannonical_watermark = watermark.empty() ? "0" : "1";

  //.....................................................................
  // here we start building the canonical URL
  //
  std::pair<std::string, std::string> canonical_info;
  try {
    canonical_info = Sipi::SipiHttpServer::get_canonical_url(img_w,
      img_h,
      conn_obj.host(),
      params[iiif_prefix],
      sid.getIdentifier(),
      region,
      size,
      rotation,
      quality_format,
      sid.getPage(),
      cannonical_watermark);
  } catch (Sipi::SipiError &err) {
    send_error(conn_obj, Connection::BAD_REQUEST, err);
    return;
  }

  std::string canonical_header = canonical_info.first;
  std::string canonical = canonical_info.second;

  // now we check if we can send the file directly
  //
  if ((region->getType() == SipiRegion::FULL) && (size->getType() == SipiSize::FULL) && (angle == 0.0) && (!mirror)
      && watermark.empty() && (quality_format.format() == in_format)
      && (quality_format.quality() == SipiQualityFormat::DEFAULT)) {
    conn_obj.status(Connection::OK);
    conn_obj.header("Cache-Control", "must-revalidate, post-check=0, pre-check=0");
    conn_obj.header("Link", canonical_header);

    // set the header (mimetype)
    switch (quality_format.format()) {
    case SipiQualityFormat::TIF:
      conn_obj.header("Content-Type", "image/tiff");
      break;
    case SipiQualityFormat::JPG:
      conn_obj.header("Content-Type", "image/jpeg");
      break;
    case SipiQualityFormat::PNG:
      conn_obj.header("Content-Type", "image/png");
      break;
    case SipiQualityFormat::JP2:
      conn_obj.header("Content-Type", "image/jp2");
      break;
    default: {
    }
    }
    try {
      if (not_head_request) conn_obj.sendFile(infile);
    } catch (shttps::InputFailure iofail) {
      log_debug("Browser unexpectedly closed connection");
    } catch (Sipi::SipiError &err) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err);
    }
    return;
  }// finish sending unmodified file in toto

  // we only allow the cache if the file is not watermarked
  if (cache != nullptr) {
    //!>
    //!> here we check if the file is in the cache. If so, it's being blocked from deletion
    //!>
    std::string cachefile = cache->check(infile, canonical, true);
    // we block the file from being deleted if successfull

    if (!cachefile.empty()) {
      log_debug("Using cachefile %s", cachefile.c_str());
      conn_obj.status(Connection::OK);
      conn_obj.header("Cache-Control", "must-revalidate, post-check=0, pre-check=0");
      conn_obj.header("Link", canonical_header);

      // set the header (mimetype)
      switch (quality_format.format()) {
      case SipiQualityFormat::TIF:
        conn_obj.header("Content-Type", "image/tiff");
        break;
      case SipiQualityFormat::JPG:
        conn_obj.header("Content-Type", "image/jpeg");
        break;
      case SipiQualityFormat::PNG:
        conn_obj.header("Content-Type", "image/png");
        break;
      case SipiQualityFormat::JP2:
        conn_obj.header("Content-Type", "image/jp2");
        break;
      default: {
      }
      }

      try {
        //!> send the file from cache
        if (not_head_request) conn_obj.sendFile(cachefile);
        //!> from now on the cache file can be deleted again
      } catch (shttps::InputFailure err) {
        // -1 was thrown
        log_debug("Browser unexpectedly closed connection");
        cache->deblock(cachefile);
        return;
      } catch (Sipi::SipiError &err) {
        log_err("Error sending cache file: \"%s\": %s", cachefile.c_str(), err.to_string().c_str());
        send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err);
        cache->deblock(cachefile);
        return;
      }
      cache->deblock(cachefile);
      return;
    }
    cache->deblock(cachefile);
  }

  Sipi::SipiImage img;
  try {
    img.read(infile, region, size, quality_format.format() == SipiQualityFormat::JPG, server->scaling_quality());
  } catch (const SipiImageError &err) {
    send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err.to_string());
    return;
  } catch (const SipiSizeError &err) {
    send_error(conn_obj, Connection::BAD_REQUEST, err.to_string());
    return;
  }

  //
  // now we rotate
  //
  if (mirror || (angle != 0.0)) {
    try {
      img.rotate(angle, mirror);
    } catch (Sipi::SipiError &err) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err);
      return;
    }
  }

  if (quality_format.quality() != SipiQualityFormat::DEFAULT) {
    switch (quality_format.quality()) {
    case SipiQualityFormat::COLOR:
      img.convertToIcc(SipiIcc(icc_sRGB), 8);
      break;// for now, force 8 bit/sample
    case SipiQualityFormat::GRAY:
      img.convertToIcc(SipiIcc(icc_GRAY_D50), 8);
      break;// for now, force 8 bit/sample
    case SipiQualityFormat::BITONAL:
      img.toBitonal();
      break;
    default: {
      send_error(conn_obj, Connection::BAD_REQUEST, "Invalid quality specificer");
      return;
    }
    }
  }

  //
  // let's add a watermark if necessary
  //
  if (!watermark.empty()) {
    try {
      img.add_watermark(watermark);
    } catch (Sipi::SipiError &err) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err);
      log_err("GET %s: error adding watermark: %s", uri.c_str(), err.to_string().c_str());
      return;
    } catch (std::exception &err) {
      send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err.what());
      log_err("GET %s: error adding watermark: %s", uri.c_str(), err.what());
      return;
    }
    log_info("GET %s: adding watermark", uri.c_str());
  }

  img.connection(&conn_obj);
  conn_obj.header("Cache-Control", "must-revalidate, post-check=0, pre-check=0");
  std::string cachefile;

  try {
    if (cache != nullptr) {
      try {
        //!> open the cache file to write into.
        cachefile = cache->getNewCacheFileName();
        conn_obj.openCacheFile(cachefile);
      } catch (const shttps::Error &err) {
        send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err);
        return;
      }
    }
    switch (quality_format.format()) {
    case SipiQualityFormat::JPG: {
      conn_obj.status(Connection::OK);
      conn_obj.header("Link", canonical_header);
      conn_obj.header("Content-Type", "image/jpeg");// set the header (mimetype)
      conn_obj.setChunkedTransfer();
      Sipi::SipiCompressionParams qp = { { JPEG_QUALITY, std::to_string(server->jpeg_quality()) } };
      if (not_head_request) img.write("jpg", "HTTP", &qp);
      break;
    }
    case SipiQualityFormat::JP2: {
      conn_obj.status(Connection::OK);
      conn_obj.header("Link", canonical_header);
      conn_obj.header("Content-Type", "image/jp2");// set the header (mimetype)
      conn_obj.setChunkedTransfer();
      if (not_head_request) img.write("jpx", "HTTP");
      break;
    }
    case SipiQualityFormat::TIF: {
      conn_obj.status(Connection::OK);
      conn_obj.header("Link", canonical_header);
      conn_obj.header("Content-Type", "image/tiff");// set the header (mimetype)
      // no chunked transfer needed...

      if (not_head_request) img.write("tif", "HTTP");
      break;
    }
    case SipiQualityFormat::PNG: {
      conn_obj.status(Connection::OK);
      conn_obj.header("Link", canonical_header);
      conn_obj.header("Content-Type", "image/png");// set the header (mimetype)
      conn_obj.setChunkedTransfer();

      if (not_head_request) img.write("png", "HTTP");
      break;
    }
    default: {
      // HTTP 400 (format not supported)
      log_warn("Unsupported file format requested! Supported are .jpg, .jp2, .tif, .png");
      conn_obj.setBuffer();
      conn_obj.status(Connection::BAD_REQUEST);
      conn_obj.header("Content-Type", "text/plain");
      conn_obj << "Not Implemented!\n";
      conn_obj << "Unsupported file format requested! Supported are .jpg, .jp2, .tif, .png\n";
      conn_obj.flush();
    }
    }

    if (conn_obj.isCacheFileOpen()) {
      conn_obj.closeCacheFile();
      //!>
      //!> ATTENTION!!! Here we change the list of available cache files. Removable when debugging.
      //!>
      cache->add(infile, canonical, cachefile, img_w, img_h, tile_w, tile_h, clevels, numpages);
    }
  } catch (Sipi::SipiError &err) {
    if (cache != nullptr) {
      conn_obj.closeCacheFile();
      unlink(cachefile.c_str());
    }
    send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err);
    return;
  } catch (Sipi::SipiImageError &err) {
    if (cache != nullptr) {
      conn_obj.closeCacheFile();
      unlink(cachefile.c_str());
    }
    send_error(conn_obj, Connection::INTERNAL_SERVER_ERROR, err.what());
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
  auto *serv = static_cast<SipiHttpServer *>(user_data);
  const bool prefix_as_path = serv->prefix_as_path();
  const auto uri = conn_obj.uri();// has form "/pre/fix/es.../BAU_1_000441077_2_1.j2k/full/,1000/0/default.jpg"

  std::vector<std::string> params{};
  auto request_type{ handlers::iiif_handler::UNDEFINED };

  if (auto parse_url_result = handlers::iiif_handler::parse_iiif_uri(uri); parse_url_result.has_value()) {
    params = parse_url_result.value().params;
    request_type = parse_url_result.value().request_type;
  } else {
    send_error(conn_obj, Connection::BAD_REQUEST, parse_url_result.error());
    return;
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
  _salsah_prefix = "imgrep";
  _cache = nullptr;
  _scaling_quality = { ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH, ScalingMethod::HIGH };
}

//=========================================================================

void SipiHttpServer::cache(const std::string &cachedir_p,
  size_t max_cachesize_p,
  size_t max_nfiles_p,
  float cache_hysteresis_p)
{
  try {
    _cache = std::make_shared<SipiCache>(cachedir_p, max_cachesize_p, max_nfiles_p, cache_hysteresis_p);
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
  // setting the image root
  //
  log_info("Serving images from %s", _imgroot.c_str());
  log_debug("Salsah prefix: %s", _salsah_prefix.c_str());

  add_route(Connection::GET, "/favicon.ico", favicon_handler);
  add_route(Connection::GET, "/", iiif_handler);
  add_route(Connection::HEAD, "/", iiif_handler);

  user_data(this);

  // in shttps::Server::run(), add additional routes are added, namely the ones for the LUA scripts
  Server::run();
}

//=========================================================================
}// namespace Sipi
