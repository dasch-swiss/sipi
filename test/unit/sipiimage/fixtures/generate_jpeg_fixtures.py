#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["Pillow>=10.0"]
# ///
"""
One-shot fixture generator for JPEG test fixtures used by the DEV-6250 /
DEV-6257 regression tests.

Produces three fixtures under test/_test_data/images/jpeg/:

  1. cmyk/cmyk_photoshop_app14.jpg — CMYK baseline with an Adobe APP14 marker
     advertising transform=0 (Photoshop-style "Unknown / CMYK" — libjpeg-turbo
     outputs inverted CMYK that needs re-inversion).
  2. cmyk/cmyk_raw_no_app14.jpg — CMYK baseline **without** APP14 (raw CMYK;
     no inversion needed). Pinned as the negative test for Phase 5.2.
  3. malformed_xmp.jpg — 64x64 RGB JPEG with a deliberately corrupted APP1 XMP
     segment (valid JPEG envelope, XMP packet that fails to parse). Used by the
     F3 feature-contract test to prove that log_warn is routed to stderr under
     --json without breaking the single-document contract on stdout.

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
    print("\nAll fixtures generated successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
