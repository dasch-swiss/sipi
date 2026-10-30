/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <numeric>
#include <ostream>
#include <utility>
#include <vector>

#include <expat.h>
#include <openssl/evp.h>

#include "metadata/xmp.h"

/*!
 * SIPI stores the XMP packet verbatim: it neither parses nor validates the
 * RDF/XML payload, because Exiv2's XMP parser is not thread-safe at the
 * Exiv2 version SIPI depends on. xmpBytes() returns exactly the bytes it
 * was constructed with.
 */

namespace Sipi {

XmpMutex xmp_mutex;

void xmplock_func(void *pLockData, bool lockUnlock)
{
  auto *m = static_cast<XmpMutex *>(pLockData);
  if (lockUnlock) {
    m->lock.lock();
  } else {
    m->lock.unlock();
  }
}
//=========================================================================

Xmp::Xmp(const std::string &xmp) { __xmpstr = xmp; }
//============================================================================

Xmp::Xmp(const char *xmp) { __xmpstr = xmp; }
//============================================================================

Xmp::Xmp(const char *xmp, int len)
{
  std::string buf(xmp, len);
  __xmpstr = buf;
}
//============================================================================


Xmp::~Xmp() {}
//============================================================================


// Returns the RDF/XML packet exactly as it was constructed.
std::string Xmp::xmpBytes() { return __xmpstr; }
//============================================================================

namespace {

  bool is_name_char(char c)
  {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == '.' || c == ':';
  }

  std::size_t skip_space(const std::string &s, std::size_t i)
  {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) { ++i; }
    return i;
  }

  // A real packet binds a namespace a few times at most; the cap keeps a
  // hostile one from multiplying the passes.
  constexpr std::size_t kMaxBindings = 8;
  constexpr std::size_t kMaxStartTagBytes = 256;
  // An element to erase may declare namespaces in its start tag.
  constexpr std::size_t kMaxErasedStartTagBytes = 4096;

  // The prefixes the packet binds to `uri` (`xmlns:p="uri"`, at most
  // kMaxBindings), plus the conventional `defaults`, which always apply.
  std::vector<std::string>
    prefixes_bound_to(const std::string &s, const std::string &uri, std::initializer_list<const char *> defaults)
  {
    std::vector<std::string> prefixes;
    for (std::size_t pos = s.find("xmlns:"); pos != std::string::npos && prefixes.size() < kMaxBindings;
         pos = s.find("xmlns:", pos + 1)) {
      std::size_t i = pos + 6;
      const std::size_t name_start = i;
      while (i < s.size() && is_name_char(s[i]) && s[i] != ':') { ++i; }
      const std::string prefix = s.substr(name_start, i - name_start);
      i = skip_space(s, i);
      if (prefix.empty() || i >= s.size() || s[i] != '=') { continue; }
      i = skip_space(s, i + 1);
      if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) { continue; }
      if (s.compare(i + 1, uri.size(), uri) == 0 && i + 1 + uri.size() < s.size() && s[i + 1 + uri.size()] == s[i]
          && std::find(prefixes.begin(), prefixes.end(), prefix) == prefixes.end()) {
        prefixes.push_back(prefix);
      }
    }
    for (const char *d : defaults) {
      if (std::find(prefixes.begin(), prefixes.end(), d) == prefixes.end()) { prefixes.emplace_back(d); }
    }
    return prefixes;
  }

  // Replaces the integer value of every `name="123"` attribute and
  // `<name ...>123</name>` element. One forward pass; the search for the end
  // of a start tag is bounded, so unterminated tags cannot make it quadratic.
  void set_integer_property(std::string &s, const std::string &name, const std::string &value)
  {
    std::string out;
    std::size_t copied = 0;
    for (std::size_t pos = s.find(name); pos != std::string::npos; pos = s.find(name, pos + 1)) {
      const std::size_t after = pos + name.size();
      if (after < s.size() && is_name_char(s[after])) { continue; }
      const char before = pos == 0 ? ' ' : s[pos - 1];
      std::size_t digits = std::string::npos;
      char terminator = 0;
      if (before == '<') {
        const auto limit = s.begin() + static_cast<std::ptrdiff_t>(std::min(s.size(), after + kMaxStartTagBytes));
        const auto close = std::find(s.begin() + static_cast<std::ptrdiff_t>(after), limit, '>');
        if (close == limit) { continue; }
        digits = skip_space(s, static_cast<std::size_t>(close - s.begin()) + 1);
      } else if (!is_name_char(before) && before != '/') {
        std::size_t i = skip_space(s, after);
        if (i >= s.size() || s[i] != '=') { continue; }
        i = skip_space(s, i + 1);
        if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) { continue; }
        terminator = s[i];
        digits = i + 1;
      } else {
        continue;
      }
      std::size_t end = digits;
      while (end < s.size() && std::isdigit(static_cast<unsigned char>(s[end])) != 0) { ++end; }
      if (end == digits) { continue; }
      if (terminator != 0) {
        if (end >= s.size() || s[end] != terminator) { continue; }
      } else if (s.compare(skip_space(s, end), name.size() + 3, "</" + name + ">") != 0) {
        continue;
      }
      out.append(s, copied, digits - copied);
      out += value;
      copied = end;
    }
    if (copied > 0) {
      out.append(s, copied);
      s = std::move(out);
    }
  }

  // Removes every `<name ...>...</name>` and `<name .../>` element. False if
  // a start tag or an element has no end.
  bool erase_element(std::string &s, const std::string &name)
  {
    const std::string open = "<" + name;
    const std::string close = "</" + name;
    std::string out;
    std::size_t copied = 0;
    std::size_t pos = s.find(open);
    while (pos != std::string::npos) {
      const std::size_t after = pos + open.size();
      if (after < s.size() && is_name_char(s[after])) {
        pos = s.find(open, after);
        continue;
      }
      const auto limit = s.begin() + static_cast<std::ptrdiff_t>(std::min(s.size(), after + kMaxErasedStartTagBytes));
      const auto gt = std::find(s.begin() + static_cast<std::ptrdiff_t>(after), limit, '>');
      if (gt == limit) { return false; }
      std::size_t end = static_cast<std::size_t>(gt - s.begin()) + 1;
      if (*(gt - 1) != '/') {
        std::size_t c = s.find(close, end);
        while (c != std::string::npos && c + close.size() < s.size() && is_name_char(s[c + close.size()])) {
          c = s.find(close, c + close.size());
        }
        if (c == std::string::npos) { return false; }
        end = skip_space(s, c + close.size());
        if (end >= s.size() || s[end] != '>') { return false; }
        ++end;
      }
      out.append(s, copied, pos - copied);
      copied = end;
      pos = s.find(open, end);
    }
    if (copied > 0) {
      out.append(s, copied);
      s = std::move(out);
    }
    return true;
  }

}// namespace

void Xmp::set_pixel_dimensions(std::size_t w, std::size_t h)
{
  // Any prefix bound to a namespace names the same properties.
  const std::string wv = std::to_string(w);
  const std::string hv = std::to_string(h);
  for (const auto &p : prefixes_bound_to(__xmpstr, "http://ns.adobe.com/exif/1.0/", { "exif" })) {
    set_integer_property(__xmpstr, p + ":PixelXDimension", wv);
    set_integer_property(__xmpstr, p + ":PixelYDimension", hv);
  }
  for (const auto &p : prefixes_bound_to(__xmpstr, "http://ns.adobe.com/tiff/1.0/", { "tiff" })) {
    set_integer_property(__xmpstr, p + ":ImageWidth", wv);
    set_integer_property(__xmpstr, p + ":ImageLength", hv);
  }
  for (const auto &p :
    prefixes_bound_to(__xmpstr, "http://iptc.org/std/Iptc4xmpExt/2008-02-29/", { "Iptc4xmpExt" })) {
    set_integer_property(__xmpstr, p + ":MaxAvailWidth", wv);
    set_integer_property(__xmpstr, p + ":MaxAvailHeight", hv);
  }
}
//============================================================================

bool Xmp::erase_thumbnails()
{
  for (const auto &p : prefixes_bound_to(__xmpstr, "http://ns.adobe.com/xap/1.0/", { "xmp", "xap" })) {
    if (!erase_element(__xmpstr, p + ":Thumbnails")) { return false; }
  }
  for (const auto &p : prefixes_bound_to(__xmpstr, "http://ns.adobe.com/xap/1.0/g/img/", { "xmpGImg", "xapGImg" })) {
    if (!erase_element(__xmpstr, p + ":image")) { return false; }
  }
  // A text scan cannot resolve every binding (a default namespace, a prefix
  // past the cap): a Thumbnails element or GImg image left over fails.
  for (std::size_t pos = __xmpstr.find("Thumbnails"); pos != std::string::npos;
       pos = __xmpstr.find("Thumbnails", pos + 1)) {
    const char before = pos == 0 ? ' ' : __xmpstr[pos - 1];
    const std::size_t after = pos + 10;
    if ((before == ':' || before == '<') && (after >= __xmpstr.size() || !is_name_char(__xmpstr[after]))) {
      return false;
    }
  }
  return __xmpstr.find("GImg:image") == std::string::npos;
}
//============================================================================

namespace {

  constexpr const char *kRdfNs = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";

  struct Description
  {
    std::string about;
    std::vector<std::pair<std::string, std::string>> bindings;// in scope: prefix ("" = default), URI
  };

  struct Property
  {
    std::size_t begin;
    std::size_t end;
    std::size_t description;
    bool rights;
  };

  // Real packets bind a few dozen namespaces and hold a few descriptions;
  // every description copies the bindings in scope, so both are capped.
  constexpr std::size_t kMaxSplitBindings = 64;
  constexpr std::size_t kMaxSplitDescriptions = 256;

  // The packet's top-level properties: the children of every rdf:Description
  // directly inside rdf:RDF, as expat (namespace mode, ' ' separator) reports them.
  struct Scan
  {
    XML_Parser parser = nullptr;
    std::vector<std::string> path;
    std::vector<std::pair<std::string, std::string>> bindings;
    std::vector<Description> descriptions;
    std::vector<Property> properties;
    std::size_t rdf_content = std::string::npos;// first byte after the rdf:RDF start tag
    std::size_t property_depth = 0;
    std::size_t property_tag = 0;
    const char *refused = nullptr;// why the scan stopped the parse

    void refuse(const char *why)
    {
      refused = why;
      XML_StopParser(parser, XML_FALSE);
    }

    static void start_ns(void *self, const XML_Char *prefix, const XML_Char *uri)
    {
      auto &s = *static_cast<Scan *>(self);
      if (s.bindings.size() == kMaxSplitBindings) {
        s.refuse("more than 64 namespace bindings in scope");
        return;
      }
      s.bindings.emplace_back(prefix != nullptr ? prefix : "", uri != nullptr ? uri : "");
    }

    // XMP has no DOCTYPE; entities would not survive the cut.
    static void doctype(void *self, const XML_Char *, const XML_Char *, const XML_Char *, int)
    {
      static_cast<Scan *>(self)->refuse("a DOCTYPE");
    }

    static void end_ns(void *self, const XML_Char *prefix)
    {
      auto &b = static_cast<Scan *>(self)->bindings;
      const std::string p = prefix != nullptr ? prefix : "";
      const auto it = std::find_if(b.rbegin(), b.rend(), [&](const auto &e) { return e.first == p; });
      if (it != b.rend()) { b.erase(std::next(it).base()); }
    }

    static void start(void *self, const XML_Char *name, const XML_Char **atts)
    {
      auto &s = *static_cast<Scan *>(self);
      const std::string rdf(kRdfNs);
      const auto here = static_cast<std::size_t>(XML_GetCurrentByteIndex(s.parser));
      const auto tag = static_cast<std::size_t>(XML_GetCurrentByteCount(s.parser));
      const std::size_t depth = s.path.size();
      if (name == rdf + " RDF" && s.rdf_content == std::string::npos) { s.rdf_content = here + tag; }
      if (name == rdf + " Description" && depth > 0 && s.path.back() == rdf + " RDF") {
        if (s.descriptions.size() == kMaxSplitDescriptions) {
          s.refuse("more than 256 rdf:Description elements");
          return;
        }
        Description d{ "", s.bindings };
        for (int i = 0; atts[i] != nullptr; i += 2) {
          if (atts[i] == rdf + " about") { d.about = atts[i + 1]; }
        }
        s.descriptions.push_back(std::move(d));
      } else if (depth > 1 && s.path.back() == rdf + " Description" && s.path[depth - 2] == rdf + " RDF") {
        static const std::vector<std::string> rights = { "http://purl.org/dc/elements/1.1/ rights",
          "http://purl.org/dc/elements/1.1/ creator",
          "http://ns.adobe.com/photoshop/1.0/ Credit" };
        static const std::vector<std::string> rights_ns = { "http://ns.adobe.com/xap/1.0/rights/ ",
          "http://ns.useplus.org/ldf/xmp/1.0/ ",
          "http://creativecommons.org/ns# " };
        const std::string n(name);
        const bool is_rights =
          std::find(rights.begin(), rights.end(), n) != rights.end()
          || std::any_of(rights_ns.begin(), rights_ns.end(), [&](const std::string &ns) { return n.starts_with(ns); });
        s.properties.push_back({ here, here + tag, s.descriptions.size() - 1, is_rights });
        s.property_depth = depth + 1;
        s.property_tag = tag;
      }
      s.path.emplace_back(name);
    }

    static void end(void *self, const XML_Char *)
    {
      auto &s = *static_cast<Scan *>(self);
      if (s.path.size() == s.property_depth) {
        const auto count = static_cast<std::size_t>(XML_GetCurrentByteCount(s.parser));
        // An empty-element tag reports its end with no bytes of its own.
        if (count > 0) {
          s.properties.back().end = static_cast<std::size_t>(XML_GetCurrentByteIndex(s.parser)) + count;
        }
        s.property_depth = 0;
      }
      s.path.pop_back();
    }
  };

  std::string escaped(const std::string &v)
  {
    std::string out;
    for (const char c : v) {
      switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += c;
      }
    }
    return out;
  }

  std::string md5_hex(const std::string &bytes)
  {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest, &len, EVP_md5(), nullptr) != 1) { return {}; }
    std::string hex;
    char buf[3];
    for (unsigned int i = 0; i < len; ++i) {
      std::snprintf(buf, sizeof(buf), "%02X", digest[i]);
      hex += buf;
    }
    return hex;
  }

  std::string has_extended_note(const std::string &about, const std::string &guid)
  {
    return std::string(R"(<rdf:Description xmlns:rdf=")") + kRdfNs + R"(" rdf:about=")" + escaped(about)
           + R"(" xmlns:xmpNote="http://ns.adobe.com/xmp/note/" xmpNote:HasExtendedXMP=")" + guid + R"("/>)";
  }

}// namespace

Result<Xmp::Extended> Xmp::split_extended(std::size_t limit) const
{
  const auto fail = [](const std::string &why) {
    return std::unexpected(SipiValueError{ ErrorCode::kWriteFailed, "XMP packet cannot be split: " + why });
  };
  if (__xmpstr.size() > INT_MAX) { return fail("over 2 GB"); }
  Scan scan;
  const std::unique_ptr<XML_ParserStruct, decltype(&XML_ParserFree)> parser(
    XML_ParserCreateNS(nullptr, ' '), &XML_ParserFree);
  if (parser == nullptr) { return fail("no XML parser"); }
  scan.parser = parser.get();
  XML_SetUserData(parser.get(), &scan);
  XML_SetNamespaceDeclHandler(parser.get(), Scan::start_ns, Scan::end_ns);
  XML_SetElementHandler(parser.get(), Scan::start, Scan::end);
  XML_SetStartDoctypeDeclHandler(parser.get(), Scan::doctype);
  if (XML_Parse(parser.get(), __xmpstr.data(), static_cast<int>(__xmpstr.size()), XML_TRUE) != XML_STATUS_OK) {
    return fail(scan.refused != nullptr ? scan.refused : XML_ErrorString(XML_GetErrorCode(parser.get())));
  }
  if (scan.rdf_content == std::string::npos || scan.descriptions.empty()) { return fail("no rdf:Description"); }
  const std::string about = scan.descriptions.front().about;

  // Rights properties go last; among the rest the largest go first.
  std::vector<std::size_t> order(scan.properties.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    const Property &pa = scan.properties[a];
    const Property &pb = scan.properties[b];
    if (pa.rights != pb.rights) { return !pa.rights; }
    return pa.end - pa.begin > pb.end - pb.begin;
  });
  std::vector<bool> moved(scan.properties.size(), false);
  std::size_t size = __xmpstr.size() + has_extended_note(about, std::string(32, '0')).size();
  for (const std::size_t i : order) {
    if (size <= limit) { break; }
    moved[i] = true;
    size -= scan.properties[i].end - scan.properties[i].begin;
  }
  if (size > limit) { return fail("over " + std::to_string(limit) + " bytes with every property moved"); }

  Extended out;
  out.extended = std::string(R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf=")") + kRdfNs + R"(">)";
  for (std::size_t d = 0; d < scan.descriptions.size(); ++d) {
    std::string body;
    for (std::size_t i = 0; i < scan.properties.size(); ++i) {
      const Property &p = scan.properties[i];
      if (moved[i] && p.description == d) { body.append(__xmpstr, p.begin, p.end - p.begin); }
    }
    if (body.empty()) { continue; }
    std::string open = R"(<rdf:Description rdf:about=")" + escaped(scan.descriptions[d].about) + '"';
    const auto &bindings = scan.descriptions[d].bindings;
    for (std::size_t b = 0; b < bindings.size(); ++b) {
      const auto &[prefix, uri] = bindings[b];
      // The innermost binding of a prefix is the one in scope.
      if (std::any_of(bindings.begin() + static_cast<std::ptrdiff_t>(b) + 1, bindings.end(), [&](const auto &e) {
            return e.first == prefix;
          })) {
        continue;
      }
      // The bindings go on the rdf:Description itself, so they name it too.
      if (prefix == "rdf" && uri != kRdfNs) { return fail("prefix rdf bound to " + uri); }
      open += " xmlns" + (prefix.empty() ? "" : ":" + prefix) + "=\"" + escaped(uri) + '"';
    }
    out.extended += open + '>' + body + "</rdf:Description>";
  }
  out.extended += "</rdf:RDF></x:xmpmeta>";
  out.guid = md5_hex(out.extended);
  if (out.guid.size() != 32) { return fail("no MD5"); }

  out.standard.reserve(size);
  std::size_t copied = 0;
  const auto copy_to = [&](std::size_t pos) {
    out.standard.append(__xmpstr, copied, pos - copied);
    copied = pos;
  };
  copy_to(scan.rdf_content);
  out.standard += has_extended_note(about, out.guid);
  for (std::size_t i = 0; i < scan.properties.size(); ++i) {
    if (!moved[i]) { continue; }
    copy_to(scan.properties[i].begin);
    copied = scan.properties[i].end;
  }
  copy_to(__xmpstr.size());
  return out;
}
//============================================================================

std::ostream &operator<<(std::ostream &outstr, const Xmp &) { return outstr; }
//============================================================================

}// namespace Sipi
