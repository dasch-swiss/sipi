#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["Pillow>=10.0"]
# ///
"""
One-shot fixture generator for JPEG test fixtures used by the heritage
JPEG read + CMYK APP14 regression tests.

Produces three fixtures under test/_test_data/images/jpeg/:

  1. cmyk/cmyk_photoshop_app14.jpg — CMYK baseline with an Adobe APP14 marker
     advertising transform=0 (Photoshop-style "Unknown / CMYK" — libjpeg-turbo
     outputs inverted CMYK that needs re-inversion).
  2. cmyk/cmyk_raw_no_app14.jpg — CMYK baseline **without** APP14 (raw CMYK;
     no inversion needed). Pinned as the negative test.
  3. malformed_xmp.jpg — 64x64 RGB JPEG with a deliberately corrupted APP1 XMP
     segment (valid JPEG envelope, XMP packet that fails to parse). Used by the
     F3 feature-contract test to prove that log_warn is routed to stderr under
     --json without breaking the single-document contract on stdout.

Also produces seven marker-parsing regression fixtures as a
sibling `../malformed/` directory (relative to the given output dir), see
`generate_malformed()` and test/_test_data/images/malformed/README.md for
each fixture's defect (DEV-6066, N1, N4, the parse_photoshop
even-padding pointer overshoot, DEV-7056's fatal-metadata-parse fixture,
DEV-7078's zero-tag-count ICC profile, and S2-14's oversized-Exif marker
guard).

Run (from the sipi repo root):

    uv run test/unit/fixtures/generate_jpeg_fixtures.py \
        test/_test_data/images/jpeg/

This script is committed alongside the generated .jpg files so the fixtures
are reproducible but it is NOT invoked by CI.
"""
from __future__ import annotations

import io
import pathlib
import sys
from dataclasses import dataclass

from PIL import Image


@dataclass(frozen=True)
class App14Spec:
    """Encoding of an Adobe APP14 marker segment.

    Layout: "Adobe\\0" identifier (6 bytes), version (2 bytes), flags0 (2
    bytes), flags1 (2 bytes), transform (1 byte) = 13 bytes of payload.
    """

    version: int = 100
    flags0: int = 0
    flags1: int = 0
    transform: int = 0  # 0 = Unknown/CMYK (Photoshop), 1 = YCbCr, 2 = YCCK


def _generate_checker(size: int = 128, block: int = 16) -> bytes:
    """Produce a 4-channel (CMYK) checker pattern."""
    out = bytearray()
    for y in range(size):
        for x in range(size):
            on = ((x // block) + (y // block)) % 2 == 0
            # In CMYK, a "dark" pixel has high C/M/Y/K; a light pixel has 0s.
            # We use alternating dark and light cells to produce visible banding
            # that makes inversion errors obvious.
            if on:
                out.extend((0xE0, 0x20, 0x40, 0x80))  # dark, tinted cyan-ish
            else:
                out.extend((0x10, 0x10, 0x10, 0x10))  # near-white
    return bytes(out)


def _encode_cmyk_jpeg(pixels: bytes, size: int = 128) -> bytes:
    """Encode a CMYK JPEG using Pillow. Pillow always emits APP14 with
    transform=0 for CMYK output (libjpeg convention), so the returned bytes
    are suitable for the "Photoshop / APP14" fixture.
    """
    img = Image.frombytes("CMYK", (size, size), pixels)
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def _strip_app14(jpeg_bytes: bytes) -> bytes:
    """Remove any APP14 (`FF EE`) marker segments from a JPEG byte stream.
    Returns a new JPEG byte stream with the same image data but no APP14.
    """
    out = bytearray()
    i = 0
    n = len(jpeg_bytes)
    while i < n:
        if jpeg_bytes[i] != 0xFF:
            out.append(jpeg_bytes[i])
            i += 1
            continue
        # SOI / EOI have no length field.
        marker = jpeg_bytes[i + 1]
        if marker == 0xD8 or marker == 0xD9:
            out.extend(jpeg_bytes[i : i + 2])
            i += 2
            continue
        # Start Of Scan (SOS) — after this marker the rest is entropy-coded data.
        if marker == 0xDA:
            out.extend(jpeg_bytes[i:])
            break
        # Reserved markers (0x01, 0xD0–0xD7) have no length.
        if marker in (0x01,) or 0xD0 <= marker <= 0xD7:
            out.extend(jpeg_bytes[i : i + 2])
            i += 2
            continue
        # All other markers have a 2-byte big-endian length (incl. the length
        # bytes themselves) immediately after the marker.
        seg_len = (jpeg_bytes[i + 2] << 8) | jpeg_bytes[i + 3]
        seg_end = i + 2 + seg_len
        if marker == 0xEE:  # APP14 — skip
            i = seg_end
            continue
        out.extend(jpeg_bytes[i:seg_end])
        i = seg_end
    return bytes(out)


def _inject_malformed_xmp(jpeg_bytes: bytes) -> bytes:
    """Insert a deliberately malformed APP1 XMP segment after SOI.

    The segment advertises itself as XMP (namespace `http://ns.adobe.com/xap/1.0/\0`)
    but the XMP payload is truncated / non-XML. Exiv2's XMP parser rejects it
    and sipi emits `log_warn("Failed to parse XMP metadata from JPEG")`, which
    is exactly the log_warn site the F3 test needs to exercise.
    """
    if jpeg_bytes[:2] != b"\xff\xd8":
        raise ValueError("not a JPEG (missing SOI)")
    xmp_ns = b"http://ns.adobe.com/xap/1.0/\0"
    bad_payload = b"<not-xml-at-all>"
    # APP1 segment length counts everything after the marker (incl. length bytes)
    seg_body = xmp_ns + bad_payload
    seg_len = len(seg_body) + 2  # +2 for the length field itself
    if seg_len > 0xFFFF:
        raise ValueError("APP1 payload too large")
    app1 = bytearray()
    app1.extend(b"\xff\xe1")  # APP1 marker
    app1.extend(seg_len.to_bytes(2, "big"))
    app1.extend(seg_body)
    # Inject immediately after the SOI so it precedes any real APP markers.
    return jpeg_bytes[:2] + bytes(app1) + jpeg_bytes[2:]


def _insert_app_marker(jpeg_bytes: bytes, marker: int, payload: bytes) -> bytes:
    """Insert a raw APPn segment with an arbitrary payload immediately after
    SOI. `payload` becomes the segment's data bytes verbatim (no namespace
    header is added) — the caller controls the exact `data_length` libjpeg's
    marker-saving machinery will see.
    """
    if jpeg_bytes[:2] != b"\xff\xd8":
        raise ValueError("not a JPEG (missing SOI)")
    seg_len = len(payload) + 2  # +2 for the length field itself
    if seg_len > 0xFFFF:
        raise ValueError("segment payload too large")
    seg = bytearray()
    seg.append(0xFF)
    seg.append(marker)
    seg.extend(seg_len.to_bytes(2, "big"))
    seg.extend(payload)
    return jpeg_bytes[:2] + bytes(seg) + jpeg_bytes[2:]


def _build_icc_profile_no_description(size: int = 536) -> bytes:
    """Synthesize a minimal but well-formed ICC profile with a zero tag
    count — therefore no tags at all, including no description tag.

    Builds the standard 128-byte ICC header field by field (profile size,
    preferred CMM `ADBE`, version `0x02100000`, device class `mntr`, data
    colour space `RGB `, PCS `XYZ `, a fixed date/time, signature `acsp`,
    platform `MSFT`, everything else zero), appends a 4-byte tag count of
    0, and pads with zero bytes out to the declared size.
    """
    header = bytearray()
    header += size.to_bytes(4, "big")  # profile size
    header += b"ADBE"  # preferred CMM type
    header += (0x02100000).to_bytes(4, "big")  # profile version
    header += b"mntr"  # profile/device class
    header += b"RGB "  # colour space of data
    header += b"XYZ "  # profile connection space
    for field in (0x07D1, 0x0001, 0x0017, 0x0013, 0x0022, 0x0022):  # date/time
        header += field.to_bytes(2, "big")
    header += b"acsp"  # profile file signature
    header += b"MSFT"  # primary platform signature
    header += b"\x00" * 4  # profile flags
    header += b"\x00" * 4  # device manufacturer
    header += b"\x00" * 4  # device model
    header += b"\x00" * 8  # device attributes
    header += b"\x00" * 4  # rendering intent
    header += b"\x00" * 12  # PCS illuminant
    header += b"\x00" * 4  # profile creator
    header += b"\x00" * 16  # profile ID
    header += b"\x00" * 28  # reserved
    assert len(header) == 128
    header += b"\x00\x00\x00\x00"  # tag count = 0 (no tags at all)
    header += b"\x00" * (size - len(header))
    assert len(header) == size
    return bytes(header)


def _build_oversized_exif_tiff(target_total: int = 65527) -> bytes:
    """Build a minimal but well-formed little-endian TIFF/Exif blob whose
    single IFD0 entry (`ImageDescription`, ASCII) carries enough data to
    bring the whole blob to `target_total` bytes -- the largest an Exif TIFF
    blob can be and still fit, together with the 6-byte "Exif\\0\\0"
    identifier, inside a single APP1 marker segment (2-byte JPEG segment
    length field: 65535 max, minus 2 length bytes minus 6 identifier bytes =
    65527).

    Layout: TIFF header (8) + IFD0 entry count (2) + one 12-byte entry + next
    IFD offset (4) = 26 bytes of structure, followed by the ASCII value data
    itself (NUL-terminated).
    """
    header_and_ifd_overhead = 8 + 2 + 12 + 4
    n = target_total - header_and_ifd_overhead
    assert n > 4, "ASCII value must be large enough to need an offset, not inline storage"
    value_offset = header_and_ifd_overhead
    data = (b"A" * (n - 1)) + b"\x00"

    tiff = bytearray()
    tiff += b"II"  # little-endian byte order
    tiff += (42).to_bytes(2, "little")  # TIFF magic number
    tiff += (8).to_bytes(4, "little")  # offset to IFD0
    tiff += (1).to_bytes(2, "little")  # IFD0 entry count
    tiff += (0x010E).to_bytes(2, "little")  # tag: ImageDescription
    tiff += (2).to_bytes(2, "little")  # type: ASCII
    tiff += n.to_bytes(4, "little")  # count (bytes, incl. NUL terminator)
    tiff += value_offset.to_bytes(4, "little")  # value offset
    tiff += (0).to_bytes(4, "little")  # next IFD offset (none)
    assert len(tiff) == value_offset
    tiff += data
    assert len(tiff) == target_total
    return bytes(tiff)


def generate_malformed(malformed_dir: pathlib.Path) -> None:
    """Regression fixtures for the JPEG marker-parsing over-reads fixed
    alongside DEV-6066 / N1 / N4, plus the DEV-7056 fatal-metadata-parse
    fixture (see test/_test_data/images/malformed/README.md for the defect
    writeup of each). Each fixture starts from a small well-formed RGB JPEG
    so libjpeg's header parse (and the image decode itself) still succeeds;
    only the injected marker segment is malformed.
    """
    malformed_dir.mkdir(parents=True, exist_ok=True)

    base_img = Image.new("RGB", (32, 32), color=(96, 160, 32))
    base_buf = io.BytesIO()
    base_img.save(base_buf, format="JPEG", quality=85)
    base_jpeg = base_buf.getvalue()

    # jpeg_xmp_truncated.jpg — the APP1 XMP namespace header
    # ("http://ns.adobe.com/xap/1.0/\0", 29 bytes) is the *entire* segment
    # payload: no XMP packet bytes follow it, and no "<?xpacket begin"
    # wrapper is present anywhere in the segment. Before DEV-6066,
    # read_shape()'s marker walk tracked how far it had scanned with a
    # counter that started at 0 from the position memmem() found (not from
    # the start of marker->data), so it kept advancing up to a further
    # marker->data_length bytes past the namespace header — walking well
    # past the end of the segment's heap buffer looking for a wrapper that
    # does not exist.
    xmp_ns = b"http://ns.adobe.com/xap/1.0/\0"
    xmp_truncated = _insert_app_marker(base_jpeg, 0xE1, xmp_ns)
    xmp_path = malformed_dir / "jpeg_xmp_truncated.jpg"
    xmp_path.write_bytes(xmp_truncated)
    print(f"  wrote {xmp_path} ({len(xmp_truncated)} bytes, XMP namespace with no packet body)")

    # jpeg_icc_short_app2.jpg — an APP2 segment whose payload is exactly the
    # 12-byte "ICC_PROFILE\0" identifier with none of the 2 trailing
    # sequence-number/count bytes the real ICC-in-JPEG layout requires.
    # `read()`'s ICC extraction computed `data_length - offset - 14`; with
    # `data_length == offset + 12` that underflows (N1) before the fix.
    icc_short = _insert_app_marker(base_jpeg, 0xE2, b"ICC_PROFILE\0")
    icc_path = malformed_dir / "jpeg_icc_short_app2.jpg"
    icc_path.write_bytes(icc_short)
    print(f"  wrote {icc_path} ({len(icc_short)} bytes, ICC_PROFILE identifier with no header tail)")

    # jpeg_photoshop_short_app13.jpg — an APP13 segment shorter than the
    # 14-byte "Photoshop 3.0\0" identifier it is compared against. The first
    # 5 bytes ("Photo") deliberately match the identifier's prefix so
    # `strncmp(..., 14)` cannot short-circuit on an early mismatch and must
    # walk past the segment's actual (5-byte) allocation to find a
    # difference or `n` (N4).
    app13_short = _insert_app_marker(base_jpeg, 0xED, b"Photo")
    app13_path = malformed_dir / "jpeg_photoshop_short_app13.jpg"
    app13_path.write_bytes(app13_short)
    print(f"  wrote {app13_path} ({len(app13_short)} bytes, APP13 identifier truncated to 5 bytes)")

    # jpeg_photoshop_overshoot_app13.jpg — a *well-formed* "Photoshop 3.0\0"
    # identifier (14 bytes, so the caller's `data_length >= 14` + strncmp
    # guard passes and parse_photoshop() runs) followed by exactly one 8BIM
    # resource that is precisely 7 bytes and nothing after it:
    #   "8BIM" (4) + resource id 0x0404 (2) + Pascal name-length 0x00 (1).
    # parse_photoshop() sees length==7. It consumes 8BIM(4)+id(2), reads the
    # 1-byte name length (0 -> empty, even), then `slen++` (length byte) makes
    # slen=1 and the odd-even padding bump makes slen=2 — one byte more than
    # the single byte remaining. Before the fix, `ptr += slen` walked one byte
    # past `end`, so the next `4 > (size_t)(end - ptr)` computed `end - ptr ==
    # -1`, cast to SIZE_MAX, defeated the guard, and over-read `datalen` and
    # beyond (the review's most severe finding). The fix adds
    # `if (slen > (size_t)(end - ptr)) break;` before the advance, so the walk
    # stops cleanly and the image still decodes.
    app13_overshoot = _insert_app_marker(
        base_jpeg, 0xED, b"Photoshop 3.0\0" + b"8BIM" + b"\x04\x04" + b"\x00"
    )
    overshoot_path = malformed_dir / "jpeg_photoshop_overshoot_app13.jpg"
    overshoot_path.write_bytes(app13_overshoot)
    print(
        f"  wrote {overshoot_path} ({len(app13_overshoot)} bytes, "
        "APP13 8BIM resource with even-padding pointer overshoot)"
    )

    # jpeg_exif_truncated.jpg — an APP1 segment whose payload is the 6-byte
    # "Exif\0\0" identifier immediately followed by a truncated little-endian
    # TIFF header ("II*", 3 bytes): no low byte of the magic number, no IFD
    # offset, and no IFD behind it. read()'s EXIF extraction locates
    # "Exif\0\0" via memmem() and hands everything after it to Exif::parse()
    # (exiv2's Exiv2::ExifParser::decode()), which needs a well-formed TIFF
    # structure (byte order + magic + IFD) to walk. Unlike this file's other
    # fixtures, which pin a *bounds guard* stopping before any parser runs,
    # this payload passes every upstream length check and reaches
    # Exiv2::ExifParser::decode() itself, which throws Exiv2::Error — fatal
    # under SIPI's no-corrupt-embedded-metadata contract (DEV-7056).
    exif_payload = b"Exif\x00\x00" + b"II*"
    exif_truncated = _insert_app_marker(base_jpeg, 0xE1, exif_payload)
    exif_path = malformed_dir / "jpeg_exif_truncated.jpg"
    exif_path.write_bytes(exif_truncated)
    print(f"  wrote {exif_path} ({len(exif_truncated)} bytes, EXIF identifier with truncated TIFF header)")

    # jpeg_icc_no_description.jpg — a well-formed APP2 ICC segment (the real
    # "ICC_PROFILE\0" + sequence number (1) + count (1) layout, so it clears
    # every upstream length guard and the embedded bytes actually reach
    # Icc::parse()) whose profile has a valid 128-byte header but a tag count
    # of 0 — no tags at all, therefore no description tag. lcms2's
    # cmsOpenProfileFromMem() accepts the header-only profile, but
    # cmsGetProfileInfoASCII(..., cmsInfoDescription, ...) then reports
    # length 0. Icc::parse() must classify this as icc_unknown instead of
    # reading a description buffer that was never populated (DEV-7078).
    icc_no_desc_profile = _build_icc_profile_no_description()
    icc_no_desc_payload = b"ICC_PROFILE\x00" + b"\x01\x01" + icc_no_desc_profile
    icc_no_desc = _insert_app_marker(base_jpeg, 0xE2, icc_no_desc_payload)
    icc_no_desc_path = malformed_dir / "jpeg_icc_no_description.jpg"
    icc_no_desc_path.write_bytes(icc_no_desc)
    print(f"  wrote {icc_no_desc_path} ({len(icc_no_desc)} bytes, ICC profile with zero tag count)")

    # jpeg_oversized_exif.jpg — a single APP1 segment carrying the largest
    # Exif TIFF blob that still fits a JPEG marker's 2-byte length field
    # (65527 TIFF bytes + the 6-byte "Exif\0\0" identifier = 65533, plus the
    # 2 length bytes themselves = 65535, the field's maximum value). The TIFF
    # blob is well-formed (single IFD0 entry, ImageDescription/ASCII) so
    # Exif::parse() accepts it; SipiIOJpeg::write()'s re-serialized Exif
    # blob is then at or above the 65533-byte total-marker-length limit
    # libjpeg's write_marker_header() enforces, regressing the guard that
    # skips (rather than emits) an oversized marker instead of longjmp'ing
    # out of jpeg_write_marker() (S2-14).
    exif_oversized_tiff = _build_oversized_exif_tiff()
    exif_oversized_payload = b"Exif\x00\x00" + exif_oversized_tiff
    exif_oversized = _insert_app_marker(base_jpeg, 0xE1, exif_oversized_payload)
    exif_oversized_path = malformed_dir / "jpeg_oversized_exif.jpg"
    exif_oversized_path.write_bytes(exif_oversized)
    print(f"  wrote {exif_oversized_path} ({len(exif_oversized)} bytes, oversized Exif APP1 payload)")


def generate(out_dir: pathlib.Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "cmyk").mkdir(exist_ok=True)

    pixels = _generate_checker(128)
    cmyk_jpeg = _encode_cmyk_jpeg(pixels, 128)

    photoshop_path = out_dir / "cmyk" / "cmyk_photoshop_app14.jpg"
    photoshop_path.write_bytes(cmyk_jpeg)
    print(f"  wrote {photoshop_path} ({len(cmyk_jpeg)} bytes, APP14 transform=0)")

    raw_jpeg = _strip_app14(cmyk_jpeg)
    raw_path = out_dir / "cmyk" / "cmyk_raw_no_app14.jpg"
    raw_path.write_bytes(raw_jpeg)
    print(f"  wrote {raw_path} ({len(raw_jpeg)} bytes, APP14 stripped)")

    # F3 fixture: a small RGB JPEG with deliberately malformed XMP.
    rgb_img = Image.new("RGB", (64, 64), color=(128, 64, 200))
    rgb_buf = io.BytesIO()
    rgb_img.save(rgb_buf, format="JPEG", quality=85)
    rgb_jpeg = rgb_buf.getvalue()
    malformed = _inject_malformed_xmp(rgb_jpeg)
    malformed_path = out_dir / "malformed_xmp.jpg"
    malformed_path.write_bytes(malformed)
    print(
        f"  wrote {malformed_path} ({len(malformed)} bytes, malformed XMP injected)"
    )


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <output_dir>", file=sys.stderr)
        return 1
    out_dir = pathlib.Path(sys.argv[1])
    print(f"Generating JPEG fixtures under {out_dir}")
    generate(out_dir)
    malformed_dir = out_dir.parent / "malformed"
    print(f"Generating JPEG marker-parsing regression fixtures under {malformed_dir}")
    generate_malformed(malformed_dir)
    print("\nAll fixtures generated successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
