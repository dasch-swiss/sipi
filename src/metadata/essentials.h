/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_METADATA_SIPI_ESSENTIALS_H
#define SIPI_METADATA_SIPI_ESSENTIALS_H

#include <cstddef>
#include <cstdint>
#include <expected>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "shttps/Hash.h"

namespace Sipi {

/*!
 * Plain value type holding the fields embedded in SIPI's image-header
 * "essentials" packet. Per DEV-6408 (Probe 2): the public API is a struct
 * of accessors, not 17 getter/setter pairs.
 *
 * Wire format: protobuf (`src/metadata/essentials.proto`) for new emissions
 * (Service Files); the legacy pipe-delimited form is parsed indefinitely via
 * `Essentials::parse_legacy`. Image-shape fields back the `read_shape` fast
 * path per ADR-0004 (DEV-6379 consumes them in Phase 9).
 */
struct EssentialsFields
{
  // Identity / provenance.
  std::string origname;//!< original filename
  std::string mimetype;//!< original mime type
  shttps::HashType hash_type = shttps::HashType::none;//!< type of checksum

  //! Raw digest bytes of the post-transformation pixel buffer (NOT hex). The
  //! legacy pipe-delimited carrier stored hex; `parse_legacy` decodes it on
  //! the way in. Comparing against `shttps::Hash::hash()` (hex) goes through
  //! `Essentials::to_hex(data_chksum)`.
  std::vector<std::byte> data_chksum;

  bool use_icc = false;//!< apply this ICC profile when converting from JPEG2000
  std::vector<unsigned char> icc_profile;//!< decoded ICC profile bytes (empty if none)

  // Image-shape fields (ADR-0004). Populated by the format-handler writer for
  // Service Files; read by the `read_shape` fast path. Activation criterion is
  // `img_w != 0 && img_h != 0` — both required; partial shape falls through to
  // format-native parsing.
  std::uint32_t img_w = 0;
  std::uint32_t img_h = 0;
  std::uint32_t tile_w = 0;
  std::uint32_t tile_h = 0;
  std::uint32_t clevels = 0;
  std::uint32_t numpages = 0;
  std::uint32_t nc = 0;
  std::uint32_t bps = 0;
};

/*!
 * Reasons a `Essentials::parse` call may fail. Mirrors the codec's
 * `CodecParseError` (the public surface adapts from it). Ordering matches
 * the dispatcher: cheaper checks first, then content validation.
 */
enum class ParseError {
  Empty,// zero-length input
  Malformed,// protobuf ParseFromArray returned false
  MissingVersion,// format_version == 0 (proto3 default → field never set)
  UnknownVersion,// format_version > 1 (future writer)
  MissingCore,// origname/mimetype/hash_type/data_chksum missing after a successful parse
};

/*!
 * Small class that creates a packet of essentials metadata embedded in an
 * image header (containing original filename, original mime type, checksum
 * method, pixel-data checksum, and optionally an ICC profile).
 *
 * Two on-disk wire forms coexist (per ADR-0005 / DEV-6410):
 *   * **protobuf** — new emissions (Service Files only). Created via
 *     `serialize_bytes()`, parsed via `Essentials::parse(span)`.
 *   * **pipe-delimited legacy** — retained indefinitely for reading legacy
 *     packets per DEV-6398 scope. Parsed via `Essentials::parse_legacy`.
 *     `serialize()` still produces this form during the Expand/Migrate
 *     window; Phase 14 contracts both the legacy ctor and `serialize()`.
 */
class Essentials
{
private:
  bool _is_set = false;
  EssentialsFields _fields{};

public:
  Essentials() = default;

  /*!
   * Construct from fully-populated fields. Marks the packet as set.
   */
  explicit Essentials(EssentialsFields fields) : _is_set(true), _fields(std::move(fields)) {}

  /*!
   * Parse a serialized (pipe-delimited) packet. Marks the packet as set on
   * successful parse.
   *
   * \deprecated Phase 14 removes this overload. New call sites should use
   * `Essentials::parse_legacy(std::string_view)` for the legacy form or
   * `Essentials::parse(std::span<const std::byte>)` for the protobuf form.
   */
  explicit Essentials(const std::string &serialized);

  [[nodiscard]] bool is_set() const { return _is_set; }

  [[nodiscard]] const EssentialsFields &fields() const { return _fields; }

  /*!
   * Mutable field access. Reaching for this implicitly marks the packet as set.
   */
  EssentialsFields &fields_mut()
  {
    _is_set = true;
    return _fields;
  }

  /*!
   * Parse the protobuf wire format (ADR-0005). Returns the populated packet
   * on success or a `ParseError` on the first failure condition. Emits an
   * INFO tripwire log on success identifying the on-disk `format_version`.
   */
  [[nodiscard]] static std::expected<Essentials, ParseError>
    parse(std::span<const std::byte> bytes);

  /*!
   * Parse the legacy pipe-delimited wire form. Returns an unset Essentials
   * (`is_set() == false`) on malformed input; otherwise returns a packet with
   * `data_chksum` hex-decoded into raw bytes. The legacy reader is kept
   * indefinitely (DEV-6398 scope) so on-disk Service Files written before the
   * protobuf rollout remain readable.
   */
  [[nodiscard]] static Essentials parse_legacy(std::string_view serialized);

  /*!
   * Serialize the packet to the protobuf wire form (ADR-0005). Byte-deterministic
   * — protobuf emits fields in field-number order, which is stable across protoc
   * revisions for the same `.proto` schema.
   */
  [[nodiscard]] std::vector<std::byte> serialize_bytes() const;

  /*!
   * Serialize the packet to the legacy pipe-delimited wire form used by
   * pre-protobuf SIPI emissions. Retained for the Expand/Migrate window;
   * Phase 14 removes it.
   *
   * \deprecated Use `serialize_bytes()` for new emissions.
   */
  [[nodiscard]] std::string serialize() const;

  /*!
   * Hex-encode raw bytes using lowercase digits. The format-handler tripwire
   * branch in `SipiImage::readSource` compares `shttps::Hash::hash()` (hex
   * string) against `data_chksum` (raw bytes) via this helper. Phase 12
   * introduces `compute_pixel_hash` returning raw bytes and folds the
   * comparison back to raw-vs-raw; this helper goes away with it.
   */
  [[nodiscard]] static std::string to_hex(std::span<const std::byte> bytes);

  /*!
   * Inverse of `to_hex`. Returns an empty vector on a non-hex / odd-length
   * input — used by `parse_legacy` to lift the legacy hex `data_chksum` into
   * raw bytes.
   */
  [[nodiscard]] static std::vector<std::byte> from_hex(std::string_view hex);

  friend std::ostream &operator<<(std::ostream &os, const Essentials &rhs) { return os << rhs.serialize(); }
};

}// namespace Sipi

#endif
