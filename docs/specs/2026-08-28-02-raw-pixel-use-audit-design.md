---
title: "Raw pixel-buffer and private-member access audit"
date: 2026-08-28
author: "Ivan Subotic"
status: draft
repositories: []
---

# Raw pixel-buffer and private-member access audit

Precondition audit for ADR-0007 (`docs/adr/0007-sipiimage-decomposition.md`, accepted).
The ADR proposes to replace the four format-handler `friend class` declarations on
`SipiImage` with a public mutator surface (`pixels_writable()` plus metadata setters).
This document inventories every site that actually relies on that friendship or on
direct access to `pixels`, so the proposed surface can be checked against real usage
rather than assumption. **No code changes accompany this document.**

## 1. Scope and method

Swept: `src/` (format handlers, `SipiImage.{h,cpp}`, FFI/Lua seam, CLI), `test/unit/`,
`test/approval/`, and every `*_benchmark.cpp`. Not swept: `src/server-rs`,
`src/scripting/rust` (Rust has no access to C++ private members; relevant only as a
*consumer of the public API*, confirmed clean below), docs.

Search patterns (ripgrep/grep, run from repo root):

```
grep -n "\.pixels\b\|->pixels\b\|pixels\.data\|pixels\[" src/formats/SipiIO{Tiff,J2k,Jpeg,Png}.cpp
grep -noE "img(->|\.)[A-Za-z_]+" <handler.cpp> | sort | uniq -c        # per-field access census
grep -noE "img(->|\.)<field>\b" <handler.cpp>                          # per-field line numbers
grep -rn --include='*.cpp' --include='*.h' --include='*.hpp' "\.pixels\b|->pixels\b" .
grep -n "^bool SipiIO<X>::\|^void SipiIO<X>::\|^SipiImgInfo SipiIO<X>::" <handler.cpp>  # function boundaries
```

Field-access line numbers were then bucketed into the enclosing function (by line
range) with a Python pass over the grep output, so each inventory row below is
`(handler, function, field) -> [line numbers]`, not a hand-picked sample. Five
citations were spot-checked afterward against the live file content (§6 confirms:
all five matched).

## 2. Inventory

### 2.1 Format handlers — the four friendships

`SipiImage.h:614-617` declares `friend class SipiIOTiff/SipiIOJ2k/SipiIOJpeg/SipiIOPng`.
Function line ranges used for bucketing:

| Handler | Function | Lines |
|---|---|---|
| `SipiIOTiff.cpp` | `read` / `read_shape` / `write_basic_tags` / `write` / `write_subfile` / `readExif` / `writeExif` / `separateToContig` | 928-1553 / 1554-1677 / 1678-1709 / 1710-2019 / 2020-2073 / 2074-2245 / 2246-2404 / 2405-2487 |
| `SipiIOJ2k.cpp` | `read` / `read_shape` / `write` | 264-819 / 820-1046 / 1047-1542 |
| `SipiIOJpeg.cpp` | `parse_photoshop` / `read` / `read_shape` / `write` | 375-461 / 462-836 / 837-983 / 984-1272 |
| `SipiIOPng.cpp` | `read` / `read_shape` / `write` | 132-341 / 342-449 / 450-624 |

#### `SipiIOTiff.cpp`

| Field | Function | Lines | R/W | What it does |
|---|---|---|---|---|
| `pixels` | `read` | 1372,1385,1387,1401-1409,1412,1456-1459,1464 | mutate | decode: palette expand (`img->pixels = std::move(inbuf)` then indexed palette lookup into a new `dataptr`, assigned back), CIELab a*/b* sign-fixup in place |
| `pixels` | `write` | 1757,1762,1785-1788,1794,1932 | read | `TIFFWriteScanline(tif, img->pixels.data() + …)`; CIELab sign-fixup read before write |
| `pixels` | `write_subfile` | 2045 | read | pyramid-level subsampling: `img.pixels.data()` copied into a reduced buffer |
| `pixels` | `separateToContig` | 2411,2423,2425,2437,2472,2481 | mutate | planar→contiguous repack (`img->pixels = std::move(tmp_v)`) and 1-bit bitonal packing |
| `nx`/`ny`/`nc`/`bps` | `read` | nx:951-1474 (22 sites), ny:966-1465 (13), nc:969-1507 (15), bps:972-1509 (21) | mutate+read | geometry set from TIFF tags, then reused to size/index the decode buffer |
| `nx`/`ny`/`nc`/`bps` | `write`/`write_basic_tags`/`write_subfile`/`separateToContig` | see full line lists in §"Reproduction" below | read | geometry read to emit TIFF tags and index the encode buffer |
| `es` (`ExtraSamples`) | `read` | 1037 | mutate | set from TIFF `ExtraSamples` tag |
| `es` | `write_basic_tags`/`write` | 1701,1703,1704,1814,1816,1817 | read | emitted as the TIFF `ExtraSamples` tag |
| `photo` | `read` | 980,982,997,1377,1413,1418,1485,1490,1507 | mutate+read | set from TIFF photometric tag; read to drive ICC/CMYK branches |
| `photo` | `write`/`write_basic_tags`/`separateToContig` | 1707,1753,1777,1821,2457,2468 | read | emitted as TIFF photometric tag; drives 1-bit-pack branch |
| `orientation` | `read` | 977 | mutate | set from TIFF orientation tag |
| `xmp`/`iptc` | `read` / `write` | xmp: 1155 / 1893,1895; iptc: 1129 / 1881,1883 | mutate / read | decode: parsed into `img->xmp`/`img->iptc`; encode: serialized out, gated by `skip_metadata` |
| `icc` | `read` | 1172,1252,1256,1417,1423,1429,1434,1441,1462,1477,1491 | mutate+read | ICC profile parsed from TIFF tag; read to decide CMYK/CIELab handling |
| `icc` | `write` | 1809,1810,1864,1870 | read | ICC bytes emitted via `Icc::iccBytes()` |
| `exif` | `read` | 1050-1118 (16 sites) | mutate | populated tag-by-tag from TIFF EXIF-adjacent tags, each guarded by `img->ensure_exif()` |
| `exif` | `write` | 1826-1953 (14 sites) | read | emitted tag-by-tag, gated by `skip_metadata` |
| `exif` | `readExif` (own method) | 2086-2225 (11 sites) | mutate | dedicated EXIF-blob parser (`readExif(SipiImage*, TIFF*, toff_t)`) |
| `exif` | `writeExif` (own method) | 2261-2380 (10 sites) | read | dedicated EXIF-blob writer |
| `essential_metadata` (public getter/setter, not privileged) | `read`/`write` | 1279,1284,1287 / 1863 | mutate/read | Essentials packet round-trip (already public API — listed for completeness, not a friendship dependency) |
| `skip_metadata` | `write` | 1826,1864,1881,1893 | read | gates which metadata blocks are emitted |
| `ensure_exif` (private method) | `read` | 1049,1053,1057,1061,1065,1069,1073,1077,1081,1099,1103,1117,1140 | call | lazily allocates `img->exif` before each tag write — a *private method call*, not just a field reach-in |

#### `SipiIOJ2k.cpp`

| Field | Function | Lines | R/W | What it does |
|---|---|---|---|---|
| `pixels` | `read` | 712,735,759,788,794 | mutate | `img->pixels = std::move(buffer8\|buffer16\|tmpbuf)` — three decode paths (8-bit, 16-bit, palette-then-8-bit) |
| `pixels` | `write` | 1495,1505,1511 | read | Kakadu stripe encode: `kdu_int16*`/`kdu_byte*` cast directly over `img->pixels.data()` at a `stripe_start` offset |
| `nx`/`ny`/`nc`/`bps` | `read` | nx 9, ny 5, nc 13, bps 9 sites | mutate+read | JP2 codestream geometry parsed into fields, reused for buffer sizing |
| `nx`/`ny`/`nc`/`bps` | `write` | nx 6, ny 10, nc 22, bps 7 sites | read | geometry read to drive Kakadu encode parameters and stripe loop |
| `es` | `read`/`write` | 563 / 1327,1381,1397,1424-1433 (12) | mutate/read | JP2 channel-definition box round-trip |
| `photo` | `read`/`write` | 20 sites / 1539 | mutate+read | colour-space box parsed; read repeatedly to select the Kakadu colour transform |
| `orientation` | `read` | 560 | mutate | set (no EXIF orientation box in JP2; likely a default/derived value — see §5) |
| `xmp`/`iptc`/`exif` | `read`/`write` | xmp 367/1466-1467; iptc 377/1450-1451; exif 386/1281-1459 (7) | mutate/read | UUID-box metadata round-trip |
| `essential_metadata` | `read`/`write` | 399,460 / 1308 | mutate/read | Essentials packet (fast-path shape read) |

#### `SipiIOJpeg.cpp`

| Field | Function | Lines | R/W | What it does |
|---|---|---|---|---|
| `pixels` | `read` | 749,755,781 | mutate | `img->pixels.assign(...)` sized buffer, then `memcpy` per scanline, then in-place CMYK/YCCK byte inversion loop (`255 - img->pixels[b]`) |
| `pixels` | `write` | 1264 | read | `row_pointer[0] = &img->pixels[cinfo.next_scanline * row_stride]` fed to libjpeg-turbo |
| `nx`/`ny`/`nc`/`bps` | `read`/`write` | ~10-14 sites each, split across both functions | mutate+read | JPEG SOF geometry parsed; reused for buffer sizing and libjpeg-turbo param setup |
| `photo` | `read`/`write` | 715-776 (6) / 1092,1146 | mutate+read | derived from JFIF/Adobe markers; drives colour-space branch on write |
| `orientation` | `parse_photoshop` / `read` | 433 / 576,605 | mutate | **line 433 spot-checked**: `if (img->exif->getValByKey(...)) { img->orientation = Orientation(ori); }` — orientation set from an already-parsed EXIF tag inside the Photoshop APP13/EXIF marker parser |
| `xmp`/`iptc`/`icc`/`exif` | `parse_photoshop`/`read`/`read_shape`/`write` | see full lists | mutate/read | Photoshop IRB (APP13), Adobe APP14, and EXIF/XMP marker parsing; `read_shape` (no full decode) still reaches `exif`/`xmp`/`essential_metadata` (922,941,913) for the fast-path shape read |
| `app14_transform` | `read` | 692,778 | mutate+read | the JPEG-only CMYK/YCCK polarity flag (`SipiImage.h:118-129`); set at decode from the Adobe APP14 marker, read immediately after to decide the inversion branch — this field is **removed** per the ADR ("App14_transform field removed"), not migrated to a public accessor |
| `essential_metadata` | `read`/`read_shape`/`write` | 590 / 913 / 1199,1256 | mutate/read | Essentials packet round-trip incl. the shape-only fast path |

#### `SipiIOPng.cpp`

| Field | Function | Lines | R/W | What it does |
|---|---|---|---|---|
| `pixels` | `read` | 305 | mutate | `img->pixels = std::move(buffer)` |
| `pixels` | `write` | 602,604 | read | `row_pointers[i] = img->pixels.data() + i*stride` (8-bit) or `+ 2*i*stride` (16-bit), fed to libpng |
| `nx`/`ny`/`nc`/`bps` | `read`/`write` | ~4-9 sites each | mutate+read | IHDR geometry parsed / emitted |
| `es` | `read`/`write` | 219,233,297 / 520,524 | mutate/read | tRNS/alpha-channel accounting |
| `photo` | `read` | 213,218,224,228,232 | mutate | PNG colour-type → `PhotometricInterpretation` mapping (write side has no direct `photo` reach-in — colour type is re-derived from `nc`/`es` at 518-533) |
| `orientation` | `read` | 189 | mutate | default-set (PNG has no orientation chunk in this codepath) |
| `xmp`/`iptc`/`icc`/`exif` | `read`/`write` | see full lists | mutate/read | eXIf/iTXt-XMP/iCCP chunk round-trip |
| `essential_metadata` | `read`/`write` | 283 / 549,592 | mutate/read | Essentials packet round-trip |

### 2.2 The `~12` processing methods (`src/SipiImage.cpp`)

| Method | Private state touched | Mutates image? |
|---|---|---|
| `convertYCC2RGB()` (414-472) | `bps`,`nc`,`nx`,`ny`,`pixels` | yes — full-buffer replace (`pixels = std::move(outbuf)`) |
| `convertToIcc()` (476-567) | `nc`,`nx`,`ny`,`bps`,`photo`,`icc`,`pixels` | yes — replaces `pixels`, `icc`, and can change `nc`/`bps`/`photo` |
| `removeChannel()` (572-713, called by `removeExtraSamples()`) | `nc`,`nx`,`ny`,`bps`,`es`,`photo`,`pixels` | yes — replaces `pixels`, decrements `nc`, erases an `es` entry |
| `crop(x,y,w,h)` / `crop(region)` (718-838) | `nx`,`ny`,`nc`,`bps`,`pixels` | yes — replaces `pixels`, shrinks `nx`/`ny` |
| `scaleFast`/`scaleMedium`/`scale` (926-1146) + private `bilinn` | `nx`,`ny`,`nc`,`bps`,`pixels` | yes — replaces `pixels`, sets `nx`/`ny` to the new size |
| `rotate()` (1151-~1378) | `nx`,`ny`,`nc`,`bps`,`pixels` | yes — mirror/rotate replaces `pixels` (dimensions swap for 90°/270°) |
| `set_topleft()` (1379-1410) | `orientation`,`exif` | yes — calls `rotate()` internally, then sets `orientation = TOPLEFT` and patches the EXIF tag if present |
| `to8bps()` (1414-1438) | `bps`,`pixels` | yes — replaces `pixels`, sets `bps = 8` |
| `toBitonal()` (1443-1479) | `photo` (via `convertToIcc` call), `pixels` | yes — in-place Floyd-Steinberg dither, rewrites `pixels` in place (no reallocation) |
| `add_watermark()` (1485-1526) | `nx`,`ny`,`nc`,`bps`,`pixels` | yes — in-place per-pixel blend, rewrites `pixels` in place |
| `operator-=`/`operator+=` (1533-1769) | `nx`,`ny`,`nc`,`bps`,`photo`,`pixels` (own + `rhs`'s) | yes on `*this`; read-only on `rhs` (same-class access to another instance, not friendship-based) |
| `operator-`/`operator+` (1651-1656, 1774-1779) | delegates to `-=`/`+=` on a copy | no (returns new value) |
| `operator==` (1783-1817) | `nx`,`ny`,`nc`,`bps`,`photo`,`pixels` (both instances) | no — pure comparison |
| `compare()` (1821-1847) | same as above | no |
| `maxPixelDelta()` (1851-~1900) | same as above | no |

All of the above are same-class access (`this->pixels` and `rhs.pixels` on two
`SipiImage` instances) — they need no friendship and are unaffected by the friend-class
removal; they become the free functions' problem only in the sense that the free
function needs equivalent access to *its* `const Image&`/`Image&` parameters, which the
proposed public surface must supply (§4).

### 2.3 FFI/Lua seam and CLI

`src/ffi/image_handle.cpp` (692 lines) is the **only** FFI file that touches
`SipiImage` methods, and every call is through the already-public API:
`readSource`/`read`/`getNx`/`getNy`/`getOrientation`/`crop`/`scale`/`rotate`/
`set_topleft`/`setOrientation`/`add_watermark`/`getExif`/`compute_pixel_hash`/
`getIcc`/`getNc`/`getBps`/`essential_metadata`/`write` (image_handle.cpp:123-623,
full line list above under §2.3 grep output). `src/ffi/serve_image.cpp` constructs
`SipiImage` values (lines 420, 489, 671) and passes them to `image_handle.cpp`'s
functions but does not call any method directly. Neither file, nor
`src/scripting/rust/bindings/image.rs` (Rust — cannot access C++ private members at
all), reaches into `pixels` or any other private/protected member. **The FFI/Lua seam
imposes no constraint beyond the existing public API.**

No CLI file (`src/cli/`, `src/cli-rs/`) references `pixels`, `getPixel`, or `setPixel`
at all (`grep` returned zero matches).

### 2.4 Tests and benchmarks

- `test/unit/sipiimage/sipiimage.cpp` (635 lines): constructs `Sipi::SipiImage`
  instances and calls only public methods (`read`/`readSource`/`write`/`getNx`/
  comparisons via `operator==`). Zero occurrences of `.pixels`, `->pixels`,
  `getPixel`, or `setPixel`.
- `test/unit/fixtures/generate_malformed_images.cpp`: manipulates raw file bytes on
  disk, not `SipiImage` instances — irrelevant to this audit.
- `test/approval/image_encode_baseline_test.cpp`, `test/approval/metadata_golden_test.cpp`:
  zero occurrences of `.pixels`/`->pixels`.
- `src/process_benchmark.cpp`: exercises `read`, `scaleFast`/`scaleMedium`/`scale`,
  `rotate`, `crop`, `to8bps`, `convertToIcc`, `getNx`/`getNy`/`getNc`/`getBps` — all
  public API, all hot-path (§5).
- `src/formats/decode_benchmark.cpp`, `src/formats/encode_benchmark.cpp`: exercise
  `read`/`write`/`getNx`/`getNy`/`getNc` only.

**No test or benchmark in the repository accesses a private/protected `SipiImage`
member.** Tests and benchmarks impose no constraint on the API beyond what is already
public — they are not a driver for `pixels_writable()` or any new accessor.

A repo-wide grep (`grep -rn --include='*.cpp' --include='*.h' --include='*.hpp'
"\.pixels\b|->pixels\b" .`, excluding `SipiImage.{h,cpp}` and the four handlers)
returned **zero** matches, confirming the four format handlers are the sole
non-member consumers of `pixels`.

## 3. Access patterns that emerge

Collapsing the inventory (§2) into distinct needs:

1. **Bulk-replace the whole pixel buffer after a transformation.** By far the most
   common pattern across both handlers (decode) and processing methods (crop, scale,
   rotate, colour conversion, channel removal, bit-depth reduction): compute a new
   `std::vector<byte>` sized by `checked_buf_size_or_throw(...)`, then
   `pixels = std::move(newbuf)`, then update `nx`/`ny`/`nc`/`bps` to match.
2. **Bulk-write into a pre-sized buffer during decode**, one scanline/tile/strip at
   a time (TIFF `separateToContig`, PNG `row_pointers`, JPEG `memcpy` per scanline,
   J2K Kakadu stripe writes). Needs a raw writable `byte*`/size, not a
   `std::vector<byte>&` API (the C libraries want a pointer + stride).
3. **Bulk-read the whole buffer during encode**, handed to a C library as a raw
   pointer (`TIFFWriteScanline`, libpng `row_pointers`, libjpeg-turbo
   `row_pointer`, Kakadu stripe pull). Needs a raw readable `const byte*`, not
   iterator-based access.
4. **In-place per-pixel/per-byte transform without resizing** — `toBitonal`'s
   Floyd-Steinberg dither, `add_watermark`'s blend, the TIFF CMYK sign-fixup, the
   JPEG CMYK/YCCK inversion loop. Needs indexed mutable access at existing size, no
   reallocation.
5. **Read-only whole-buffer scan for comparison/hashing** — `compare`,
   `maxPixelDelta`, `operator==`, `compute_pixel_hash`. All same-class (`this` +
   `rhs`), needing no privilege beyond what a free function's `const Image&`
   parameter already has.
6. **Geometry/photometric field mutation at decode, read at encode** — `nx`, `ny`,
   `nc`, `bps`, `es`, `photo`, `orientation` are *written* once per decode (parsed
   from the container format) and *read* many times at encode (to drive the codec's
   own tag/parameter emission) and inside every processing method (to index
   `pixels`). This is not a `pixels`-shaped problem — it is "does the format handler
   get a mutable `Image&` at all", independent of pixel-buffer mechanics.
7. **Metadata setters at decode, getters at encode** — `xmp`, `icc`, `iptc`, `exif`
   follow the same write-once-at-decode / read-at-encode shape as pattern 6, plus
   the already-public `essential_metadata()` getter/setter and `skip_metadata`
   (currently private, read-only at encode — no handler ever sets it).
8. **Private-method delegation** — `ensure_exif()` (TIFF only): a handler calls back
   into `SipiImage`'s own lazy-init logic rather than duplicating it. This is *not*
   a data-access pattern; it is behavior delegation, and removing the friendship
   removes the handler's ability to call it at all.
9. **A field that should not survive the decomposition at all** —
   `app14_transform`: mutated and read within the same `SipiIOJpeg::read()` call,
   never touched anywhere else. The ADR already calls for its removal (JPEG inverts
   CMYK/YCCK at decode time internally); it needs no accessor.

## 4. Proposed minimal public surface

Given patterns 1-9, a single `pixels_writable()` returning `std::vector<byte>&` (the
ADR's starting suggestion) is **insufficient for patterns 2-4 as stated, and too
coarse for pattern 1** if it is the *only* primitive: handing out a mutable reference
to the whole `vector` re-opens exactly what the friendships closed — a caller with
`pixels_writable()` can resize, reassign, or corrupt the buffer relative to
`nx`/`ny`/`nc`/`bps` with no consistency check, which is precisely the invariant the
current friend-only access (all of it inside `SipiImage`'s own trusted format
handlers) currently protects implicitly by convention, not by type.

Proposed surface on `Image` (patterns from §3 in parentheses):

| Member | Signature (sketch) | Serves |
|---|---|---|
| `set_pixels(std::vector<byte>&& buf, size_t nx, size_t ny, size_t nc, size_t bps)` | replaces buffer + geometry atomically, size-checked against `nx*ny*nc*bps/8` | pattern 1 (all bulk-replace call sites: decode assigns, crop/scale/rotate/colour/channel/bit-depth) |
| `pixels_writable()` → `std::span<byte>` (not `vector&`) | mutable view at current size, no resize capability | patterns 2 and 4 (decode-time scanline/strip writes; in-place dither/watermark/inversion) |
| `pixels() const` → `std::span<const byte>` | read-only view at current size | pattern 3 (encode-time raw-pointer handoff to libtiff/libpng/libjpeg-turbo/Kakadu) and pattern 5 (comparison/hash, though same-class access already covers `Image`'s own processing/comparison code) |
| existing public getters (`getNx`/`getNy`/`getNc`/`getBps`/`getPhoto`/`getOrientation`) + new `setNx`/`setNy`/... or a combined `set_geometry(nx,ny,nc,bps,photo)` | | pattern 6 |
| existing `getExif`/`getIcc`/`getXmp` + new setters (`set_exif`, `set_icc`, `set_xmp`, `set_iptc`) mirroring `essential_metadata()`'s existing getter/setter shape | | pattern 7 |
| **no accessor** — `skip_metadata` stays read-only via the existing `setSkipMetadata()` (already public); `orientation` needs a public mutable path (`setOrientation` already exists) | | pattern 6/7 |
| **removed entirely**, per ADR | — | pattern 9 (`app14_transform` folds into the JPEG handler's own decode-local state, not `Image`) |
| **no equivalent needed** | — | pattern 8: `ensure_exif()`'s lazy-init behavior moves into `set_exif`/an `Exif&` accessor that lazily allocates, so the handler never needs to call a private method — it just calls the public setter, which does the lazy-init internally |

`std::span` over `std::vector<byte>&` for the writable/readable accessors is the load-
bearing choice: a `vector&` lets a caller call `.resize()`/`.clear()`/`= std::move(...)`
and silently desynchronize the buffer from `nx*ny*nc*bps`, which is exactly the
footgun the ADR is trying to close. `set_pixels()` is the only path that changes size,
and it takes the geometry alongside the buffer so `Image` can validate
`buf.size() == nx*ny*nc*bps/8` in one place instead of trusting every call site (today,
trusting the four handlers implicitly).

**Can the four friendships be removed with this surface? Yes, with two caveats:**

1. `ensure_exif()` must become part of a public `set_exif`/lazy-`Exif&` accessor (or
   the handler duplicates the four-line lazy-init itself) — not a big change, but it
   is not "just add `pixels_writable()`", contrary to what a narrow reading of the ADR
   suggests.
2. `app14_transform` is removed per the ADR's own stated plan (pattern 9) — it is not
   migrated to any accessor, and this is a real (if small) behavior-preservation risk
   flagged in §5.

With `set_pixels`/`pixels_writable`/`pixels`/geometry-setters/metadata-setters in
place, no site in §2.1 needs anything the friendships uniquely provide beyond those
two caveats.

## 5. Risks and open questions

- **Hot-path accessor inlining.** `pixels_writable()`/`pixels()` returning
  `std::span<byte>` from a trivial inline getter should compile to the same
  pointer-plus-length the current `pixels.data()` calls produce, in an optimized
  build — but this is asserted, not verified, by this document. The processing
  methods' inner loops (`convertYCC2RGB`, `crop`, `scale*`, `rotate`, `removeChannel`,
  `toBitonal`, `add_watermark` — all in `src/SipiImage.cpp`, all inside triple-nested
  `for` loops over `nx*ny*nc`) and the four handlers' `read`/`write` scanline loops are
  exactly the sites ADR-0007's Consequence 5 claims are unaffected by the earlier
  `vector<byte>` swap; the *decomposition* (moving these into
  free functions taking `Image&`/`const Image&`) has not yet been benchmarked. Per
  `CLAUDE.md`'s hot-path rule, `process_benchmark.cpp`/`decode_benchmark.cpp`/
  `encode_benchmark.cpp` must be re-run before/after the actual free-function
  extraction — this audit does not substitute for that.
- **`j2k`'s `orientation` and PNG's `orientation` are set from nothing (no orientation
  box/chunk in either container).** `SipiIOJ2k.cpp:560` and `SipiIOPng.cpp:189` both
  write `img->orientation` unconditionally at decode — almost certainly a
  default-value assignment (`TOPLEFT`), but this document could not confirm the exact
  value without reading the surrounding branch logic in full; worth a one-line check
  before the setter-based migration locks in the same default.
- **`app14_transform` removal changes the shape of `Image`, not just its access
  control.** The ADR frames this as "already decided," but it is the one field in
  this inventory whose *replacement* is not "add an accessor" but "restructure the
  JPEG handler to fully resolve CMYK/YCCK polarity before constructing/returning the
  `Image`." That is a real code change bundled into what otherwise reads as a
  mechanical friend-to-public-API swap — flagging it so it is not accidentally
  scoped out of the eventual decomposition PR.
- **`PhotometricInterpretation` on PNG write side is re-derived from `nc`/`es`
  (`SipiIOPng.cpp:518-533`) rather than read from `img->photo` directly** — confirms
  pattern 6 holds for PNG's read side but the write side already avoids touching
  `photo`, so PNG's write function needs one fewer setter/getter dependency than the
  other three handlers. Not a risk, just an asymmetry worth preserving (don't force a
  `getPhoto()` call into PNG's write path that isn't there today).
- **The five-line-number spot-check (§6) covered TIFF/J2K/JPEG/PNG write-path
  `pixels` sites plus one `SipiImage.cpp` processing-method call** — all five were
  correct on inspection. No broader verification pass (e.g., re-deriving every one of
  the ~400 individual line citations by hand) was performed; the bucketing script's
  output was spot-checked, not exhaustively hand-verified line-by-line.

## 6. Spot-check record

Five `file:line` citations were picked and independently re-read from the live files
before this document was finalized:

| Citation | Claimed content | Verified |
|---|---|---|
| `src/formats/SipiIOTiff.cpp:1932` | `TIFFWriteScanline(tif, img->pixels.data() + i * img->nc * img->nx * (img->bps / 8), (int)i, 0);` | correct |
| `src/formats/SipiIOJ2k.cpp:1511` | `kdu_byte *buf = img->pixels.data() + stripe_start * img->nc * img->nx;` | correct |
| `src/formats/SipiIOJpeg.cpp:433` | `if (img->exif->getValByKey("Exif.Image.Orientation", ori)) { img->orientation = Orientation(ori); }` | correct |
| `src/formats/SipiIOPng.cpp:602` | `for (size_t i = 0; i < img->ny; i++) { row_pointers[i] = (img->pixels.data() + i * img->nx * img->nc); }` | correct |
| `src/SipiImage.cpp:1447` | `convertToIcc(Icc(icc_GRAY_D50), 8);` (inside `toBitonal()`) | correct |

All five matched exactly; no citation in this document is known to be wrong.
