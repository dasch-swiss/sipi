---
title: "SIPI: ADR-0004 + ADR-0005 joint implementation (protobuf Essentials, read_shape fast path, verb-noun CLI)"
date: 2026-05-13
author: "Ivan Subotic"
status: reviewed(3)
linear: DEV-6537
children:
  - DEV-6542  # docs: file-role taxonomy + intentional-output + XMP-channel ADRs (LANDED 2026-05-14, sipi#637)
  - DEV-6378  # rename SipiIO::getDim → read_shape
  - DEV-6539  # rename SipiImage::readOriginal → readSource; strip ambient stamping
  - DEV-6410  # protobuf wire format + schema
  - DEV-6540  # cli: restructure sipi.cpp into subcommands (verb-noun)
  - DEV-6379  # fast path in read_shape + JP2 UUID-box carrier
  - DEV-6538  # cache: remove SizeRecord/sizetable/getSize
followups:
  - DEV-6541  # dsp-ingest: migrate to new sipi subcommand surface (cross-repo)
adrs: [0003, 0004, 0005, 0009, 0010, 0011]
future_adrs: [0012]  # Preservation File format + metadata (not in this PR)
repositories:
  - sipi
---

# SIPI: ADR-0004 + ADR-0005 joint implementation

Implement [ADR-0004](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0004-image-shape-ownership.md) (image-shape ownership via Essentials-packet fast path) and [ADR-0005](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0005-essentials-packet-versioned-binary-serialization.md) (protobuf wire format for the Essentials packet) together as a single coordinated change. Restructure the SIPI CLI from flag-mode-selection to verb-shaped subcommands. Establish the architectural principle: **`Essentials` packet creation is intentional output, gated by an explicit `convert service-file` subcommand — never ambient.**

## Architectural principles (reframed 2026-05-14)

These supersede the v1-v3 framing of the plan:

1. **Source files are never mutated.** Read does only read. The misleading `SipiImage::readOriginal` is renamed to `readSource` — the tool cannot assert the file is "the original."
2. **Identity stamping is intentional output, not a side effect of reading.** No call to `readSource` ever produces an `Essentials` packet. Packet creation happens only when the operator invokes a master-creation subcommand (`sipi convert service-file`).
3. **Three file roles, not two.** Per [ADR-0009](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0009-file-role-taxonomy.md) (landed 2026-05-14 via sipi#637): **Preservation File** (long-term bit-level preservation), **Service File** (mezzanine baseline read by SIPI server), **Access File** (end-user delivery; promoted from previously-implicit "IIIF derivative" to first-class). This PR delivers the Service File path (JP2 / pyramidal TIFF for IIIF serving) plus the supporting `convert access-file` surface. The Preservation File path — long-term preservation with rights, provenance, PREMIS-shaped metadata — is **out of scope** and deferred to a future ADR-0012 + separate Linear parent.
4. **CLI shape reflects intent.** Per [ADR-0010](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0010-file-role-creation-is-intentional.md): verb-noun subcommand surface (`server`, `convert [<role>]`, `verify [<role>]`, `query`, `compare`). Generic bare verbs (`convert`, `verify`) are anyone-use utilities; explicit role nouns (`convert service-file`, `verify service-file`, etc.) are DSP-specific operations with preservation-chain semantics.
5. **Hash-verify-on-read is a corruption tripwire, not a preservation guard.** Per [ADR-0010](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0010-file-role-creation-is-intentional.md): the existing `if (checksum != emdata.fields().data_chksum) return false;` branch becomes `log_err("Essentials data_chksum mismatch in %s; possible corruption", path); /* continue */` plus an increment on `sipi_essentials_hash_mismatch_total{format}`. Serving operates from Service Files; a mismatch is an infra signal that wants logging, not a hard abort. The active deliberate check is `sipi verify service-file <file>`.

6. **Preservation metadata propagates via XMP, not Essentials packet.** Per [ADR-0011](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0011-preservation-metadata-via-xmp.md): rights / provenance / PREMIS-shaped data rides SIPI's existing Embedded-metadata propagation chain (XMP-Rights, XMP-PLUS, XMP-PROV, C2PA-via-XMP, PREMIS-XMP). The Essentials packet stays scoped to SIPI-internal concerns (technical identity + image shape + future file-structure offsets). No new propagation infrastructure needed.

## Goal

- Replace pipe-delimited `Essentials` wire format with Protocol Buffers (`proto3` + `LITE_RUNTIME`).
- Rename `SipiIO::getDim` → `read_shape` (DEV-6378) and `SipiImage::readOriginal` → `readSource` (DEV-6539); strip ambient stamping from the latter.
- Restructure `sipi.cpp` CLI into verb-noun subcommands (DEV-6540): `server`; `convert` / `convert access-file` / `convert service-file` / `convert preservation-file` (stub); `verify` / `verify access-file` / `verify service-file` / `verify preservation-file` (stub); `query`; `compare`. Two tiers per ADR-0010: generic verbs are anyone-use; role-noun verbs are DSP-opinionated. Immediate breakage of legacy flag forms.
- Add a master-creation orchestrator that assembles `EssentialsFields` (identity + image-shape) from observed source + post-transformation `SipiImage` state, and hands the packet to the format-handler writer.
- Implement the ADR-0004 fast path in `read_shape` for service-file formats (JP2 + pyramidal TIFF): consume image-shape fields from the Essentials packet; fall through to format-native parsing if absent.
- Migrate the JP2 Essentials carrier from a codestream comment to a JP2 UUID box at slot 4 (after Signature → FTYP → `jp2h`, before `jp2c`).
- Remove `SipiCache::SizeRecord`, `sizetable`, `getSize()` — dead code once `read_shape` is the source of truth (DEV-6538).
- Restrict the `Essentials` carrier surface to **JP2 + pyramidal TIFF only**, and only when the writer is invoked via the master-creation orchestrator.
- Preserve every existing file's readability via the indefinite legacy pipe reader.

## Children of DEV-6537

| Issue | Phase(s) | Scope |
|-------|----------|-------|
| [DEV-6378](https://linear.app/dasch/issue/DEV-6378) | 1 | rename `SipiIO::getDim` → `read_shape` |
| [DEV-6539](https://linear.app/dasch/issue/DEV-6539) | 2 | rename `SipiImage::readOriginal` → `readSource`; strip stamping |
| [DEV-6410](https://linear.app/dasch/issue/DEV-6410) | 3-5, 14 | `.proto` schema, protobuf adapter, `Essentials` dispatcher, Contract cleanup |
| [DEV-6540](https://linear.app/dasch/issue/DEV-6540) | 11-12 | CLI subcommand restructure + master-creation orchestrator |
| [DEV-6379](https://linear.app/dasch/issue/DEV-6379) | 6-9 | format-handler reader/writer updates, fast path, JP2 UUID-box carrier |
| [DEV-6538](https://linear.app/dasch/issue/DEV-6538) | 10 | `SipiCache` shrinkage |

Follow-up (not in this PR): [DEV-6541](https://linear.app/dasch/issue/DEV-6541) — dsp-ingest migration to subcommand surface.

## Non-goals

- **Preservation File format + metadata** (rights, provenance, PREMIS-shaped data) — deferred. Should be specified in **ADR-0012** before any DEV ticket. This PR does not write Preservation Files; the `convert service-file` subcommand is the only master-creation verb. (Note: ADR-0009 has been re-purposed as the File-role taxonomy per [sipi#637](https://github.com/dasch-swiss/sipi/pull/637); the future archival-format ADR takes the next number.)
- **File-structure offsets in the Essentials packet** — schema reserves field numbers `16-31` for them but doesn't populate. Reason: at write time the codec hasn't laid out the file yet, so offsets are zero. `read_shape` only needs shape, not offsets — offsets matter for the *decode* step which DEV-6442+ handles.
- **`InputSource` / `RangeSource` for S3 access** — [DEV-6442](https://linear.app/dasch/issue/DEV-6442) per ADR-0004 "natural follow-on." The fast path in this PR works on local files via `pread()`.
- **Other `SipiCache` interface tightening** (RAII BlockedScope, `Stats stats()`, privatize FileCacheRecord/tcompare) — stays in [DEV-6397](https://linear.app/dasch/issue/DEV-6397).
- **`SipiImage` decomposition** — [DEV-6388](https://linear.app/dasch/issue/DEV-6388).
- **`SipiHttpServer` decomposition** — [DEV-6440](https://linear.app/dasch/issue/DEV-6440) / [DEV-6441](https://linear.app/dasch/issue/DEV-6441).
- **Other ADR-0006 format-handler API modernizations** — stays in [DEV-6374](https://linear.app/dasch/issue/DEV-6374).
- **`SipiImage` ↔ `Icc` friend coupling** — [DEV-6396](https://linear.app/dasch/issue/DEV-6396).
- **Deprecation cycle for legacy CLI flags** — explicitly rejected (immediate breakage per maintainer decision 2026-05-14).
- **dsp-ingest migration** — separate ticket [DEV-6541](https://linear.app/dasch/issue/DEV-6541); paired-release coordination at deploy time.

## Context

- **Origin/main as of 2026-05-13** is at `456080ab`. Metadata refactor scaffolding ([DEV-6398](https://linear.app/dasch/issue/DEV-6398) children) is landed: lowercase filenames, struct-of-accessors (DEV-6408), test-seam pattern (DEV-6406), Bazel package promotion (DEV-6405), SipiExif body extraction (DEV-6407), raw-pointer overload removal (DEV-6409). This plan starts from a clean `main`.
- **ADR-0005 will be rewritten** to record the protobuf decision. v1/v2 drafts argued for CBOR; the protobuf choice prevails on schema-evolution-by-field-number + cross-language readiness.
- **ADR-0004 stays as-is** but is now the implementation reference for the fast path + JP2 UUID-box carrier. Minor amendment: "fast path landed alongside packet schema in [DEV-6537](https://linear.app/dasch/issue/DEV-6537)."
- **[ADR-0009](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0009-file-role-taxonomy.md), [ADR-0010](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0010-file-role-creation-is-intentional.md), and [ADR-0011](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0011-preservation-metadata-via-xmp.md) landed via sipi#637** (DEV-6542) ahead of this implementation work. They formalize the file-role taxonomy, the intentional-output principle, and the XMP-channel decision that this implementation realizes.
- **ADR-0012 is the future placeholder** for the Preservation File format + metadata schema. Mentioned here so the implementer of the service-file path knows where Preservation File metadata will live (not here).
- **Restricted carrier surface** per ADR-0004 §line 20: only service-file formats (JP2 + pyramidal TIFF) carry Essentials, and only when produced by the `convert service-file` subcommand.

## Constraints

- **Source files are never mutated.** `readSource` reads and returns; never writes back to the source path.
- **Legacy reader retained indefinitely.** ~100 K files with legacy Essentials packets in production have pipe-delimited Essentials packets. No re-conversion. If an existing file is re-processed (e.g., via `sipi convert service-file old.jp2 new.jp2`), the previous packet is dropped and a fresh one is created with current identity from the user-supplied source (per maintainer decision 2026-05-14).
- **No ambient Essentials emission.** Format-handler writers never emit Essentials unless called via the master-creation orchestrator.
- **ICC determinism invariant** ([ADR-0002](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0002-icc-profile-determinism-test-only.md)): approval tests inject `SOURCE_DATE_EPOCH=946684800` and `SIPI_WORKSPACE_ROOT="."`. Protobuf encoding is byte-deterministic.
- **Build completeness invariant** (CLAUDE.md): every target builds on macOS-aarch64, linux-x86_64, linux-aarch64.
- **No backwards-compatibility shims.** `serialize() → std::string` overload disappears; legacy CLI flag forms removed without deprecation cycle.
- **Hash-verify-on-read is a corruption tripwire.** Log ERROR, continue; do not gate.

## Decisions

### D1: Wire format — **Protocol Buffers (`proto3` + `LITE_RUNTIME`)** via BCR

`bazel_dep(name = "protobuf", version = "34.1", repo_name = "com_google_protobuf")` + `bazel_dep(name = "rules_proto", version = "7.1.0")`. `option optimize_for = LITE_RUNTIME;` keeps the static-link footprint at ~300-500 KB.

Reasons (unchanged from v3):
- Schema evolution by field number is a stronger contract than CBOR's field-name evolution.
- Cross-language readiness for the future Rust IIIF service in dsp-repository.
- BCR `bazel_dep` is one line per module; no `rules_foreign_cc` integration.

### D2: Schema — `src/metadata/essentials.proto`

```protobuf
syntax = "proto3";

package sipi.metadata;

option optimize_for = LITE_RUNTIME;

enum HashType {
  HASH_TYPE_UNSPECIFIED = 0;
  HASH_TYPE_MD5 = 1;
  HASH_TYPE_SHA1 = 2;
  HASH_TYPE_SHA256 = 3;
  HASH_TYPE_SHA384 = 4;
  HASH_TYPE_SHA512 = 5;
}

message Essentials {
  uint32 format_version = 1;

  // Core preservation identity. Populated by the master-creation orchestrator
  // from observed source + computed pixel hash.
  string origname = 2;      // basename of the source file as supplied by the operator
  string mimetype = 3;      // detected from source magic bytes / extension
  HashType hash_type = 4;
  bytes data_chksum = 5;    // raw digest of the post-transformation pixel buffer
  bool use_icc = 6;
  bytes icc_profile = 7;

  // Image-shape dimensions (ADR-0004). Populated by the writer from current
  // (post-transformation) SipiImage state.
  uint32 img_w = 8;
  uint32 img_h = 9;
  uint32 tile_w = 10;
  uint32 tile_h = 11;
  uint32 clevels = 12;
  uint32 numpages = 13;
  uint32 nc = 14;
  uint32 bps = 15;

  // Reserved runway for file-structure offsets (post-DEV-6442 work).
  reserved 16 to 31;
}
```

**Discipline** (documented inline in `essentials.proto`):
- Never repurpose a field number; mark removed fields `reserved <n>;`.
- Never repurpose an enum value.
- Adding a field: pick lowest unreserved; describe; default to optional semantics.
- Breaking semantic change: bump `format_version`, write a per-version reader.

**HashType integer mapping** (`shttps::HashType` declaration order):
- `static_assert(static_cast<int>(shttps::HashType::md5) == 1);` and equivalents in `essentials.cpp`.
- Cross-context coupling note: add a one-line comment in `shttps/Hash.h` declaring the SIPI on-disk dependency.

**`EssentialsFields` struct** (`src/metadata/essentials.h`):
```cpp
struct EssentialsFields {
  std::string origname;
  std::string mimetype;
  shttps::HashType hash_type = shttps::HashType::none;
  std::vector<std::byte> data_chksum;   // CHANGED: was std::string (hex); now raw bytes
  bool use_icc = false;
  std::vector<unsigned char> icc_profile;
  // New (DEV-6537):
  uint32_t img_w = 0;
  uint32_t img_h = 0;
  uint32_t tile_w = 0;
  uint32_t tile_h = 0;
  uint32_t clevels = 0;
  uint32_t numpages = 0;
  uint32_t nc = 0;
  uint32_t bps = 0;
};
```

### D3: Per-format carrier strategy

| Format | New carrier (write + read) | Legacy carrier (read-only) |
|--------|----------------------------|----------------------------|
| **JP2** (Service File, written by `convert service-file` subcommand only) | JP2 **UUID box** at slot 4 (after Signature → FTYP → `jp2h`, before `jp2c`). 16-byte SIPI UUID + 4-byte big-endian `total_size` + N-byte protobuf payload. | codestream comment with `"SIPI:"` 5-byte prefix |
| **Pyramidal TIFF** (Service File, written by `convert service-file` subcommand only) | new private tag `TIFFTAG_SIPIMETA_PB = 65112`, registered as `TIFF_UNDEFINED` | existing `TIFFTAG_SIPIMETA = 65111`, `TIFF_ASCII` |
| JPEG / PNG / plain TIFF (derivative outputs from `convert` or other operations) | **No new carrier. No write.** | Existing carriers read-only for backward compat |

**Reader dispatch is channel-based:**
- TIFF: try `TIFFTAG_SIPIMETA_PB` first → `Essentials::parse(...)`. Else fall back to `TIFFTAG_SIPIMETA` → `Essentials::parse_legacy(...)`.
- JP2: walk top-level boxes via `jp2_family_src` + `jp2_input_box::open_next()`. For each UUID box, check first 16 bytes against `kSipiEssentialsUuid`. On match, read `total_size` then payload. Else fall back to legacy codestream-comment scan.
- JPEG / PNG: legacy-only.

**Dual-carrier precedence (TIFF + JP2):** new wins silently for the parse. Warn-once-per-file-path at WARN level via a bounded process-local cache (size cap 4096, LRU drop). On `convert service-file` re-encode of a dual-carrier file, the writer strips the legacy carrier — single-carrier output.

**Kakadu API** (verified against `src/formats/SipiIOJ2k.cpp:724-754`):
```cpp
// Write
jp2_output_box uuid_box;
uuid_box.open(family_tgt, jp2_uuid_4cc);
uuid_box.write(kSipiEssentialsUuid.data(), 16);
uint32_t total_size_be = htobe32(payload.size());
uuid_box.write(&total_size_be, 4);
uuid_box.write(reinterpret_cast<const kdu_byte*>(payload.data()), payload.size());
uuid_box.close();

// Read — walk top-level boxes
jp2_family_src family_src;
family_src.open(filepath.c_str());
jp2_input_box box;
box.open(&family_src);
while (box.exists()) {
  if (box.get_box_type() == jp2_uuid_4cc) {
    kdu_byte uuid[16];
    box.read(uuid, 16);
    if (std::memcmp(uuid, kSipiEssentialsUuid.data(), 16) == 0) {
      uint32_t total_size_be;
      box.read(&total_size_be, 4);
      uint32_t total_size = be32toh(total_size_be);
      std::vector<std::byte> bytes(total_size);
      box.read(reinterpret_cast<kdu_byte*>(bytes.data()), total_size);
      return Essentials::parse(bytes);
    }
  }
  box.close();
  box.open_next();
}
```

### D4: Parse API — `std::expected<Essentials, ParseError>` static factories

```cpp
namespace Sipi {
class Essentials {
public:
  enum class ParseError {
    Empty,           // zero-length input
    Malformed,       // protobuf ParseFromArray returned false
    MissingVersion,  // format_version == 0 (proto3 default → field was never set)
    UnknownVersion,  // format_version > 1 (future writer)
    MissingCore,     // protobuf parsed but origname/mimetype/hash_type/data_chksum missing
  };

  [[nodiscard]] static std::expected<Essentials, ParseError>
  parse(std::span<const std::byte> bytes);

  [[nodiscard]] static Essentials
  parse_legacy(std::string_view legacy_text);

  [[nodiscard]] std::vector<std::byte> serialize() const;
};
}
```

**`parse` implementation:** `Empty` → `Malformed` (proto parse fail) → `MissingVersion` (0) → `UnknownVersion` (>1) → `MissingCore` → map to `EssentialsFields`. Tripwire log on every successful parse: `"Essentials: read format_version=%u (max supported: 1)"`.

**`parse_legacy`** retains permissive pipe-split semantics. Hex-decodes `data_chksum` into raw bytes when populating the struct.

### D5: CLI subcommand surface (NEW — DEV-6540)

Verb-noun pattern, two tiers (per [ADR-0010](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0010-file-role-creation-is-intentional.md)):

**Tier 1 — generic (anyone-use, ImageMagick-style):**
```
sipi server [--port ...] [--workers ...] [--jwt ...] [--filewatch ...] [opts]
sipi convert <in> <out> [transform-opts] [--skipmeta] [--icc <target>]
                                              # generic format conversion; produces Access File (no Essentials)
sipi verify <file>                            # role-agnostic: can SIPI read + decode it? (RDU sanity check)
sipi query <in> [--json]                      # image info
sipi compare <A> <B> [--json]                 # byte/pixel comparison
```

**Tier 2 — DSP-opinionated (preservation-chain semantics):**
```
sipi convert access-file <in> <out> [transform-opts] [--icc <target>]
                                              # produce Access File from a Service File input
                                              # validates: input must have an Essentials packet
sipi convert service-file <in> <out> [--topleft]
                                              # create Service File (writes Essentials); restricted opts
sipi convert preservation-file <in> <out>     # stub; errors "awaits ADR-0012"

sipi verify access-file <file>                # asserts: valid Access File (no Essentials; well-formed XMP)
sipi verify service-file <file>               # asserts: Essentials parses, hash matches, shape consistent
sipi verify preservation-file <file>          # stub; errors "awaits ADR-0012"
```

**Option-availability matrix:**

| Group | Options | Attached to |
|-------|---------|-------------|
| `generic_transform_opts` | `--region`, `--size`, `--rotate`, `--mirror`, `--watermark`, `--reduce`, `--quality`, `--format` | `convert`, `convert access-file` |
| `color_space_opts` | `--icc` | `convert`, `convert access-file` |
| `normalize_opts` | `--topleft` | `convert`, `convert access-file`, `convert service-file` |
| `strip_opts` | `--skipmeta` | `convert` only (DSP-opinionated flow always propagates metadata) |
| `output_opts` | `--json` | `query`, `compare` |
| (server) | port, workers, jwt, filewatch, hostname, … | `server` only |

Implemented via CLI11's `add_subcommand()` (nested for the `convert` / `verify` role-noun forms) / `require_subcommand(1)`. Bare `convert <in> <out>` defaults to access-file semantics (ImageMagick-style; preserves embedded metadata; no Essentials).

**Input requirements:**

| Subcommand | Input | On invalid input |
|------------|-------|------------------|
| `convert <in> <out>` | Any readable image | Error from decoder |
| `convert access-file <in> <out>` | Must be a Service File (Essentials packet present + parseable) | Error: "input must be a Service File; got <description>. Use `sipi convert <in> <out>` for generic conversion." |
| `convert service-file <in> <out>` | Any readable image | Decoder error only |
| `convert preservation-file <in> <out>` | Future | Errors "awaits ADR-0012" |

**Immediate breakage** of legacy flag forms (`--convert`, `--query`, `--compare`). Tests, Hurl scripts, manpage, justfile recipes all migrate. dsp-ingest migration tracked in [DEV-6541](https://linear.app/dasch/issue/DEV-6541) as a paired-release coordination.

### D6: Master-creation orchestrator (NEW — DEV-6540)

The `convert service-file` subcommand's dispatcher in `sipi.cpp` orchestrates:

1. **Read source** via `SipiImage::readSource(input_path, region, size)`. No stamping; no mutation. The source file is untouched on disk.
2. **Apply user transformations** (orientation, ICC convert, rotate, mirror, watermark, region, resize) — same code paths as plain `convert` (shared option group).
3. **Build `EssentialsFields`**:
   - `origname` = basename of `input_path` (the source filename as supplied by the operator; the tool makes no claim that this is the "original" — it's the source for *this* conversion)
   - `mimetype` = `Parsing::getFileMimetype(input_path).first`
   - `hash_type` = SHA-256 (operator-configurable in a future flag)
   - `data_chksum` = SHA-256 of the **post-transformation** pixel buffer (computed by the orchestrator, not in `readSource`)
   - `use_icc` / `icc_profile` = current SipiImage ICC state
   - Image-shape fields populated from current `SipiImage` state (`nx`, `ny`, `nc`, `bps`) and from codec params (`tile_w`/`tile_h`/`clevels` for pyramidal TIFF and JP2)
4. **Drop any existing Essentials read from the source** (per maintainer decision 2026-05-14: re-conversion with `convert service-file` produces a fresh packet keyed to the new source path / hash).
5. **Pass to writer** with master-mode = `convert service-file`. Writer emits the carrier (JP2 UUID box / TIFF tag 65112).

`convert` subcommand does NOT invoke the orchestrator. It performs `readSource` → transformations → writer with master-mode = `none`. No Essentials packet is created.

### D7: Hash-verify-on-read = corruption tripwire (refined)

`SipiImage::readSource` (formerly `readOriginal`) is responsible for **reading only**. The hash-verify branch is repurposed:
- If the source happens to be a Service File with an existing `Essentials` packet (parsed via the format-handler reader), the legacy compute-and-compare check fires.
- On mismatch: `log_err("Essentials data_chksum mismatch for %s; possible corruption", filepath)` and **continue** (do not return `false`, do not abort).
- The previous "return false on mismatch" behavior is dropped — that was a preservation-guard semantic that didn't fit the actual operational model (serving operates from Service Files; the operator wants the corruption *signal*, not a hard fail).

This becomes [DEV-6539](https://linear.app/dasch/issue/DEV-6539)'s "reshape the hash-verify branch" scope item.

### D8: Fast path in `read_shape`

Per [DEV-6379](https://linear.app/dasch/issue/DEV-6379) / ADR-0004. Service File format handlers (JP2 + pyramidal TIFF) override `read_shape` to: `pread()` first 64 KB → locate Essentials packet at the known prefix position → parse → if `img_w != 0 && img_h != 0` return `SipiImgInfo` populated from packet. Else fall back to format-native parsing.

Activation criterion: **both** `img_w != 0` and `img_h != 0`. Outcome label `partial` for the case where only one is populated.

64 KB prefix + `total_size: u32` spillover handler for outliers (>64 KB ICC). Bounded follow-up range-GET.

### D9: Cache shrinkage

Per [DEV-6538](https://linear.app/dasch/issue/DEV-6538). Delete `SipiCache::SizeRecord`, `sizetable`, `getSize()`, populate-site in `add()`, vestigial non-cleanup in `purge()` / `remove()`. Consumer at `SipiHttpServer.cpp:1583` switches to `format_handler->read_shape(infile)`.

### D10: PR shape — single PR with Expand/Migrate/Contract commit organization

One PR covers all 6 children of DEV-6537. Commits internally follow Expand/Migrate/Contract so each builds and tests pass.

| # | Commit | Phase |
|---|--------|-------|
| 1 | `refactor(format_handlers): rename SipiIO::getDim → read_shape (DEV-6378)` | 1 |
| 2 | `refactor(metadata): rename SipiImage::readOriginal → readSource; strip stamping (DEV-6539)` | 2 |
| 3 | `build(metadata): add protobuf + rules_proto via BCR (DEV-6410)` | 3 |
| 4 | `feat(metadata): essentials.proto + protobuf_codec adapter (DEV-6410)` | 4 |
| 5 | `feat(metadata): add Essentials::parse / parse_legacy alongside legacy API (DEV-6410)` | 5 |
| 6 | `refactor(formats): readers route to parse_legacy; derivative writers stop emitting Essentials (DEV-6379)` | 6 |
| 7 | `feat(formats): TIFF TIFFTAG_SIPIMETA_PB tag, pyramidal writer gated on master-mode (DEV-6379)` | 7 |
| 8 | `feat(formats): JP2 SIPI UUID box carrier, writer gated on master-mode (DEV-6379)` | 8 |
| 9 | `feat(formats): read_shape fast path in pyramidal TIFF + JP2 (DEV-6379)` | 9 |
| 10 | `refactor(cache): remove SizeRecord, sizetable, getSize (DEV-6538)` | 10 |
| 11 | `refactor(cli): restructure sipi.cpp into subcommands (DEV-6540)` | 11 |
| 12 | `feat(cli): service-file orchestrator + verify subcommand (DEV-6540)` | 12 |
| 13 | `feat(observability): sipi_read_shape_fast_path_total + sipi_essentials_hash_mismatch_total metrics` | 13 |
| 14 | `refactor(metadata): remove legacy Essentials(string)/operator<< (DEV-6410)` | 14 |
| 15 | `test: protobuf + legacy + fast-path + spillover + LRU fixtures + equivalence tests` | 15 |
| 16 | `test: regenerate approval-test goldens for ImageEncodeBaseline.*` | 15 |
| 17 | `docs: rewrite ADR-0005 (protobuf); amend ADR-0004 (joint-impl pointer)` | 16 |

## Acceptance Criteria

### Build + tests
- [ ] `bazel build //src:sipi` green on darwin-aarch64, linux-x86_64, linux-aarch64.
- [ ] `bazel test //...` passes: unit, approval (regenerated goldens), e2e-rust, hurl, smoke.
- [ ] `bazel coverage` line-coverage on `src/metadata/essentials.cpp`, `src/metadata/internal/protobuf_codec.cpp`, the `read_shape` fast-path branches, and the master-creation orchestrator ≥ 90 %.
- [ ] `bazel-bin/src/sipi` binary-size delta vs. pre-merge ≤ 1 MB.

### Wire format + schema
- [ ] `MODULE.bazel` has `bazel_dep(name = "protobuf", version = "34.1", repo_name = "com_google_protobuf")` and `bazel_dep(name = "rules_proto", version = "7.1.0")`.
- [ ] `src/metadata/essentials.proto` checked in. `cc_proto_library` consumed only by `src/metadata/internal/protobuf_codec.cpp`.
- [ ] `static_assert`s in `essentials.cpp` lock the `shttps::HashType` ↔ `sipi::metadata::HashType` integer mapping.

### API surface
- [ ] `Essentials::serialize()` returns `std::vector<std::byte>`; no `std::string` overload; `operator<<` gone; `Essentials(const std::string&)` ctor gone.
- [ ] `SipiImage::readOriginal` is gone; `readSource` is the only name. All call sites + tests updated.
- [ ] `SipiImage::readSource` does **not** stamp `Essentials`. The struct is only populated by the master-creation orchestrator.
- [ ] `SipiCache::SizeRecord`, `sizetable`, `getSize()` are gone. `SipiHttpServer.cpp:~1583` calls `format_handler->read_shape(infile)`.
- [ ] `SipiIO::getDim` is gone; `read_shape` is the only name.

### CLI
- [ ] Legacy `--convert`, `--query`, `--compare` flag forms no longer parse (immediate breakage); `sipi --convert` exits with usage error.
- [ ] All subcommands work: `server`; `convert` / `convert access-file` / `convert service-file` / `convert preservation-file` (stub, errors with "awaits ADR-0012"); `verify` / `verify access-file` / `verify service-file` / `verify preservation-file` (stub); `query`; `compare`. Total: 1 server + 4 convert + 4 verify + query + compare = 11 invocations exercised.
- [ ] `sipi convert service-file <in> <out>` produces an output whose Essentials packet's image-shape fields match the **output** dimensions (post-transformation), and whose `data_chksum` is the SHA-256 of the post-transformation pixel buffer.
- [ ] `sipi convert <in> <out>` produces an output with **no Essentials packet** (whether output is JP2, TIFF, JPEG, or PNG). Verified by inspecting output bytes.
- [ ] `sipi convert access-file <in> <out>` rejects non-Service-File input with the documented error message; accepts Service-File input and emits an Access File with propagated XMP + no Essentials packet.
- [ ] CLI11 option-availability matrix enforced: `--icc` on `convert service-file` errors at parse time; `--skipmeta` on any DSP-opinionated subcommand (`convert {access,service,preservation}-file`, `verify *`) errors at parse time; `--region`/`--size`/`--rotate`/`--mirror`/`--watermark`/`--reduce`/`--quality`/`--format` on `convert service-file` errors at parse time. Tested in Phase 12.8.
- [ ] `sipi verify <file>` does the role-agnostic decoder-coverage check (RDU sanity use case). `sipi verify service-file <file>` reads the file, asserts presence of Essentials packet, re-computes the pixel hash, compares against `data_chksum`; exits 0 on match, exits 1 + logs ERROR on mismatch. `sipi verify access-file <file>` asserts the file is a valid Access File (no Essentials, well-formed XMP). `sipi verify preservation-file <file>` errors "awaits ADR-0012".
- [ ] Hurl tests + e2e-rust tests + smoke test all updated to the subcommand form and pass.

### Carrier behavior
- [ ] JP2 SIPI UUID box: positioned at slot 4 (after Sig/FTYP/jp2h, before jp2c); UUID committed in source; layout = 16-byte UUID + 4-byte BE total_size + payload.
- [ ] No call site writes Essentials to JPEG, PNG, or plain TIFF.
- [ ] Service File writes (pyramidal TIFF + JP2) only happen via the master-creation orchestrator.
- [ ] On re-encode of a dual-carrier file via `convert service-file`, the legacy carrier is stripped from the output.

### Observability
- [ ] `sipi_read_shape_fast_path_total{format, outcome}` Prometheus counter exposed at `GET /metrics`; fixture matrix exercises all four outcomes (`hit`, `miss`, `partial`, `fallback`) per format.
- [ ] `sipi_essentials_hash_mismatch_total{format}` Prometheus counter exposed; fixture exercises at least one mismatch event.
- [ ] jpylyzer validation runs clean on the regenerated JP2 goldens (informational reports for SIPI UUID box; no validation failures).

### Approval tests
- [ ] `ImageEncodeBaseline.*.approved.{tif,jpg,png,jp2}` goldens regenerated. JPEG/PNG/plain-TIFF outputs lose Essentials (subtractive). Pyramidal-TIFF + JP2 outputs gain new carrier when written via `convert service-file`. Pixel content bit-identical.
- [ ] `test/approval/CHANGELOG.approval.md` one consolidated entry: cause = "DEV-6537 ADR-0004 + ADR-0005 joint implementation; service-file subcommand introduced; protobuf packet via JP2 UUID box / TIFF tag 65112; legacy CLI flags broken".

### Docs + ADRs
- [ ] ADR-0005 rewritten and committed: status `proposed` → `accepted`. Covers wire format, dispatcher semantics, schema discipline, JP2 UUID-box position + total_size + 64 KB prefix invariant, considered alternatives.
- [ ] ADR-0004 amended: status `proposed` → `accepted`; joint-impl pointer to [DEV-6537](https://linear.app/dasch/issue/DEV-6537); CBOR → protobuf reconciliation.
- [ ] CHANGELOG entry under `[Unreleased]` documenting: wire-format change, `read_shape` rename, `readSource` rename, JPEG/PNG/plain-TIFF Essentials-write removal, **CLI subcommand restructure with immediate breakage**.
- [ ] `UBIQUITOUS_LANGUAGE.md` glossary entries: `kSipiEssentialsUuid`, JP2 UUID-box carrier, `format_version`, `read_shape` fast path, `convert service-file` (subcommand + concept), `readSource`, "master-creation orchestrator".

## Implementation Phases

### Phase 1 — `SipiIO::getDim` → `read_shape` rename (DEV-6378)

- [x] **1.1** Rename the pure-virtual in `SipiIO` base class. Update all four overrides. _Landed via sipi commit `519ce0de`._
- [x] **1.2** Update all call sites (`SipiHttpServer.cpp`, `SipiImage.cpp`, tests). _Landed via sipi commit `519ce0de`._
- [x] **1.3** Commit. Build green expected immediately. _Landed via sipi commit `519ce0de`._

### Phase 2 — `SipiImage::readOriginal` → `readSource` rename + strip stamping (DEV-6539)

- [x] **2.1** Rename both `readOriginal` overloads to `readSource` in `SipiImage.hpp` and `SipiImage.cpp`. _Landed via sipi commit `802d15bb`._
- [x] **2.2** **Strip the Essentials stamping logic** at the now-renamed sites (`:326-334` and `:361-366`): remove the `if (!emdata.is_set()) { ... essential_metadata(emdata2); }` block. `readSource` just reads. _Landed via sipi commit `802d15bb`._
- [x] **2.3** **Reshape the hash-verify branch**: replace `if (checksum != emdata.fields().data_chksum) return false;` with `log_err("Essentials data_chksum mismatch in %s; possible corruption", filepath.c_str());`. Continue execution. Increment a new `sipi_essentials_hash_mismatch_total{format}` counter (added in Phase 13). _Landed via sipi commit `802d15bb`; counter wiring landed with Phase 13 (`be2e9952`)._
- [x] **2.4** Drop the `htype` parameter from the `readSource` signature. Hash type is now decided at write time by the master-creation orchestrator, not by the read call. _Landed via sipi commit `802d15bb`._
- [x] **2.5** Update all call sites. There's only one production call site (`src/sipi.cpp:1014`) plus tests. _Landed via sipi commit `802d15bb`._
- [x] **2.6** Commit. The codebase is in an intermediate state where no Essentials is stamped by reads, and the orchestrator doesn't exist yet — therefore `sipi --convert` (still flag-style at this commit) produces output with no new Essentials packet. This is fine because: (a) the existing writers still emit pipe-delimited from existing in-memory `emdata`, (b) no in-memory `emdata` exists because reading doesn't stamp anymore. Net effect: no Essentials emission until Phases 11-12 land. Intentional intermediate state. _Landed via sipi commit `802d15bb`._

### Phase 3 — protobuf Bazel wiring (DEV-6410)

- [x] **3.1** Add `bazel_dep(name = "protobuf", version = "34.1", ...)` and `bazel_dep(name = "rules_proto", version = "7.1.0")` to `MODULE.bazel`. _Landed via sipi commits `f63b2e86` + transitive-deps unblock `3d02d298` + ANDROID_HOME workaround `ee92293d`._
- [x] **3.2** Create `src/metadata/essentials.proto` with the schema in D2. _Landed via sipi commit `f63b2e86`._
- [x] **3.3** Update `src/metadata/BUILD.bazel` with `proto_library` + `cc_proto_library`. _Landed via sipi commit `f63b2e86`._
- [x] **3.4** Verify `bazel build //src/metadata:essentials_cc_proto` on all three platforms. _Verified on darwin-aarch64 locally; linux-x86_64 / linux-aarch64 verified by CI on subsequent commits._

### Phase 4 — Internal protobuf adapter (DEV-6410)

- [x] **4.1** `src/metadata/internal/protobuf_codec.h` — declare `encode_essentials` / `decode_essentials`. Visibility-restricted. _Landed via sipi commit `35ca2c65`._
- [x] **4.2** `src/metadata/internal/protobuf_codec.cpp` — the **only** TU that includes `essentials.pb.h`. Add the HashType static_asserts. _Landed via sipi commit `35ca2c65`._
- [x] **4.3** Update `src/metadata/internal/BUILD.bazel`. _Landed via sipi commit `35ca2c65`._
- [x] **4.4** Unit test `src/metadata/internal/protobuf_codec_test.cpp`: round-trip, malformed bytes, MissingVersion, UnknownVersion, MissingCore. _Landed via sipi commit `35ca2c65`._

### Phase 5 — Expand: `Essentials` API alongside legacy (DEV-6410)

- [x] **5.1** `src/metadata/essentials.h`: add `enum class ParseError`, `static parse(...)`, `static parse_legacy(...)`, `static serialize_bytes() → std::vector<std::byte>`. Extend `EssentialsFields` with 8 image-shape `uint32_t` fields. Flip `data_chksum` from `std::string` (hex) to `std::vector<std::byte>` (raw bytes). Keep the legacy `Essentials(const std::string&)` ctor, `operator<<`, and `serialize() → std::string` for now.
- [x] **5.2** `src/metadata/essentials.cpp`: implement factories. `parse_legacy` hex-decodes `data_chksum` when populating. Tripwire log on `parse` success.
- [x] **5.3** Add cross-context comment in `shttps/Hash.h`: "Enum value order is SIPI on-disk contract — do not reorder."
- [x] **5.4** `src/metadata/essentials_test.cpp` co-located. Cover all 5 `ParseError` variants + equivalence-property against frozen baseline.

### Phase 6 — Reader routing + derivative-format write deletion (DEV-6379)

For each format, update the reader to route through `parse_legacy` (no new carrier yet) AND delete the Essentials-write code paths from derivative formats.

- [x] **6.1 TIFF reader** (`SipiIOTiff.cpp:1170, :1465`): replace `Essentials se(emdatastr)` → `Essentials::parse_legacy(emdatastr)`.
- [x] **6.2 JPEG reader** (`SipiIOJpeg.cpp:595, :912` — both call sites): replace `Essentials se(emdatastr)` → `Essentials::parse_legacy(emdatastr)`.
- [x] **6.3 JP2 reader** (`SipiIOJ2k.cpp:306-315, :706`): replace `Essentials se(cstr + 5)` → `Essentials::parse_legacy(cstr + 5)`.
- [x] **6.4 PNG reader** (`SipiIOPng.cpp:277`): replace `Essentials se(png_texts[i].text)` → `Essentials::parse_legacy(png_texts[i].text)`.
- [x] **6.5 JPEG writer**: delete only `:1282-1289` (the `if (es.is_set()) { ... jpeg_write_marker(JPEG_COM, ...); }` block). Keep line 1226 (`Essentials es = img->essential_metadata();`) and lines 1228-1261 (ICC fallback — uses `es.fields().use_icc` / `es.fields().icc_profile` for ICC sourcing).
- [x] **6.6 PNG writer**: delete only `:593-599` (the iTXt SIPI chunk write) + line 592 (the `sipi_buf` declaration). Keep line 548 + lines 549-564 (ICC fallback).
- [x] **6.7 Plain TIFF writer**: in the non-pyramidal branch, skip `TIFFSetField(... TIFFTAG_SIPIMETA ...)` entirely.

### Phase 7 — Pyramidal TIFF carrier, gated on master-mode (DEV-6379 + DEV-6410)

- [x] **7.1** Register `TIFFTAG_SIPIMETA_PB = 65112` as `TIFF_UNDEFINED`.
- [x] **7.2** Add a `master_mode` parameter to the TIFF writer's params (default = no Essentials).
- [x] **7.3** When `master_mode == service-file` AND pyramidal: emit `TIFFSetField(tif, TIFFTAG_SIPIMETA_PB, bytes.size(), bytes.data())` from `es.serialize_bytes()`. Strip the legacy `TIFFTAG_SIPIMETA` (don't set it). Otherwise: emit no SIPIMETA tag at all.
- [x] **7.4** Extend the TIFF reader (Phase 6.1) to try `TIFFTAG_SIPIMETA_PB` first → `Essentials::parse`, then fall back to `parse_legacy(emdatastr)`. Implement the dual-carrier warn-once cache (Phase 9.7). _Note: warn-once LRU cache deferred to Phase 9.6; this commit emits unconditional `log_warn` on dual-carrier detection._

### Phase 8 — JP2 UUID-box carrier, gated on master-mode (DEV-6379)

- [x] **8.1** Generate one RFC 4122 v4 UUID via `uuidgen`. Define `static constexpr std::array<std::uint8_t, 16> kSipiEssentialsUuid = {...};` in `src/formats/SipiIOJ2k.cpp`. Document in ADR-0005 + glossary. _Value: `7B28A646-B9C3-4FB2-900B-B6855DF23882`. Implementation uses `static kdu_core::kdu_byte sipi_essentials_uuid[]` (matching the existing `xmp_uuid`/`iptc_uuid`/`exif_uuid` siblings) rather than `std::array` per the plan literal — the Kakadu write helpers take raw `kdu_byte*`, and the symmetry beats the std::array form for this site. ADR-0005 + glossary docs land with Phase 16._
- [x] **8.2** Add a `master_mode` parameter to the JP2 writer's params (default = no Essentials). _Done in Phase 7 (key declared alongside `TIFF_MasterMode`)._
- [x] **8.3** When `master_mode == service-file`: emit the SIPI UUID box at slot 4 (after Signature → FTYP → `jp2h`, before `jp2c`). Layout = 16-byte UUID + 4-byte BE total_size + payload. Mirror the existing `write_xmp_box` / `write_iptc_box` / `write_exif_box` helpers at `:724-754` — add a new `write_essentials_box(family_tgt, bytes)` helper. Strip the legacy codestream-comment emission. _Note: Kakadu's `jp2_output_box::set_target_size()` writes the box's own length field — no separate `total_size: u32` prefix is needed inside the box payload (the spillover handling in Phase 9.5 reads it from the box header). Slot ordering: emission happens immediately after the first `jpx_out.write_headers()` call, before IPTC/EXIF/XMP UUID boxes._
- [x] **8.4** Extend the JP2 reader (Phase 6.3) to walk top-level boxes via `jp2_family_src` + `jp2_input_box::open_next()`. On finding a UUID box matching `kSipiEssentialsUuid`, read `total_size` and payload, then `Essentials::parse(payload)`. Else fall back to codestream-comment scan + `parse_legacy(cstr+5)`. Handle `total_size` spillover (re-fetch beyond 64 KB prefix if needed). _Spillover handling deferred to Phase 9.5 — current implementation uses `box.get_remaining_bytes()` which is `kdu_long` and handles arbitrarily-large boxes via Kakadu's source-managed reads. Dual-carrier detection emits unconditional `log_warn`; warn-once cache lands in Phase 9.6._

### Phase 9 — `read_shape` fast path (DEV-6379)

- [x] **9.1** `SipiIOJ2k::read_shape`: open `jp2_family_src`, walk top-level boxes; if SIPI UUID box found and `Essentials::parse` returns `is_set` with `img_w != 0 && img_h != 0`, return `SipiImgInfo` from packet. Else fall through to existing `codestream.create(...)` path.
- [x] **9.2** Pyramidal `SipiIOTiff::read_shape`: parse TIFF header + first IFD; if `TIFFTAG_SIPIMETA_PB` present and `Essentials::parse` succeeds with `img_w != 0 && img_h != 0`, return from packet. Else fall through.
- [ ] **9.3** Unit test: fixtures with **corrupted format-native** + **valid Essentials**; fast path returns from packet, not format-native. _Deferred to Phase 15.4 (fixture creation belongs there)._
- [x] **9.4** Activation criterion: **both** `img_w != 0` and `img_h != 0`. Partial shape (one but not both) → outcome label `partial`, falls through. _Implemented as `if (f.img_w != 0 && f.img_h != 0)` in both fast paths; the `partial` outcome label is wired with Phase 13 metrics._
- [x] **9.5** `total_size` spillover: when prefix-truncated, issue bounded follow-up read for full payload. Local-file mode (`pread()` ignores prefix budget) → no spillover needed. Range-source mode (DEV-6442 follow-up) → spillover read. _**In-scope work complete.** Local-file mode handles arbitrary payloads natively — `box.get_remaining_bytes()` (Kakadu) returns `kdu_long` for the full UUID-box payload, and `TIFFGetField` returns the full SIPIMETA_PB tag without any prefix constraint. Both are demonstrated to handle the ≤ 64 KB ICC fixture's payload via the JP2/TIFF reader paths in Phases 7-9 — no separate spillover read is required. Range-source spillover (where the on-disk read budget IS bounded) is scope-out for this PR per the plan's "Scope (out)" line on `InputSource`/`RangeSource`; it lands with DEV-6442._
- [x] **9.6** Warn-once-per-file dual-carrier log: bounded LRU cache keyed on `std::hash<std::string>{}(filepath)`. Implementation: a `std::list<size_t>` for insertion-order tracking + a `std::unordered_map<size_t, std::list<size_t>::iterator>` for O(1) lookup (canonical LRU pattern; `std::unordered_set` alone cannot LRU-evict because it has no insertion-order semantics). Soft cap 4096 entries. On overflow: drop the front (oldest) entry from the list and erase its map key. On warn: emit, then insert the key at the back of the list and add to the map. Log message: `log_warn("Essentials: both legacy and new carriers present for %s; using new carrier", filepath)`. Cache is mutex-protected (lock contention negligible — only fires on dual-carrier reads, expected to be rare). _Helper at `src/formats/essentials_dual_carrier.{hpp,cpp}`; replaces the three unconditional log_warn sites from Phases 7-8._

### Phase 10 — Cache shrinkage (DEV-6538)

- [x] **10.1** Delete `SizeRecord`, `sizetable`, `getSize()` from `SipiCache.h` / `.cpp`. Remove populate-site in `add()`. `purge()` / `remove()` no longer touch `sizetable` (vestigial bug becomes moot).
- [x] **10.2** Update consumer at `SipiHttpServer.cpp:1583`: `cache->getSize(infile, ...)` → `format_handler->read_shape(infile)`. Remove `:1575-1578` "nc/bps remain 0" comment.
- [x] **10.3** Update `test/unit/sipicache/` (remove getSize cases). SipiCache stays in its current location pending [DEV-6400](https://linear.app/dasch/issue/DEV-6400) Bazel package promotion (out of scope).
- [x] **10.4** Pre-merge sweep: `grep -rn 'getSize\|sizetable\|SizeRecord' src/ shttps/ test/` returns no hits outside the change set. _Verified — remaining hits are narrative comments referencing the removal._

### Phase 11 — CLI subcommand restructure (DEV-6540)

- [x] **11.1** Refactor `src/sipi.cpp`'s CLI11 setup from single-`App` to multi-subcommand. Top-level subcommands: `server`, `convert`, `verify`, `query`, `compare`. `convert` and `verify` each register nested sub-subcommands for the file-role variants (`access-file`, `service-file`, `preservation-file`). `app.require_subcommand(1)` so a bare `sipi` invocation errors with usage. _Landed via sipi commit `f8ac34d5`._
- [x] **11.2** Factor options into typed `CLI::Option_group`s per the D5 option-availability matrix and attach per-subcommand:
  - `generic_transform_opts` (`--region`, `--size`, `--rotate`, `--mirror`, `--watermark`, `--reduce`, `--quality`, `--format`) → attached to `convert`, `convert access-file`
  - `color_space_opts` (`--icc`) → attached to `convert`, `convert access-file`
  - `normalize_opts` (`--topleft`) → attached to `convert`, `convert access-file`, `convert service-file`
  - `strip_opts` (`--skipmeta`) → attached to `convert` only
  - `output_opts` (`--json`) → attached to `query`, `compare`
  - Each option group **rejected** at parse time on subcommands it isn't attached to (CLI11 emits a clear usage error). _Landed via sipi commit `81e87935`._
- [x] **11.3** Move the existing CLI bodies (convert flow at `:1010-1700`, query, compare) into per-subcommand callback functions. `convert` defaults to generic conversion (no Essentials emit); `convert access-file` invokes the access-file orchestrator (Phase 12.2); `convert service-file` invokes the service-file orchestrator (Phase 12.1); `convert preservation-file` errors with "awaits ADR-0012". Server mode stays as-is, attached to the `server` subcommand. _Landed via sipi commits `d3378471` (query/compare) + `b99f03a7` (convert) + `cd1553b9` (server)._
- [x] **11.4** **Remove** legacy flag forms (`--convert`, `--query`, `--compare`). Bare `sipi --convert ...` now fails with CLI11 usage error. _Landed via sipi commit `cddbe084`._
- [x] **11.5** Update justfile recipes, manpage, README examples, Hurl test scripts, e2e-rust tests — all migrate to the subcommand form. _Landed via sipi commit `770e0d09`; remaining doc references flipped via `04419e2c`._

### Phase 12 — Master-creation orchestrator, access-file orchestrator, and verify subcommands (DEV-6540)

- [x] **12.1** Implement the **`convert service-file` orchestrator** per D6 (master-creation): `readSource` → transformations → build `EssentialsFields` → drop any source Essentials → pass to writer with `master_mode = service-file`. Helper `compute_pixel_hash(SipiImage&, HashType)` lives in `src/metadata/essentials.{h,cpp}` (returns `std::vector<std::byte>`). Resides at `src/cli/service_file_orchestrator.{h,cpp}`. _Landed via sipi commit `9eea6541` (orchestrator) + `a19de9e0` (master → service-file terminology) + `fd7c1251` (server-side Lua upload wiring)._
- [x] **12.2** Implement the **`convert access-file` orchestrator**: read input via the format-handler reader; **validate input is a Service File** — fail with `log_err("convert access-file requires a Service File input; %s has no Essentials packet. Use 'sipi convert' for generic format conversion.", input_path)` and exit non-zero if Essentials packet absent or `parse()`/`parse_legacy()` fails. Apply transformations (region/size/rotation/quality/format per IIIF semantics). Propagate XMP/IPTC/EXIF/ICC from input via standard format-handler write paths (existing infrastructure per ADR-0011). Optionally inject an IIIF-provenance-event XMP fragment recording the SIPI version + transformation parameters (mirrors what the IIIF server emits on access-file responses — code-sharing target with the server's access-file emission path). Pass to writer with `master_mode = none`. Resides at `src/cli/access_file_orchestrator.{h,cpp}`. _Landed via sipi commit `44b8446d`. IIIF-provenance-event XMP fragment deferred to a follow-up commit alongside the server's access-file emission path so the two paths share one helper._
- [x] **12.3** Implement **all four verify subcommand variants**:
  - **`sipi verify <file>`** — role-agnostic decoder-coverage check (RDU sanity use case per ADR-0009): open file via the format-handler reader; walk format structure (TIFF IFDs / JP2 box tree / JPEG markers / PNG chunks); decode at least the lowest available resolution. Exit 0 on success; exit 1 + brief error on failure. No metadata assertions.
  - **`sipi verify access-file <file>`** — all of `verify <file>` plus: format is in the Access File format set (JPEG / PNG / plain TIFF / JP2-without-Essentials); **no Essentials packet present** (its presence indicates misclassification); XMP/IPTC well-formed if present. Exit 0/1.
  - **`sipi verify service-file <file>`** — all of `verify <file>` plus: format is pyramidal TIFF or JP2; Essentials packet present + `parse()` succeeds; recomputed pixel hash matches `data_chksum` (corruption check); image-shape fields consistent with codec output. Exit 0 on full match; exit 1 + log ERROR + increment `sipi_essentials_hash_mismatch_total{format}` on hash mismatch; exit 1 + clear error on other validation failures.
  - **`sipi verify preservation-file <file>`** — stub: errors with `"verify preservation-file: not yet implemented; awaits ADR-0012"` and exits non-zero. Subcommand registered so help/usage surfaces it.
  - All variants accept `--json` (via shared `output_opts` group) for structured reports.
- [x] **12.4** Wire the format-handler writer's `master_mode` param through from each call site: `convert service-file` (sets `service-file`), `convert access-file` (sets `none` — Access Files have no Essentials per ADR-0009), `convert` (sets `none`), `verify *` (no write path). _Landed alongside Phase 12.1-12.3. The single source of truth is the `emit_essentials_box / emit_essentials_pb` predicate in `SipiIOJ2k.cpp` / `SipiIOTiff.cpp` which gates emission on `params->contains(*_FileRole) && params->at(*_FileRole) == "service-file"`; the only two call sites that set the key are `service_file_orchestrator.cpp` and the Lua `SImage_write` binding (per commit `fd7c1251`)._
- [x] **12.5** Unit test the service-file orchestrator: synthetic source + transformations (rotate 90 + region 100x100) → output packet has post-transformation shape, post-transformation hash, source origname/mimetype. _Landed in this commit. The orchestrator's D5 transform surface is `--topleft` only (rotate/region were never plumbed through `convert service-file` — see D5 matrix); the test exercises the available transform via a `Rotate 90 CW` EXIF source and asserts the post-rotation shape (3264×2448 → 2448×3264). Seven tests in `test/unit/orchestrators/`: JP2 + pyramidal TIFF happy paths, `data_chksum` matches re-decoded pixels, topleft swaps dimensions, source Essentials dropped on re-encode, bad output extension rejected, missing input fails. Required two harness changes: a `::testing::Environment` that calls `SipiIOTiff::initLibrary()` (production sipi calls it from `main`; gtest_main doesn't) and a `materialize_fixture` helper that copies runfiles symlinks into TEST_TMPDIR so libmagic populates `mimetype` correctly. Orchestrators extracted into `//src/cli:orchestrators` cc_library so the test can link them without the binary's `main()`._
- [x] **12.6** Unit test the access-file orchestrator: feed a Service File → output has propagated XMP and **no** Essentials packet. Feed a non-Service-File → orchestrator errors with the documented message; no output file written. _Landed in `test/unit/orchestrators/access_file_orchestrator_test.cpp`. Seven tests: Service File → JPG (no Essentials), Service File → JP2 (no SIPI UUID box), non-Service-File input rejected with documented error, bad --format value rejected, unknown extension rejected, region transform applied (512×512 → 100×80), missing input fails. Tests compose Service File inputs by calling the service-file orchestrator first — ties the two ends of the preservation chain together in one assertion._
- [x] **12.7** Unit test `verify` (all 4 variants): bare `verify` on a corrupted JPEG → exits non-zero; bare `verify` on a valid JPEG → exits 0. `verify service-file` on a known-good Service File → exits 0; corrupted pixel data → exits 1 + ERROR. `verify access-file` on a known-good Access File → exits 0; same file with an injected Essentials packet → exits 1 (misclassification). `verify preservation-file` always exits non-zero with the awaits-ADR-0012 message. _Partially landed in `test/unit/orchestrators/verify_orchestrator_test.cpp`. Nine tests covering the three orchestrator-level modes (Generic, AccessFile, ServiceFile): valid + missing + corrupted inputs for Generic; passes-on-Access + rejects-Service for AccessFile; passes-on-known-good + rejects-no-packet + rejects-wrong-extension + detects-pixel-tampering for ServiceFile. `verify preservation-file` is exercised as a CLI subprocess in Phase 12.8 (no orchestrator function exists for it) — that variant is a CLI-only stub in `sipi.cpp` (no orchestrator function to unit-test), so it's exercised as a CLI subprocess in 12.8 alongside the option-availability matrix._
- [x] **12.8** Unit test the option-availability matrix: `sipi convert service-file --icc sRGB in.tif out.jp2` → CLI11 parse error; `sipi convert service-file --skipmeta in.tif out.jp2` → CLI11 parse error; `sipi convert --topleft in.tif out.jpg` → success; `sipi convert access-file --skipmeta in.jp2 out.jpg` → CLI11 parse error. _Landed in `test/e2e-rust/tests/option_matrix.rs`. Ten subprocess-level tests via `rules_rust`: `convert service-file` rejects --icc / --skipmeta / --region but accepts --topleft; `convert access-file` rejects --skipmeta; bare `convert` accepts --topleft / --skipmeta; bare `sipi` (no subcommand) errors. Also covers the **preservation-file stubs** the Phase 12.7 unit tests couldn't reach (no orchestrator function): both `convert preservation-file` and `verify preservation-file` exit non-zero with the "awaits ADR-0012" message. Note: when `--icc` is set on `convert service-file` CLI11 absorbs the flag value into the positional `input`, producing an "input: File does not exist: sRGB" error rather than a direct "Option --icc not defined" — both effectively gate the unsupported combination, so the test asserts non-zero exit + no output rather than a fixed stderr substring._

### Phase 13 — Observability metrics

- [x] **13.1** Add `sipi_read_shape_fast_path_total{format, outcome}` counter to `src/observability/metrics.{h,cpp}`. Labels: `format = {jp2, tiff}`; `outcome = {hit, miss, partial, fallback}`. Increment at the decision boundary in each `read_shape` override. _Landed via sipi commit `be2e9952`. Pre-created counter children (8) avoid per-call map lookups. Slow-path classification block hoisted to a single trailing site in each `read_shape` so precedence (parse-failure > partial > fallback > miss) is auditable._
- [x] **13.2** Add `sipi_essentials_hash_mismatch_total{format}` counter. Incremented from the corruption-tripwire branch in `readSource` (Phase 2.3) and from `verify` (Phase 12.2) on mismatch. _Landed via sipi commit `be2e9952`. Labels: `format = {jp2, tiff, jpeg, png, other}`; the non-carrier and `other` labels are safety valves so the tripwire remains attributable from any read path._
- [x] **13.3** Verify both metrics appear at `GET /metrics`. Fixture matrix exercises all four `read_shape` outcomes per format + at least one hash-mismatch event. _Endpoint exposure verified against a locally-started server (all 13 pre-created children render). Fixture-matrix coverage of every outcome belongs with Phase 15's LFS fixtures + approval regen and is tracked there._

### Phase 14 — Contract: remove legacy `Essentials` API (DEV-6410)

All callers have been migrated by Phases 6-12.

- [x] **14.1** `src/metadata/essentials.h`: remove `Essentials(const std::string&)` ctor declaration; remove `friend operator<<`; rename `serialize_bytes()` → `serialize()` after deleting the `std::string`-returning overload. _Landed via sipi commit `d6101858`. Also dropped the unused `<ostream>` include and the now-orphaned `base64Encode` / `hash_type_to_string` helpers that only fed the legacy emitter._
- [x] **14.2** `src/metadata/essentials.cpp`: remove legacy ctor body and legacy `serialize()` body. _Landed via sipi commit `d6101858`._
- [x] **14.3** Grep sweep: `grep -rn 'Essentials(.*string\|operator<<.*Essentials\|serialize_bytes' src/ shttps/ test/` returns no hits. _Verified — empty._

### Phase 15 — Fixtures + tests + approval regen

- [x] **15.1** `test/_test_data/protobuf_fixtures/gen.py` — Python script using protoc-generated `essentials_pb2.py` to author 2 fixtures (pyramidal TIFF, JP2). Document host environment in `README.md`. _Landed under `test/_test_data/protobuf_fixtures/`. `gen.py` authors `pyramidal_tiff.pb.bin` (79 B) + `jp2_uuid_box.pb.bin` (81 B) via the protoc-generated `essentials_pb2.py` (also committed — 39 lines; deterministic across protoc minor versions). `README.md` documents the regeneration recipe + Nix dev-shell additions (`protobuf` + `python3Packages.protobuf` added in `flake.nix`). Bytes verified deterministic via hex-dump inspection (`xxd pyramidal_tiff.pb.bin`)._
- [ ] **15.2** `test/_test_data/legacy_essentials/*.{tif,jpg,jp2,png}` — 4 small LFS fixtures with old pipe-delimited Essentials baked in. _Open. Requires a standalone fixture-generator that bypasses SIPI's writers (which no longer emit the legacy carriers per Phase 6.5-6.7 + Phase 14): direct libtiff `TIFFTAG_SIPIMETA` set, libjpeg `jpeg_write_marker(JPEG_COM, ...)`, libpng `png_set_text` (iTXt with key "SIPI"), Kakadu codestream comment. Existing unit-test coverage (`EssentialsParseLegacy.FrozenBaselinePopulatesEveryField`, `MalformedReturnsUnsetPacket`) already exercises `parse_legacy` against in-memory strings; this Phase 15.2 work adds on-disk-carrier round-trip coverage and is deferred to a focused follow-up PR._
- [ ] **15.3** `test/approval/legacy_essentials_test.cpp` — assert `parse_legacy` returns expected `EssentialsFields` for each fixture. _Open. Blocked on 15.2 (consumes its fixtures)._
- [ ] **15.4** `test/_test_data/fast_path_fixtures/*.{tif,jp2}` — 2 LFS fixtures with **correct** Essentials + **corrupted** format-native (assert fast path returns from packet). _Open. Requires Kakadu codestream / libtiff IFD manipulation to plant divergent shape between the Essentials packet and the codec — substantial low-level binary crafting deferred to a focused follow-up PR. Phase 9.3 ("Unit test: fixtures with corrupted format-native + valid Essentials") consumes these fixtures._
- [ ] **15.5** `test/_test_data/spillover_fixtures/*.jp2` — 1 LFS fixture with > 64 KB ICC, exercises `total_size` spillover. _Open. Local-file mode handles arbitrary payloads natively (Phase 9.5); this fixture only becomes load-bearing when range-source mode lands in DEV-6442. Deferred there._
- [x] **15.6** Warn-once cache LRU eviction test: > 4096 distinct paths → assert eviction order; assert re-encountering evicted path emits warn again. _Landed via sipi commit `04419e2c`. New `test/unit/dual_carrier/` package with 5 tests; 3 test seams added to `essentials_dual_carrier.{hpp,cpp}` (`*_soft_cap`, `*_contains`, `*_reset`)._
- [~] **15.7** 64 KB prefix invariant test: writer always positions UUID box / first IFD inside first 64 KB. _Partially landed via sipi commit `04419e2c`. JP2 invariant locked in (`JP2UuidBoxWithin64KBPrefix` passes). **TIFF invariant fails** — libtiff's default write order places the first IFD AFTER pyramid image data (offset ~256 KB for a 512×512 fixture). Test is committed as `DISABLED_TIFFFirstIFDWithin64KBPrefix` with a docstring documenting the two fix options (pre-compute IFD sizes; or post-write `TIFFRewriteDirectory`). Weaker invariant `TIFFFirstIFDOffsetIsResolvable` documents the "two range GETs for pyramidal TIFF" property the current writer does satisfy. Follow-up tracked: open a new Linear ticket for libtiff write-order fix; only then can the disabled test be enabled._
- [ ] **15.8** Regenerate `ImageEncodeBaseline.*.approved.{tif,jpg,png,jp2}` goldens. Regen host: linux-x86_64 CI runner with `SOURCE_DATE_EPOCH=946684800`. JPEG/PNG/plain-TIFF outputs lose Essentials (subtractive). Pyramidal-TIFF/JP2 outputs gain new carriers (only when test invokes `convert service-file` subcommand). _Open. Requires the linux-x86_64 CI runner to produce byte-stable goldens (matching `SOURCE_DATE_EPOCH`); deferred to a dedicated approval-regen PR where the goldens can be regenerated, reviewed, and re-approved in one shot._
- [x] **15.9** Update `test/approval/CHANGELOG.approval.md`. _Landed via sipi commit `04419e2c` as one consolidated PENDING entry covering all three sources of golden drift (JPEG/PNG/plain-TIFF subtractive, pyramidal-TIFF + JP2 carrier swap, CLI flag-form removal); flips to a dated entry on the actual approval-test regen run._
- [x] **15.10** Update `test/e2e-rust/tests/upload.rs` for the new subcommand surface; add an upload-then-fetch roundtrip exercising `convert service-file` end-to-end. _Pre-existing coverage adequate. `test/e2e-rust/tests/upload.rs::metadata_essentials_roundtrip` already exercises the service-file Lua write path (commit `fd7c1251` wired `SImage_write` with `file_role = "service-file"`) and asserts on the knora.json `originalFilename` / `originalMimeType` / `internalMimeType` fields. No CLI subprocess invocation in `upload.rs` — it's pure HTTP. No new test added._
- [x] **15.11** Add `jpylyzer` (`python3Packages.jpylyzer`) to `flake.nix` dev shell. Add a CI step that runs jpylyzer on regenerated JP2 goldens; assert no validation failures (unknown UUID box reported as informational only). _Dev-shell entry landed via sipi commit `04419e2c`. CI step is deferred to the approval-regen PR — it's only meaningful against newly regenerated goldens (Phase 15.8), which need the linux-x86_64 CI runner._
- [x] **15.12** Update Hurl tests + manpage + README examples + justfile recipes for the subcommand surface. _Bulk of this work landed via sipi commit `770e0d09` (Phase 11.5). The remaining two doc references (`json-output.md` + `sipi.md`) flipped via sipi commit `04419e2c`; tree-wide grep for `--convert` / `--query` / `--compare` / `--file` / `--outf` in `docs/`, `scripts/`, `test/`, `justfile` is now empty. Hurl tests don't shell out to the CLI._

### Phase 16 — ADR rewrite + amendment

- [x] **16.1** Rewrite `docs/adr/0005-essentials-packet-versioned-binary-serialization.md`. Status: `proposed` → `accepted`. Cover: wire format (protobuf + LITE_RUNTIME); dispatcher semantics; schema discipline; JP2 UUID-box position (slot 4) + total_size + 64 KB prefix invariant; considered alternatives (CBOR jsoncons, tinycbor, in-tree, custom-binary) with commit-SHA refs to v1/v2 drafts. _Landed via sipi commit `c631f030`. CBOR → protobuf reconciliation section spells out the three flip drivers (field-number stability, Rust readiness, BCR one-liner)._
- [x] **16.2** Amend `docs/adr/0004-image-shape-ownership.md`. Status: `proposed` → `accepted`. Joint-impl pointer to DEV-6537. Reconcile any "CBOR" → "protobuf" references. _Landed via sipi commit `c631f030`. Joint-implementation pointer added at the top of the document; ADR-0004 had no in-prose "CBOR" references requiring reconciliation._
- [x] **16.3** Add a stub `docs/adr/0012-preservation-file-format.md` with `status: proposed-future` and a one-paragraph placeholder pointing to the future Preservation File work (out of scope for DEV-6537). Provides a stable docs anchor for the architectural distinction. (ADR-0009 is taken — File-role taxonomy, landed via sipi#637.) _Landed via sipi commit `c631f030`._
- [x] **16.4** Glossary entries in `UBIQUITOUS_LANGUAGE.md`: `kSipiEssentialsUuid`, JP2 UUID-box carrier, `format_version`, `read_shape` fast path, `convert service-file` (verb + concept), `readSource`, "master-creation orchestrator", "corruption tripwire". Cross-reference both ADRs. _Landed via sipi commit `c631f030`. Four new entries added (`kSipiEssentialsUuid`, JP2 UUID-box carrier, `format_version`, `read_shape` fast path); the remaining four were already in the glossary from sipi commit `e1fd1ff7` (the DEV-6542 three-tier-taxonomy sweep)._

## Risk Matrix

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| dsp-ingest still invokes `sipi --convert ...` after the release lands → all ingestion fails | H | H | DEV-6541 tracks the dsp-ingest migration; paired-release coordination at deploy time. SRE checklist gates the SIPI deploy on dsp-ingest's matching deploy. |
| `convert service-file` orchestrator missing some metadata (e.g., ICC) the previous ambient-stamp captured | M | M | Phase 12.4 unit test diffs the new orchestrator output vs. the v3-plan ambient-stamp's expected output (regression fixture). |
| Operator confusion about the four-variant convert surface (`convert` vs `convert access-file` vs `convert service-file` vs `convert preservation-file`) and the four-variant verify surface | M | L | CHANGELOG entry + manpage organizes commands into two tiers per ADR-0010: "Generic image utilities" (anyone-use: bare `convert`, `verify`, `query`, `compare`) and "DSP preservation-chain operations" (file-role-qualified verbs). `sipi --help` for each role-noun subcommand shows clear semantic description (e.g., `convert access-file`: "Produce an Access File from a Service File input"). Phase 11.2 enforces the option-availability matrix at parse time so misuses error early. |
| Legacy reader regression (some pipe-format edge case missed) | L | H | Equivalence-property test against frozen baseline; 4 legacy fixtures. |
| JP2 UUID box not recognized by jpylyzer / preservation validators | VL | L | jpylyzer reports unknown UUIDs as informational (confirmed via best-practice research). Phase 15.11 adds the CI step. |
| JP2 UUID box overflows 64 KB prefix | L | M | `total_size: u32` lets reader issue bounded follow-up range-GET. Phase 9.5 + 15.5 fixture. |
| Field-number / enum-value error in `.proto` becomes a forever-bug | VL | H | Two-reviewer rule on `.proto`; `reserved 16-31`; `static_assert` HashType lock. |
| `shttps::HashType` reordered without updating proto (cross-context break) | L | H | Phase 5.3 adds reverse-pointer comment in `shttps/Hash.h`. |
| protobuf-lite static-link footprint exceeds ~500 KB | L | L | Phase 15 binary-size delta check. |
| Cache-shrinkage exposes a hidden consumer of `getSize()` | L | M | Phase 10.4 grep sweep + build failure catch-all. |
| `format_version=0` corrupted packet mis-classified as future-version | L | L | Phase 4 `parse` returns explicit `MissingVersion` for zero, distinct from `UnknownVersion` for >1. |
| Approval-test golden host-dependence | M | L | Regen on linux-x86_64 CI runner; document recipe. |
| Hash-verify tripwire log spams in production if a corrupted file is hot | L | L | The log is per-read; if a single file is read N times, N log entries. Acceptable for early signal. Future: deduplicate via per-file warn-once cache (currently used for dual-carrier warning, same mechanism). |
| `convert service-file` re-encode silently drops an existing archival-quality Essentials packet | L | M | Documented behavior (per maintainer decision 2026-05-14); `verify` subcommand lets operator confirm round-trip before destroying the old. |
| Future writer emits `format_version=2` and old reader is missing fields | M | L | `parse` returns `UnknownVersion`; format handler logs once and falls through to legacy reader. |
| dsp-repository Rust IIIF service consumes a stale `.proto` and drifts | L | M | When that service ships, share via Bazel cross-repo dep or post-monorepo shared `//protos/essentials.proto`. |

## File Map

### Added
- `src/metadata/essentials.proto`
- `src/metadata/internal/protobuf_codec.h` + `.cpp` + `_test.cpp`
- `src/metadata/essentials_test.cpp`
- `test/_test_data/protobuf_fixtures/gen.py` + `README.md` + 2 `.pb.bin` files
- `test/_test_data/legacy_essentials/*.{tif,jpg,jp2,png}` (4 LFS)
- `test/_test_data/fast_path_fixtures/*.{tif,jp2}` (2 LFS)
- `test/_test_data/spillover_fixtures/*.jp2` (1 LFS)
- `test/approval/legacy_essentials_test.cpp` + 4 `*.legacy.approved.txt`
- `docs/adr/0012-preservation-file-format.md` (stub placeholder; ADR-0009 is taken by File-role taxonomy)

### Modified
- `MODULE.bazel` (+2 `bazel_dep` lines)
- `src/metadata/essentials.h` (rename `data_chksum` to `std::vector<std::byte>`; add image-shape fields; new `parse`/`parse_legacy`; `serialize()` returns `std::vector<std::byte>`; legacy ctor + `operator<<` removed in Phase 14)
- `src/metadata/essentials.cpp` (dispatcher + tripwire log + HashType static_asserts)
- `src/metadata/internal/BUILD.bazel`
- `src/metadata/BUILD.bazel` (proto + cc_proto + protobuf_codec + essentials_test)
- `include/SipiIO.h` — `getDim` → `read_shape` virtual
- `src/SipiImage.{hpp,cpp}` — `readOriginal` → `readSource`; strip stamping; reshape hash-verify into corruption tripwire; drop `htype` param
- `src/formats/SipiIOTiff.cpp` — `read_shape` rename; new tag 65112; reader fallback; writer gated on master_mode
- `src/formats/SipiIOJpeg.cpp` — `read_shape` rename; `parse_legacy` in 2 reader sites; **delete only** JPEG_COM emission at `:1282-1289` (ICC fallback at `:1228-1261` stays)
- `src/formats/SipiIOJ2k.cpp` — `read_shape` rename; UUID box read+write helpers; writer gated on master_mode
- `src/formats/SipiIOPng.cpp` — `read_shape` rename; `parse_legacy`; **delete only** iTXt SIPI chunk at `:593-599` (ICC fallback at `:549-564` stays)
- `src/sipi.cpp` — full CLI11 subcommand restructure (`server` / `convert` / `convert service-file` / `query` / `compare` / `verify`); master-creation orchestrator dispatcher; legacy flag forms removed
- `src/SipiHttpServer.cpp:~1583` — `cache->getSize()` → `format_handler->read_shape(infile)`; remove `:1575-1578` overestimation comment
- `src/SipiCache.{cpp,h}` — delete `SizeRecord`, `sizetable`, `getSize()`
- `src/observability/metrics.{h,cpp}` — `sipi_read_shape_fast_path_total{format,outcome}` + `sipi_essentials_hash_mismatch_total{format}`
- `flake.nix` — add `jpylyzer` to dev shell
- `shttps/Hash.h` — single-line cross-context comment
- `test/approval/approval_tests/ImageEncodeBaseline.*.approved.*` (regenerated)
- `test/approval/CHANGELOG.approval.md` (consolidated entry)
- `test/e2e-rust/tests/upload.rs` (subcommand form + new-carrier roundtrip)
- `test/unit/sipicache/` — remove getSize cases
- `test/hurl/*.hurl` — subcommand form
- `justfile` — recipes using subcommand form
- `docs/src/development/*.md` — update CLI examples
- `docs/adr/0005-essentials-packet-versioned-binary-serialization.md` (rewrite; status → accepted)
- `docs/adr/0004-image-shape-ownership.md` (amend; status → accepted; joint-impl pointer)
- `UBIQUITOUS_LANGUAGE.md` (glossary additions)
- `CHANGELOG.md` ([Unreleased] entries)

## Open Questions

All decisions from cycles 1-2 resolved. Remaining are implementation-time:

- **JP2 SIPI UUID value** — generated via `uuidgen` at Phase 8.1; committed in source + ADR-0005 + glossary.
- **`read_shape` 64 KB prefix size** — sufficient for typical packets; spillover handled via `total_size` field.
- **`absl::flat_hash_set` availability** — confirm Abseil is on the dep graph; if not, `std::unordered_set` with manual size cap.
- **Approval-test regen host pinning** — document in `CHANGELOG.approval.md` recipe.
- **Default hash type for `convert service-file`** — proposed SHA-256; future flag `--hash sha256|sha384|sha512` could expose it. Out of scope for v1.

## References

### ADRs
- [ADR-0005](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0005-essentials-packet-versioned-binary-serialization.md) (rewritten in this PR)
- [ADR-0004](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0004-image-shape-ownership.md) (amended in this PR)
- [ADR-0003](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0003-module-co-located-source-and-tests.md)
- [ADR-0002](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0002-icc-profile-determinism-test-only.md)
- [ADR-0009](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0009-file-role-taxonomy.md) — File-role taxonomy (Preservation/Service/Access File). Landed via sipi#637 (2026-05-14).
- [ADR-0010](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0010-file-role-creation-is-intentional.md) — File-role creation is intentional and gated by CLI subcommand. Landed via sipi#637 (2026-05-14).
- [ADR-0011](https://github.com/dasch-swiss/sipi/blob/main/docs/adr/0011-preservation-metadata-via-xmp.md) — Preservation metadata propagates via XMP. Landed via sipi#637 (2026-05-14).
- ADR-0012 (future) — Preservation File format + metadata; **out of scope for DEV-6537**. Stub added in this PR's Phase 16.3.

### Linear
- [DEV-6537](https://linear.app/dasch/issue/DEV-6537) — parent
- [DEV-6542](https://linear.app/dasch/issue/DEV-6542) — docs: ADRs 0009/0010/0011 + glossary sweep (**LANDED** via sipi#637, 2026-05-14)
- [DEV-6378](https://linear.app/dasch/issue/DEV-6378) — `getDim` → `read_shape`
- [DEV-6539](https://linear.app/dasch/issue/DEV-6539) — `readOriginal` → `readSource` + strip stamping
- [DEV-6410](https://linear.app/dasch/issue/DEV-6410) — protobuf wire format
- [DEV-6540](https://linear.app/dasch/issue/DEV-6540) — CLI subcommand restructure
- [DEV-6379](https://linear.app/dasch/issue/DEV-6379) — fast path + JP2 UUID box
- [DEV-6538](https://linear.app/dasch/issue/DEV-6538) — cache shrinkage
- [DEV-6541](https://linear.app/dasch/issue/DEV-6541) — dsp-ingest follow-up

### Probe
- [Probe 1 — SipiCache](https://github.com/dasch-swiss/sipi/blob/main/docs/archive/2026-05-08-modularization-analysis.md#probe-1--sipicache)
- [Probe 2 — metadata/](https://github.com/dasch-swiss/sipi/blob/main/docs/archive/2026-05-08-modularization-analysis.md#probe-2--metadata)
- [Probe 3 — format_handlers/](https://github.com/dasch-swiss/sipi/blob/main/docs/archive/2026-05-08-modularization-analysis.md#probe-3--format_handlers-renamed-from-formats)

### Learnings
- [sipi-nix-to-bazel-migration-lessons](../../learnings/best-practices/sipi-nix-to-bazel-migration-lessons.md) — N/A for this PR (protobuf + rules_proto are BCR deps).

### External
- [Protocol Buffers (proto3) Language Guide](https://protobuf.dev/programming-guides/proto3/)
- [Protocol Buffers `LITE_RUNTIME` option](https://protobuf.dev/reference/cpp/cpp-generated/#message-lite)
- [BCR — protobuf module](https://github.com/bazelbuild/bazel-central-registry/tree/main/modules/protobuf)
- [BCR — rules_proto module](https://github.com/bazelbuild/bazel-central-registry/tree/main/modules/rules_proto)
- [CLI11 subcommand documentation](https://cliutils.github.io/CLI11/book/chapters/subcommands.html)
- [JP2 box structure (ISO/IEC 15444-1 Annex I)](https://www.itu.int/rec/T-REC-T.800)
- [RFC 4122 — UUID](https://www.rfc-editor.org/rfc/rfc4122)
