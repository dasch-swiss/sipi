/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "iiif_handler.hpp"

#include <algorithm>
#include <string_view>

#include "shttps/Connection.h"

namespace handlers::iiif_handler {

namespace {

// Validators for IIIF Image API 3.0 URL components. Single pass, no
// allocation, linear time.

[[nodiscard]] bool consume_posint(std::string_view &s) noexcept
{
  size_t i = 0;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; }
  if (i == 0) { return false; }
  s.remove_prefix(i);
  return true;
}

// IIIF posfloat: `digit+ ('.' digit+)?`. See spec §5.1.1.
[[nodiscard]] bool consume_posfloat(std::string_view &s) noexcept
{
  if (!consume_posint(s)) { return false; }
  if (!s.empty() && s.front() == '.') {
    std::string_view tail = s;
    tail.remove_prefix(1);
    if (!consume_posint(tail)) { return false; }
    s = tail;
  }
  return true;
}

[[nodiscard]] bool starts_with_consume(std::string_view &s, std::string_view prefix) noexcept
{
  if (s.size() < prefix.size() || s.substr(0, prefix.size()) != prefix) { return false; }
  s.remove_prefix(prefix.size());
  return true;
}

// `quality "." format` — quality ∈ {color, gray, bitonal, default},
// format ∈ {jpg, tif, png, jp2}. The format set is what `SipiImage::io` writes.
[[nodiscard]] bool is_valid_qualform(std::string_view s) noexcept
{
  constexpr std::string_view qualities[] = { "color", "gray", "bitonal", "default" };
  constexpr std::string_view formats[] = { "jpg", "tif", "png", "jp2" };
  for (auto q : qualities) {
    if (starts_with_consume(s, q)) {
      if (!starts_with_consume(s, ".")) { return false; }
      return std::any_of(std::begin(formats), std::end(formats), [&](std::string_view f) { return s == f; });
    }
  }
  return false;
}

// `'!'? posfloat` — IIIF 3.0 §4.3 + §5.1.1.
[[nodiscard]] bool is_valid_rotation(std::string_view s) noexcept
{
  if (!s.empty() && s.front() == '!') { s.remove_prefix(1); }
  if (!consume_posfloat(s)) { return false; }
  return s.empty();
}

// `'^'? size-form` where size-form ∈ {
//   "max", "pct:" posfloat, posint ",", "," posint, posint "," posint, "!" posint "," posint
// }. The `!` prefix only combines with the full `posint "," posint` confined form.
[[nodiscard]] bool is_valid_size(std::string_view s) noexcept
{
  if (!s.empty() && s.front() == '^') { s.remove_prefix(1); }
  if (s == "max") { return true; }
  if (starts_with_consume(s, "pct:")) { return consume_posfloat(s) && s.empty(); }
  bool has_bang = false;
  if (!s.empty() && s.front() == '!') {
    has_bang = true;
    s.remove_prefix(1);
  }
  if (!s.empty() && s.front() == ',') {
    if (has_bang) { return false; }
    s.remove_prefix(1);
    return consume_posint(s) && s.empty();
  }
  if (!consume_posint(s)) { return false; }
  if (!starts_with_consume(s, ",")) { return false; }
  if (s.empty()) { return !has_bang; }
  return consume_posint(s) && s.empty();
}

// `"full" | "square" | posint,posint,posint,posint | "pct:" posfloat,posfloat,posfloat,posfloat`
[[nodiscard]] bool is_valid_region(std::string_view s) noexcept
{
  if (s == "full" || s == "square") { return true; }
  if (starts_with_consume(s, "pct:")) {
    for (int i = 0; i < 4; ++i) {
      if (i > 0 && !starts_with_consume(s, ",")) { return false; }
      if (!consume_posfloat(s)) { return false; }
    }
    return s.empty();
  }
  for (int i = 0; i < 4; ++i) {
    if (i > 0 && !starts_with_consume(s, ",")) { return false; }
    if (!consume_posint(s)) { return false; }
  }
  return s.empty();
}

}// namespace

template<typename T> std::string vector_to_string(const std::vector<T> &vec)
{
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < vec.size(); ++i) {
    if (i > 0) { oss << ", "; }
    oss << vec[i];
  }
  oss << "]";
  return oss.str();
}

static auto make_parse_error(std::string msg) -> std::expected<IIIFUriParseResult, std::string>
{
  return std::expected<IIIFUriParseResult, std::string>(std::unexpect, std::move(msg));
}

/**
 * This function parses parts of an IIIF URI and returns a struct with the result.
 *
 * In general, the IIIF URI schema looks like this:
 * {scheme}://{server}{/prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format}
 *
 * The string that is passed to this function is expected to be already stripped
 * off of the {schema} and {server} parts, thus only getting:
 * {/prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format},
 * e.g., "/iiif/2/image.jpg/full/200,/0/default.jpg".
 *
 * TODO: The parsing is not complete and needs to be extended to cover all cases.
 * TODO: Property based testing would be useful here.
 */
[[nodiscard]] auto parse_iiif_uri(const std::string &uri) noexcept -> std::expected<IIIFUriParseResult, std::string>
{

  // std::cout << ">> parsing IIIF URI: " << uri << std::endl;

  RequestType request_type{ UNDEFINED };

  std::vector<std::string> parts;
  size_t pos = 0;
  size_t old_pos = 0;

  //
  // IIIF URi schema:
  // {scheme}://{server}{/prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format}
  //
  // The slashes "/" separate the different parts...
  //
  while ((pos = uri.find('/', pos)) != std::string::npos) {
    pos++;
    if (pos == 1) {
      // if first char is a token skip it!
      old_pos = pos;
      continue;
    }
    parts.push_back(shttps::urldecode(uri.substr(old_pos, pos - old_pos - 1)));
    old_pos = pos;
  }

  if (old_pos != uri.length()) { parts.push_back(shttps::urldecode(uri.substr(old_pos, std::string::npos))); }

  if (parts.empty()) { return make_parse_error("No parameters/path given"); }

  // std::cout << ">> parts found: " << vector_to_string(parts) << std::endl;

  std::vector<std::string> params;

  const bool qualform_ok = !parts.empty() && is_valid_qualform(parts[parts.size() - 1]);
  const bool rotation_ok = parts.size() > 1 && is_valid_rotation(parts[parts.size() - 2]);
  const bool size_ok = parts.size() > 2 && is_valid_size(parts[parts.size() - 3]);
  const bool region_ok = parts.size() > 3 && is_valid_region(parts[parts.size() - 4]);

  // analyze the last part of the URL and look for a dot
  if ((pos = parts[parts.size() - 1].find('.', 0)) != std::string::npos) {

    // we have a dot and we will split the last part into a body and an extension
    // at this point we know that the last part could be either a quality format, a knora.json or a info.json
    std::string fname_body = parts[parts.size() - 1].substr(0, pos);
    std::string fname_extension = parts[parts.size() - 1].substr(pos + 1, std::string::npos);

    // Let's check if we have a valid IIIF URI
    if (qualform_ok && rotation_ok && size_ok && region_ok) {
      //
      // we potentially have a valid IIIF URI
      //
      if (parts.size() >= 6) {
        // we have a prefix
        std::stringstream prefix;
        for (size_t i = 0; i < (parts.size() - 5); i++) {
          if (i > 0) prefix << "/";
          prefix << parts[i];
        }
        params.push_back(prefix.str());// iiif_prefix
      } else if (parts.size() == 5) {
        // we have no prefix
        params.emplace_back("");// iiif_prefix
      } else {
        return make_parse_error("IIIF url not correctly formatted");
      }
      params.push_back(parts[parts.size() - 5]);// iiif_identifier
      params.push_back(parts[parts.size() - 4]);// iiif_region
      params.push_back(parts[parts.size() - 3]);// iiif_size
      params.push_back(parts[parts.size() - 2]);// iiif_rotation
      params.push_back(parts[parts.size() - 1]);// iiif_qualityformat
      request_type = IIIF;
    } else if ((fname_body == "info") && (fname_extension == "json")) {
      //
      // we potentially have something like "http:://{server}/{prefix}/{id}/info.json
      //
      if (parts.size() >= 3) {
        // we have a prefix
        std::stringstream prefix;
        for (size_t i = 0; i < (parts.size() - 2); i++) {
          if (i > 0) prefix << "/";
          prefix << parts[i];
        }
        params.push_back(prefix.str());// iiif_prefix
      } else if (parts.size() == 2) {
        // we have no prefix
        params.emplace_back("");// iiif_prefix
      } else {
        return make_parse_error("IIIF url not correctly formatted");
      }
      params.push_back(parts[parts.size() - 2]);// iiif_identifier
      request_type = INFO_JSON;
    } else if ((fname_body == "knora") && (fname_extension == "json")) {
      //
      // we potentially have something like "http:://{server}/{prefix}/{id}/knora.json
      //
      if (parts.size() >= 3) {
        // we have a prefix
        std::stringstream prefix;
        for (size_t i = 0; i < (parts.size() - 2); i++) {
          if (i > 0) prefix << "/";
          prefix << parts[i];
        }
        params.push_back(prefix.str());// iiif_prefix
      } else if (parts.size() == 2) {
        // we have no prefix
        params.emplace_back("");// iiif_prefix
      } else {
        return make_parse_error("IIIF url not correctly formatted");
      }
      params.push_back(parts[parts.size() - 2]);// iiif_identifier
      request_type = KNORA_JSON;
    } else {
      //
      // we potentially have something like "/{prefix}/{id}" with id as "body.ext"
      //
      if (qualform_ok) {
        // we have a valid quality format but the rest of the URL is not valid
        return make_parse_error("IIIF url not correctly formatted");
      }

      // or something like "/unit/lena512.jp2/full/max/0/garbage.garbage"
      // we optimize the check that three out of four parts are correct
      if (rotation_ok && size_ok && region_ok) {
        std::stringstream errmsg;
        errmsg << "IIIF url not correctly formatted:";
        if (!qualform_ok && !parts.empty()) errmsg << " Error in quality: \"" << parts[parts.size() - 1] << "\"!";
        return make_parse_error(errmsg.str());
      }

      if (parts.size() >= 2) {
        // we have a prefix
        //
        // we have something like "/{prefix}/{id}" with id as "body.ext"
        //

        // we have a prefix
        std::stringstream prefix;
        for (size_t i = 0; i < (parts.size() - 1); i++) {
          if (parts[i].empty()) {
            return make_parse_error("IIIF url not correctly formatted");
          }
          if (!prefix.str().empty()) { prefix << "/"; }
          prefix << parts[i];
        }
        params.push_back(prefix.str());// iiif_prefix
      } else if (parts.size() == 1) {
        // we have no prefix
        //
        // we have something like "/{id}" with id as "body.ext"
        //
        params.emplace_back("");// iiif_prefix
      } else {
        std::stringstream errmsg;
        errmsg << "IIIF url not correctly formatted:";
        if (!qualform_ok && (!parts.empty())) errmsg << " Error in quality: \"" << parts[parts.size() - 1] << "\"!";
        if (!rotation_ok && (parts.size() > 1)) errmsg << " Error in rotation: \"" << parts[parts.size() - 2] << "\"!";
        if (!size_ok && (parts.size() > 2)) errmsg << " Error in size: \"" << parts[parts.size() - 3] << "\"!";
        if (!region_ok && (parts.size() > 3)) errmsg << " Error in region: \"" << parts[parts.size() - 4] << "\"!";
        return make_parse_error(errmsg.str());
      }
      params.push_back(parts[parts.size() - 1]);// iiif_identifier
      request_type = REDIRECT;
    }
  } else if (parts[parts.size() - 1] == "file") {
    //
    // we potentially have something like "/{prefix}/{id}/file
    //
    if (parts.size() >= 3) {
      // we have a prefix
      //
      // we have something like "/{prefix}/{id}/file
      //
      std::stringstream prefix;
      for (size_t i = 0; i < (parts.size() - 2); i++) {
        if (!prefix.str().empty()) prefix << "/";
        prefix << parts[i];
      }
      params.push_back(prefix.str());// iiif_prefix
    } else if (parts.size() == 2) {
      // we have no prefix
      //
      // we have something like "/{id}/file
      //
      params.emplace_back("");// iiif_prefix
    } else {
      return make_parse_error("IIIF url not correctly formatted");
    }
    params.push_back(parts[parts.size() - 2]);// iiif_identifier
    request_type = FILE_DOWNLOAD;
  } else {
    //
    // we potentially have something like "/{prefix}/{id}" with id as "body_without_ext"
    // remember that there could potentially be more than one prefix
    //

    // or something like "/unit/lena512.jp2/full/max/0/jpg"
    // we optimize the check that three out of four parts are correct
    if (rotation_ok && size_ok && region_ok) {
      std::stringstream errmsg;
      errmsg << "IIIF url not correctly formatted:";
      if (!qualform_ok && !parts.empty()) errmsg << " Error in quality: \"" << parts[parts.size() - 1] << "\"!";
      return make_parse_error(errmsg.str());
    }
    if (parts.size() >= 2) {
      // we have a prefix
      std::stringstream prefix;
      for (size_t i = 0; i < (parts.size() - 1); i++) {
        if (parts[i].empty()) {
          return make_parse_error("IIIF url not correctly formatted");
        }
        if (!prefix.str().empty()) { prefix << "/"; }
        prefix << parts[i];
      }
      params.push_back(prefix.str());// iiif_prefix
    } else if (parts.size() == 1) {
      // we have no prefix
      params.emplace_back("");// iiif_prefix
    } else {
      return make_parse_error("IIIF url not correctly formatted");
    }
    params.push_back(parts[parts.size() - 1]);// iiif_identifier
    request_type = REDIRECT;
  }

  return IIIFUriParseResult{ request_type, params };
}

std::ostream &operator<<(std::ostream &os, const IIIFUriParseResult &result)
{
  os << "RequestType: " << result.request_type << ", Params: [";
  for (size_t i = 0; i < result.params.size(); ++i) {
    os << result.params[i];
    if (i < result.params.size() - 1) { os << ", "; }
  }
  os << "]";
  return os;
}

}// namespace handlers::iiif_handler
