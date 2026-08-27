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

Also produces three marker-parsing over-read regression fixtures as a
sibling `../malformed/` directory (relative to the given output dir), see
`generate_malformed()` and test/_test_data/images/malformed/README.md for
each fixture's defect (DEV-6066, N1, N4).

Run (from the sipi repo root):

    uv run test/unit/sipiimage/fixtures/generate_jpeg_fixtures.py \
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


def generate_malformed(malformed_dir: pathlib.Path) -> None:
    """Regression fixtures for the JPEG marker-parsing over-reads fixed
    alongside DEV-6066 / N1 / N4 (see
    test/_test_data/images/malformed/README.md for the defect writeup).
    Each fixture starts from a small well-formed RGB JPEG so libjpeg's
    header parse (and the image decode itself) still succeeds; only the
    injected marker segment is malformed.
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
