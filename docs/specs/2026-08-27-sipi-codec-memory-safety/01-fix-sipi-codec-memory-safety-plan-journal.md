# Execution Journal — fix: memory-safety bugs in the C++ image codecs

Plan: `01-fix-sipi-codec-memory-safety-plan.md` (DEV-6418)
Target repo: `sipi` (worktree `security-remediation`), branch `worktree-security-remediation`
Base commit: `6518a5e9`
Chunks = plan commits C1..C9, sequential. One Conventional Commit per landed chunk.
Plan file + this journal are NOT part of any code commit (ship-time, session tier).

## Chunk table

| Chunk | Status | Commit SHA | Summary |
|-------|--------|-----------|---------|
| C1 | done | 80af4e04 | util checked-arith + validate_decode_dims + fixture tooling |
| C2 | done | fb79b32a | guard pixel-buffer allocations vs integer overflow (DEV-6063); 37 sites gated + crop signed/unsigned fix |
| C3 | done | af1b30af | channel-count buffer sizing/indexing (DEV-6068, N5); JP2 16-bit YCbCr read()-fixture test deferred |
| C4 | done | c8169a5f | TIFF buffer + libtiff-field bugs (DEV-6064/65/67, N3, N6); 4 LFS fixtures; RESOLUTIONUNIT kept `short` |
| C5 | done | 04f73a9c | JPEG marker-parsing over-reads (DEV-6066, N1, N4); 3 LFS fixtures; read_shape XMP downgraded to log_warn (matches read) |
| C6 | done | 4b6b04a3 | J2K palette expansion (DEV-6065, N2); fixture-free test via extracted validate_j2k_palette_mapping; JP2 palette fixture deferred |
| C7 | done | 99042462 | PNG 16-bit byte-swap loop counter (N7); no fixture (UBSan+review); validate_decode_dims is post-decode (libpng API) |
| C8 | done | 031ceafb | stop leaking source paths in client errors (DEV-6062); rustfmt+clippy green; e2e leak test passes |
| C9 | done | b377069f | restrict CORS to configured origin allowlist (DEV-6061); env-only knob; rustfmt+clippy green; 3 default CORS tests unchanged |

## Review-fix round (2026-08-28) — adversarial-review defects in the C1..C9 batch

| Chunk | Status | Commit SHA | Summary |
|-------|--------|-----------|---------|
| RF1 | done | 2d2b8b24 | bound parse_photoshop ptr advances past marker end: even-padding bump could push slen/datalen one byte past the earlier `end - ptr` check, wrapping the next ptrdiff to SIZE_MAX; re-check remaining buffer after each bump before `ptr +=` |
| RF2 | done | 56b4ed28 | guard J2K 8/12/16-bit decode buffers vs dimension overflow: `static_cast<int>(dims.area())` truncated the 64-bit product to 32 bits; size via `checked_buf_size` over decode-region dims (dims.size.x/y), throw `SipiImageError` on overflow |
| RF3 | done | bfa76897 | validate PNG dims before decode not after: moved bps/nc reads + `validate_decode_dims` from post-`png_read_image`/`buffer.resize` to immediately after `png_read_update_info`; removed the now-dead duplicate block, kept palette-alpha check |

Gate per commit: `just bazel-build` + `bazel test //test/unit/sipiimage:sipi_image_tests //test/unit/tiff_codecs:tiff_codecs_tests` — both green (no wrapper segfault). C++-only; asan/ubsan skipped (broken local macOS link). nx/ny confirmed assigned at SipiIOPng.cpp:187-188, before line 244.

## Review-fix round 2 (2026-08-28) — 5 medium findings (M1..M5)

| Chunk | Status | Commit SHA | Summary |
|-------|--------|-----------|---------|
| M1 | done | 43d8c19a | redact_paths util helper; applied in SipiImageError::message() + shttps::Error::message() (now by-value); to_string()/what() keep full paths; test in format_error_path_test.cpp |
| M2 | done | 4b4bd74d | Deny variant carries vary=!allowlist.is_empty(); all 4 CORS sites emit Vary:Origin on deny in allowlist mode; empty-allowlist path byte-identical; clippy+fmt green |
| M3 | done | db23869c | let-else with non-empty basename filter hard-errors StaticOutcome::Err(500) instead of unwrap_or(infile); no fallback leak; clippy+fmt green |
| M4 | done | 739c1d2b | SIPI_ALLOWED_ORIGINS row added to running.md env table after SIPI_JWTKEY; env-only (*(none)* CLI flag) |
| M5 | done | 549d320d | TIFF XMLPACKET guarded: xmp_length > INT_MAX → log_warn + skip Xmp (image still decodes); <limits> added; no fixture (>2GiB impractical), code-review + green tests bar |

## Deferrals

- **C3 JP2 16-bit YCbCr `read()` regression test + fixture (DEV-6068 item 4).** The
  `convertYCC2RGB` 16-bit fix is covered by the direct in-code `ConvertYcc16BitDoesNotOverflow`
  test. The additional integration test that drives the fix via a real `img->read()` needs a
  Kakadu-encoded JP2 with `photo == YCBCR` and `bps == 16` (only real decoded Kakadu output
  reaches `SipiIOJ2k.cpp` `convertYCC2RGB`). Generating that fixture needs license-gated Kakadu
  encode tooling (`gh auth` + org membership per `docs/src/development/kakadu.md`) and was out
  of C3's scope (SipiImage.cpp + test dir only). Follow-up: add the fixture + read() test, or
  fold into the codec fuzz-harness follow-up already noted in the plan's pre-merge section.
- **C6 J2K palette on-disk fixture (DEV-6065/N2).** The palette-expansion fix is covered by a
  fixture-free unit test on the extracted `Sipi::validate_j2k_palette_mapping`. No on-disk
  palette-indexed JP2 fixture was produced: SIPI's own `SipiIOJ2k::write` has no palette-encode
  path (`access_palette` is decode-only), and the `PALETTE_Conversion` round-trip expands to RGB
  before J2K write, so it never emits a palette-indexed JP2. Building one externally needs
  license-gated Kakadu tooling. Follow-up: same fuzz-harness / Kakadu-fixture track as the C3 item.

## Side findings

- **C9 CORS knob is env-only, not a `ServerOverrides`/clap arg (justified deviation).** The plan
  said "new clap/env arg in `config.rs` (the `ServerOverrides` env-override surface)". The worker
  read `SIPI_ALLOWED_ORIGINS` directly via `std::env` in `serve()` (like the existing
  `SIPI_RS_PORT`) via a free function `config::allowed_origins_from_env`, rather than adding a
  `ServerOverrides` field, because a new `ServerOverrides` field forces edits to cli-rs's
  exhaustive `From<&ServerArgs>` literal (out of C9's file scope), and CORS never crosses the FFI
  seam. This still satisfies the plan's actual intent (env-injectable by ops-deploy, no TOML
  section). Consequence: there is NO `--allowed-origins` CLI flag / `--help` entry — the knob is
  env-only. Maintainer: confirm env-only is acceptable, or add the CLI flag + `ServerOverrides`
  field (and the cli-rs `From` update) as a follow-up.
- **C8 two upload.lua copies (maintainer flag).** The `//test/e2e:upload` test loads
  `test/_test_data/scripts/upload.lua`, a separately git-tracked fixture FORK of
  `scripts/upload.lua` (not a generated copy). C8 applied the identical generic-message /
  keep-`server.log` fix to BOTH so the e2e leak-check passes. These two scripts should be
  reconciled/deduped in a separate change — the fork is easy to let drift.
- **C4 RESOLUTIONUNIT deviation (justified):** plan item 7 called for the
  `RESOLUTIONUNIT` `TIFFGetField` out-param → `uint16_t`. Kept as `short` instead: libtiff
  writes the same 16-bit width either way (no memory-safety difference), but `uint16_t`
  selected a different Exiv2 `assign_val` overload and shifted 2 approval-test JPEG goldens.
  Reverted to preserve byte-exact output per the plan's approval-determinism gate; documented
  inline in `SipiIOTiff.cpp`. All other DEV-6067 width fixes applied as specified.
- **C4 DEV-6065 test is intentionally tolerant** (accepts clean decode OR thrown
  `SipiImageError`): the bounds-check code is in place and reviewed correct, but the only
  reachable OOB vector is `one2eight()`'s uninitialized black/white locals for PALETTE+bps=1,
  a pre-existing issue out of C4 scope that did not reproduce out-of-range values in this
  environment. ASan on Linux CI is the real gate for the OOB class.
- Repo co-locates util unit tests in `src/util/` per ADR-0003 (hash_test.cpp,
  parsing_test.cpp, urldecode_test.cpp in `util_test`), not `test/unit/util/`.
  C1 test placement follows the ADR-0003 co-location precedent; the plan's
  literal `test/unit/util/` path is superseded by the established convention.
