---
title: "fix: SIPI security hardening wave 2 (deep analysis after DEV-6418)"
type: fix
date: 2026-09-04
author: "Ivan Subotic"
status: reviewed(2)
repositories:
  - ops-deploy
  - dsp-api
linear: DEV-7131
---

# fix: SIPI security hardening wave 2 (deep analysis after DEV-6418)

## Overview

A deep security analysis of SIPI as deployed today: an internet-exposed, open-source
IIIF image server fronting an archive whose access-control promises (restricted view,
embargo, takedown) are legal obligations, not conveniences. The analysis takes the
DEV-6418 remediation (PR #796, #797, #789, #783, #798; all merged by 2026-08-31) as the
closed baseline and asks what is still exploitable. It covered the Rust HTTP shell, the
Lua runtime and shipped scripts, the C++ metadata/encode/processing paths the wave-1
review did not reach, the FFI seam, disk cache and admission control, the container and
CI supply chain, the dependency pins, the nightly fuzz workflow, and the production
deployment in `ops-deploy` (read-only).

Result: 45 findings (43 from the reviews, 2 surfaced while deepening), of which 6 are Critical and 6 High. The Criticals are three
remotely reachable memory-corruption bugs in the TIFF and JP2 codecs, two
authorization defects (anyone can reconstruct a "restricted view" image at native
resolution by tiling, and a restricted image whose restriction resolves to nothing is
streamed as the original, which is live in DSP today), and one unauthenticated request shape that permanently wedges
every engine worker thread. The nightly fuzz workflow has been red on the TIFF and J2K
legs since 2026-08-31, and both failures are in this register.

The sipi-repo code phases (1 to 10) land as **one all-or-nothing PR**, executed by the
orchestrator, with **each finding's fix as its own commit** (rebase-merge lands every
branch commit on `main` verbatim; the three breaking commits keep their `!` and
`BREAKING CHANGE:` footers). The phase numbering is the execution and priority order
within that branch. Phase 0 is the operator-applied deployment prerequisite that unblocks
the two production-visible defaults today. Phases 1 to 5 are the security fixes; Phases 6a
to 10 are hardening; Phase 11 is deployment posture (operator-applied); Phase 12 reconciles
Linear; Phase 13 is the release and rollout. Phases 0, 11, 12 and 13 are operator or
cross-repo work and stay outside the PR.

## Problem Statement / Motivation

**Threat model.** SIPI's code is public, so an attacker reads the same source the
reviewers read. The service is reachable by anyone on the internet through Traefik
(`Host(iiif.<domain>)`, only `/store` and `/metrics` blocked). Attacker-controlled
input reaches SIPI on three channels:

1. **Every HTTP request**: the IIIF URL (identifier, region, size, rotation, quality,
   format), headers (`Origin`, `X-Forwarded-Host`, `Cookie`, `Authorization`,
   `Range`), and any body on a configured Lua route or the docroot route.
2. **Every image on disk**: SIPI decodes whatever the ingest pipeline deposited.
   A malicious or merely malformed TIFF/JP2/JPEG/PNG is a remote input the moment
   someone requests it. Archives accept files from thousands of external depositors.
3. **Operator configuration**: shipped defaults (config, scripts, docroot) that a
   generic SIPI deployment runs unchanged.

Consequences that matter for DaSCH, in order: (a) disclosure of restricted or
embargoed material, (b) process compromise (memory corruption as root in the
container), (c) availability of the whole IIIF service, (d) data-handling obligations
(what leaves the EU on a crash).

**What wave 1 fixed** (do not re-report): pixel-buffer allocation overflow, TIFF
colormap/palette bounds, TransferFunction API shape, JPEG marker over-reads, J2K
palette expansion and 8-bit decode buffer, PNG pre-decode dimension check,
`Icc::parse` empty description, error-message path leaks, CORS allowlist knob,
Lua stdlib whitelist + memory cap + deadline, JWT HS256 pinning with `exp`, fail-closed
preflight startup, `server.http` redirect/body caps, value-based codec errors, the
codec fuzz harness, `/full/1,1/` now 400, Bazel visibility boundaries. Path traversal,
`%00`, Content-Disposition CRLF and ReDoS were fixed in March 2026.

**What this analysis found** is summarized in the findings register below. The pattern
is consistent: the machinery built in wave 1 holds up (the seam, the sandbox, the path
validation, the admission ordering on the IIIF path). The exploitable risk is in the
paths the wave-1 review did not reach (planar-separate TIFF, multi-component JP2,
encode), in authorization *semantics* rather than mechanics (restrict), in shipped
defaults (secret, scripts), and in dependency freshness.

### Nightly fuzz workflow (red since 2026-08-31)

Runs 33354275318 through 33833567222 (2026-08-31 to 2026-09-04) all fail on the same
two legs; the run of 2026-08-30 was the last green one.

| Leg | Failing step | Root cause | Register |
|---|---|---|---|
| `fuzz / tiff` | Fuzz under ASan (300s) | `heap-buffer-overflow READ of size 8` in `read_standard_data<uint8_t>` at `SipiIOTiff.cpp:729` (uncompressed, contiguous, `bps == 8` scanline copy reads `nc * roi_w` bytes from a `scanline` buffer whose libtiff size `sll` is 1 byte). The crash input is in the persisted merged corpus, so the leg fails at seed load every night. | S2-08 |
| `fuzz / j2k` | Fuzz (libFuzzer, 600s) | `libFuzzer: timeout after 34 seconds` in `jp2_input_box::read_box_header` under `jx_source::finish_jp2_header_box` from `SipiIOJ2k::read_shape`. The DEV-7080 hang class, re-found by mutation after ~370 inputs. | S2-13 |

Two process defects compound this. The TIFF leg's `libfuzzer.log` is 1.6 GB (the
re-enabled libtiff warning handler prints `Fax3Decode1D: Bad code` and friends for
every malformed input), so each failing night uploads a 1.6 GB artifact and the job log
is unreadable. And the J2K leg runs at ~9 exec/s, so its 600 s budget explores almost
nothing before hitting the known hang.

### Production Sentry evidence (as of 2026-09-04)

The `dasch/sipi` Sentry project (org `dasch`) has three unresolved issues. `dsp-ls-prod-01`
runs 6.3.1; the `dsp-rdu-*` environments were upgraded to 8.0.0 during the review window,
so SIPI-1S (2026-08, `dsp-rdu-10`) is stamped 6.3.1 while SIPI-1T (2026-09-04, `dsp-rdu-14`)
is post-upgrade 8.0.0. Event line numbers are therefore against older trees, but the code
paths persist on `main@5007032c`. Each is cross-referenced to the register below; only
SIPI-1Q changes a finding's status.

| Sentry | What | Volume | Maps to |
|---|---|---|---|
| [SIPI-1Q](https://dasch.sentry.io/issues/SIPI-1Q) | `JPEG write failed: Input file read error` writing a `.jpx` source out as `default.jpg` (`sipi.phase=write`, 765×945 RGB from a 15.6 MB JP2, `input_file /sipi/images/0112/…jpx`), `dsp-ls-prod-01`, rel 6.3.1 | 117 events since 2026-08-02, last 2026-08-25 | **S2-27** (Phase 4). A mid-encode failure on the live serve path: the client receives it today as a clean `200` chunked EOF (a truncated image indistinguishable from a complete one), which S2-27 fixes with `BodyAbort`. Underlying cause is a JP2 decode read error surfacing during the JPEG row-pull — a corrupt/truncated Service File that fails identically on every request (repository-integrity signal; JP2 robustness is S2-03/S2-13 territory) |
| [SIPI-1T](https://dasch.sentry.io/issues/SIPI-1T) | `get_permission_on_file - DSP-API returned HTTP status code 404` logged from the dsp-api preflight hook (`logger sipi::lua`, `server.rs:1266`), `dsp-rdu-14`, rel **8.0.0** (current) | 46 events in a ~6-minute burst from one CH client, 2026-09-04 | **S2-34** + **S2-43** (Phases 6c/5). The hook fails closed (a *raised* hook is a bare 500, `routes.rs:571-576`; a 404 becomes the dsp-api decision), but 46 requests for missing identifiers from one client in six minutes each drove a full FFI + Lua + dsp-api SPARQL round-trip: no per-client fairness (S2-34) and no negative-result caching (deny / `direct_response` outcomes are never cached, only plain `Decision`s are, `routes.rs:559-587`), the amplification S2-43 warns about. No new defect |
| [SIPI-1S](https://dasch.sentry.io/issues/SIPI-1S) | `BatchSpanProcessor.ExportError` — OTLP exporter DNS resolution failure (`logger opentelemetry_sdk`), `dsp-rdu-10`, rel 6.3.1 | 58 events, 2026-08-23 to 08-26 | Out of scope: telemetry-export infra noise, not a SIPI defect. Recorded only so it is not mistaken for a security finding. Optional follow-up (not in this plan): lower the exporter-failure log level so a transient DNS blip stops raising Sentry errors |

Net effect: SIPI-1Q promotes S2-27 to production-confirmed and is the concrete signal
that the truncated-`200` delivery is live, not theoretical. SIPI-1T corroborates the
fairness and amplification findings without adding a defect. SIPI-1S is not in scope.

## Findings register

Severity reflects impact on the DaSCH deployment. *Status*: CONFIRMED = traced end to
end in source by the author of this plan (each Critical was re-read independently of
the reviewer that reported it); PLAUSIBLE = reviewer finding that needs a runtime
check. Line numbers are against `main` at `5007032c` (release 8.0.0).

### Critical

| ID | Finding | Where | Status | Phase |
|---|---|---|---|---|
| S2-01 | TIFF `PLANARCONFIG_SEPARATE` scanline reads write to `inbuf.data() + nc*roi_w + roi_h + i*roi_w`: no component index, no `roi_y` subtraction, wrong row stride. A stripped LZW planar-separate TIFF plus a region near the bottom writes megabytes of attacker bytes past the allocation. Loop bound at `:744` (`i < roi_h`) is also wrong. | `src/format_handlers/cpp/SipiIOTiff.cpp:741-772`, `:825-846` | CONFIRMED | 1 |
| S2-02 | JP2 encode uses `int stripe_heights[5]` but loops to `getNc()`; channels are bounded only by `kMaxDecodeChannels = 32`. An 8-channel TIFF requested as `.jp2` writes attacker-chosen `int`s over the stack frame. | `src/format_handlers/cpp/SipiIOJ2k.cpp:1564-1588` | CONFIRMED | 1 |
| S2-03 | JP2 decode uses `int stripe_heights[5]` with `Csiz` up to 32; Kakadu reads uninitialized stack as stripe heights for components 5..31 and writes past `dst`. | `src/format_handlers/cpp/SipiIOJ2k.cpp:721-785` | CONFIRMED | 1 |
| S2-04 | Restricted-view bypass by tiling: the `restrict` size clamp is computed against full-image dims, then applied by the codec against the *cropped region* (`size->get_size(roi.size.x, roi.size.y, …)`). `!128,128` on a 128×128 region is a no-op, so `N` region requests reconstruct a restricted image at native resolution. `info.json` for a `restrict` decision reports native dims and the full `scaleFactors`, which hands over the tiling grid. | `src/ffi/cpp/serve_image.cpp:543-557`; `SipiIOJ2k.cpp:491-507`; `src/server/rust/src/routes.rs:438-445`; `info.rs:91-127` (`image_info_json` emits native dims and the full `scaleFactors`) | CONFIRMED | 5 |
| S2-05 | Rotation is parsed with `f32::from_str` and no range or finiteness check; `geometry.cpp` normalizes with `while (angle >= 360.) angle -= 360.` on a `float`. Any value above ~6e9 (or 39+ digits → `inf`) never terminates. Each such GET pins one engine thread holding an admission permit and a decoded buffer. With production `SIPI_NTHREADS=2`, two requests take the service down until restart. Same unbounded-digits path exists for region coords and `pct:`. | `src/iiifparser/rust/parse.rs:91,256-267`; `src/image_processing/cpp/geometry.cpp:628-629`; `src/throttling/cpp/SipiPeakMemory.h:75` | CONFIRMED | 4 |
| S2-09 | `restrict` whose resolved restriction is a no-op (no `size`, `size = "max"`, `pct:100`, no watermark): `restricted_size->undefined()` or an unchanged `get_size` skips the clamp and the request matches the raw-file passthrough predicate. **Live in DSP**: `sipi.init.lua:129-141` returns a bare `{type = "restrict"}` whenever `restrictedViewSettings` is `nil`, so every restricted-view image without settings streams at full resolution today. `/file` accepts `restrict` with no clamp possible. | `serve_image.cpp:557`, `:603-612`; `routes.rs:655-658`, `:765-769`; dsp-api `sipi.init.lua:129-141` | CONFIRMED | 5 |

### High

| ID | Finding | Where | Status | Phase |
|---|---|---|---|---|
| S2-06 | Tiled planar-separate TIFF: `TIFFTileSize` is one plane, but `separateToContig` reads `nc` planes from it. `(nc-1)*tile` bytes of adjacent heap are rendered into the served image (heap disclosure of other requests' buffers). | `SipiIOTiff.cpp:938-951`, `:632-646` | CONFIRMED | 1 |
| S2-07 | `convertYCC2RGB` indexes channels 0..2 with no `nc >= 3` guard; JP2 `colr` says sYCC while `Csiz = 1` → 2–4 byte heap write. | `src/image_processing/cpp/color.cpp:46-57`, `:80-91`; caller `SipiIOJ2k.cpp:838` | CONFIRMED | 1 |
| S2-08 | Contiguous scanline copy trusts libtiff's `TIFFScanlineSize` (`sll`) but copies `nc*roi_w*psiz` bytes derived from SIPI's own tag reads. Nightly ASan crash (`crash-2866a0…`, 145-byte TIFF). Heap over-read. | `SipiIOTiff.cpp:704`, `:729` | CONFIRMED (fuzz) | 1 |
| S2-11 | The shipped image runs `config/sipi.config.lua` whose `jwt_secret` is a public literal; an *absent* secret becomes `""`, which is a valid HMAC key (`unwrap_or_default`). Empty, default and short secrets are all accepted at startup. `admin.password` default is the same class and has no reader at all. | `config/sipi.config.lua:133,151-161`; `src/server/rust/src/lib.rs:270`; `src/scripting/rust/bindings/server.rs:1066,1089`; `src/scripting/rust/entry.rs:209`; `src/BUILD.bazel:652-655` | CONFIRMED | 0, 7 |
| S2-12 | libtiff pinned at 4.7.1 (2025-09); 4.7.2 (2026-06-27) carries the security fixes reported against 4.7.1 (reviewer-cited CVE-2025-61143, CVE-2025-61144, CVE-2026-12912; verify on NVD before quoting externally). TIFF is the primary attacker-facing decode path. | `MODULE.bazel:687-695` | CONFIRMED (pin), PLAUSIBLE (exploitability) | 9 |
| S2-13 | Malformed JP2 hangs `read_shape` forever inside Kakadu (DEV-7080, Todo). Maintainer ruling 2026-08-29: decode watchdog at the FFI seam, own PR. Keeps the J2K fuzz leg red. | `SipiIOJ2k.cpp` `access_codestream` path | CONFIRMED (fuzz) | 3 |

### Medium

| ID | Finding | Where | Status | Phase |
|---|---|---|---|---|
| S2-14 | JPEG marker writes guard `buf.size() <= 65535` but libjpeg limits `start_l + buf.size() <= 65533`; a 65 530-byte Exif/XMP longjmps out of `jpeg_write_marker`, skipping destructors of the metadata buffers constructed inside the `setjmp` window (unbounded leak per request, DoS). IPTC APP13 length omits the 4-byte size field (truncation). | `SipiIOJpeg.cpp:1244-1264`, `:1322` | CONFIRMED | 1 |
| S2-15 | Kakadu box lengths used unchecked: `get_remaining_bytes()` returns -1 for a rubber-length box → `make_unique<char[]>(SIZE_MAX)`; `box.read()` return ignored; `kdu_codestream_comment::get_text()` NULL for binary comments dereferenced by `strncmp`. | `SipiIOJ2k.cpp:370-405`, `:466`, `:936-938`, `:1032` | CONFIRMED | 1 |
| S2-16 | `info.json` and `knora.json` are not gated by the preflight decision: `restrict` → 200 with native dims; `knora.json` returns `originalFilename`, checksums and mime for any decision that yields a path (`restrict`, `login`, `clickthrough`, `kiosk`, `external`, or a `deny` that carries a path). | `routes.rs:436-445`; `info.rs:182-222`, `:269-277` | CONFIRMED | 5 |
| S2-17 | `X-Forwarded-Host` (fallback `Host`) is trusted unvalidated: open redirect on the bare-identifier 303, poisoned `@id`/`Link` in `info.json`, and it is part of the cache key, so cycling host values evicts the whole LRU (cheap outage lever). Also reflected into the `Link` header. | `routes.rs:1923-1947`, `:2086-2100`; `serve_image.cpp:249,262,569` | CONFIRMED | 6a |
| S2-18 | No request timeout, header-read timeout or connection cap on the axum server; admission bounds engine work, not connections. Slowloris holds FDs indefinitely. | `lib.rs:489` | CONFIRMED | 6b |
| S2-19 | Lua-route bodies are read fully into RAM (and copied) *before* `admission.acquire`; `max_post_size` defaults to `0` = unlimited (production sets 2000M); no cap on the number of multipart parts, each holding an open `NamedTempFile` for the whole request. | `routes.rs:1045-1074`, `:1082-1103`; `entry.rs:178` | CONFIRMED | 6b |
| S2-20 | `SipiImage.new()` from Lua decodes with no decode-memory-budget charge; the budget is applied only on the IIIF serve path. A decompression bomb on any Lua upload route bypasses the envelope. | `src/scripting/rust/bindings/image.rs:196-211`; `src/ffi/cpp/image_handle.cpp:94-151`; contrast `serve_image.cpp:655-656` | CONFIRMED | 6b |
| S2-21 | The docroot route (`/server` in production) takes no admission permit and spawns unbounded `spawn_blocking` tasks that each reload libmagic. | `routes.rs:1417-1444` | CONFIRMED | 6b |
| S2-22 | `scripts/token.lua` splices `messageId` and `origin` query params unescaped into a `<script>` body and uses `origin` as the `postMessage` target: reflected XSS plus token exfiltration. Routed by the shipped config at `GET /api/token`. Not in the Docker image (only `send_response.lua` ships). | `scripts/token.lua:26-35`; `config/sipi.config.lua:186-190` | CONFIRMED | 7 |
| S2-23 | `scripts/upload.lua` at `POST /api/upload` has no authorization (Lua routes never call `pre_flight`), derives the stored name from the attacker's `origname`, and its non-image branch calls `server.copyTmpfile` with an undefined global (`index`), reporting success while writing nothing; the e2e fork has the "fixed" call, which would make it an arbitrary-extension write into `imgroot`. | `scripts/upload.lua:99-135,152`; `test/_test_data/scripts/upload.lua:161`; `routes.rs:987-1151` | CONFIRMED | 7 |
| S2-24 | Cookie defaults: `http_only = false`, no `SameSite`; `sendCookie` can only set flags; `apply_headers` uses `insert`, so a second `Set-Cookie` silently replaces the first. | `src/scripting/rust/bindings/mod.rs:99`; `server.rs:718-743`; `src/server/rust/src/sink.rs:316-330` | CONFIRMED | 6c |
| S2-25 | Cache freshness invalidates only when the source mtime is strictly newer. Replacing a Service File with a redacted version while preserving mtime (`cp -p`, `rsync -t`, restore) serves the cached full-resolution derivative indefinitely. Access-control failure with an operational trigger. | `src/cache/cpp/SipiCache.cpp:413` | CONFIRMED | 8 |
| S2-26 | `.sipicache` index parsed as raw structs: `char[256]` fields not NUL-guaranteed (OOB read), `cachepath` concatenated and `unlink`ed with no containment (arbitrary delete as root, needs cache-volume write), canonical key truncated to 255 bytes on persist (two entries collapse, wrong content served, byte accounting double-counted). | `SipiCache.cpp:137-164`, `:228`, `:344`, `:353`; `SipiCache.h:50` | CONFIRMED | 8 |
| S2-27 | A mid-stream encode failure after the head is sent ends as a clean chunked EOF under `200 OK`: clients receive a truncated image indistinguishable from a complete one. `sink::BodyAbort` exists and is unused here. Live in prod (Sentry SIPI-1Q, 117 events, JP2→JPEG). | `serve_image.cpp:328-353`; `serve_response.cpp:118-126`; `sink.rs:266` | CONFIRMED (prod) | 4 |
| S2-28 | Native-crash minidumps (ADR-0018) upload full process memory unredacted. Production `SIPI_SENTRY_DSN` points at `*.ingest.sentry.io` (Sentry SaaS, US), which the ADR left as an open question. Separately, `report_image_error` puts the absolute on-disk `input_file` into the Sentry context and the full `request_uri` into an indexed **tag**. | `docs/adr/0018-…`; `ops-deploy defaults/main.yml:71`; `src/server/rust/src/ffi.rs:1022-1069` | CONFIRMED | 10 |
| S2-29 | Stale native pins with attacker-bytes exposure: lcms2 2.16 (upstream 2.19.1; 2.18 fixed a heap overflow), exiv2 0.28.5 (newer 0.28.x with OOB fixes), curl 8.12.0 (BCR now has 8.21.0.bcr.1; DEV-6564 is unblocked), libexpat 2.7.1 (upstream 2.8.4 with ~25 CVEs since; BCR offers only 2.7.1). libexpat is *not* reachable from untrusted XMP today (`xmp.cpp` stores bytes verbatim), but DEV-7075 will make it reachable, so the bump must precede DEV-7075. | `MODULE.bazel:79,119,631-634,679-684` | CONFIRMED (pins) | 9 |
| S2-30 | The only CVE gate in CI (Docker Scout) scans image layers and cannot see the statically linked C libraries where the attack surface lives; it does not fail the build. No cargo-audit/cargo-deny/OSV for the crate graph. Dependabot's `cargo` entry covers only `/test/e2e`; its `bazel` entry cannot see `http_archive` pins. | `.github/workflows/ci.yml:231-257`; `publish.yml:179-198`; `.github/dependabot.yml` | CONFIRMED | 9 |
| S2-31 | Nightly fuzz workflow red on two legs since 2026-08-31; 1.6 GB logs from libtiff warnings; J2K leg at ~9 exec/s. | `.github/workflows/fuzz.yml`; `src/format_handlers/fuzz/` | CONFIRMED | 2 |
| S2-32 | Fuzz harness calls the two-argument `read(&img, path)` (region and size null) and never calls `write()`. Every ROI-dependent branch and every encoder is unreachable from the fuzzer; S2-01/02/03/06 all live in that blind spot. | `src/format_handlers/fuzz/codec_fuzz_harness.h:121` | CONFIRMED | 2 |
| S2-33 | Shipped `admission_mode` default is `basic`: the memory budget shadow-counts and never rejects; `parse_size` clamps to 32 000 rather than rejecting, so `^32000,32000` upscales admit a ~4 GB output buffer. Production runs `advanced`, generic deployments do not. | `src/ffi/cpp/SipiConf.h:65`; `parse.rs:251`; `serve_image.cpp:659-668` | CONFIRMED | 6b |
| S2-34 | No per-client fairness anywhere in admission: one client can hold every permit and fill the wait queue (the Feb 2026 incident shape). `client_ip` crosses the seam but is only logged. Design gap; needs a decision, not a patch. | `src/throttling/rust/lib.rs`; `routes.rs:295,2117` | CONFIRMED | 6c (decision) |
| S2-35 | Container runs as root with `/bin/sh`, `curl`, `ffmpeg`/`ffprobe` present; compose has no `cap_drop`, `read_only`, or `no-new-privileges`; `/sipi/config` and `/sipi/server` bind mounts are read-write. Existing DEV-5920 (non-root, blocked on NFS UID coordination) and DEV-6321. | `src/BUILD.bazel:283-323,649-706`; `ops-deploy docker-compose-iiif.yml.j2:52-61` | CONFIRMED | 11 |
| S2-36 | Production Lua config still routes `/upload`, `/upload_without_processing`, `/store`, `/delete_temp_file` to scripts that no longer exist in the image (request-time 404), and serves a live docroot at `/server` that executes any `.lua`/`.elua` placed in an operator-writable bind mount. | `ops-deploy templates/iiif/conf/sipi.prod-config.lua.j2:163-195` | CONFIRMED | 11 |

### Low

| ID | Finding | Where | Status | Phase |
|---|---|---|---|---|
| S2-37 | `std::regex` over an unbounded `Range` header on a blocking thread (libc++ recursive matcher; stack overflow = process crash). | `src/ffi/cpp/serve_response.cpp:34-38` | PLAUSIBLE | 4 |
| S2-38 | Small C++ items: `base64Decode` computes a negative length and over-declares the `BIO_read` bound; `read_shape` fast path trusts Essentials `img_w/img_h` without `validate_decode_dims`; `read_watermark` has no dimension guard; no `png_set_chunk_malloc_max` (zTXt bomb); `jp2_colour::init` with empty ICC bytes; `processing::subtract` divides by `2*maxmax` (0 on identical inputs). | `src/metadata/cpp/essentials.cpp:27-58`; `SipiIOJ2k.cpp:950-959,1388-1447`; `SipiIOTiff.cpp:436-506,1715`; `SipiIOPng.cpp:249` (`png_create_read_struct`, no `png_set_chunk_malloc_max`), `:386`; `compose.cpp:374,388` | PLAUSIBLE | 1 |
| S2-39 | Lua binding hygiene: `server.fs.mkdir` passes the mode through (shipped example uses `511` = `0777`); base `print` survives the scrub (unstructured log injection); `os.getenv` unrestricted + `server.http` = clean egress channel; `lua_to_json` recursion unbounded; Basic-auth compare is not constant-time; no filesystem confinement on any path-taking binding and the docs call the VM "sandboxed" without saying so. | `server.rs:460-486,783-890,1246-1270`; `runtime.rs:42,447-454`; `docs/src/lua/index.md:28-32` | CONFIRMED | 7 |
| S2-40 | Docroot serves dotfiles and source maps; `resolve()` answers 400 for traversal vs 404 for missing (existence oracle); `/health` discloses version. | `routes.rs:719-725,1380-1467`; `lib.rs:542-553` | CONFIRMED | 6a |
| S2-41 | Third-party actions pinned by mutable tag; no `SECURITY.md`; `claude.yml` general job runs with `contents/pull_request/issues: write` and no `--allowed-tools`. | `.github/workflows/*.yml`; `claude.yml:157-217` | CONFIRMED | 9 |
| S2-42 | `adminuser`/`adminpasswd` cross the seam into `SipiConf` and are read by nothing. Dead credential surface. | `src/cli/rust/src/commands/server/args/tls_auth.rs:20-25`; `src/ffi/cpp/init.cpp:133-134` | CONFIRMED | 7 |
| S2-43 | The *Preflight cache* key covers `(prefix, identifier, Cookie, Authorization)` while the hook can read every header, host and client IP. Today `get_params` is empty in preflight, so dsp-api's `?token=` path fails closed; wiring query params into preflight without extending the key would make it a critical bypass. | `routes.rs:526-537,693-704`; `preflight_cache.rs:15-25,139-158`; dsp-api `authentication.lua:157-164` | CONFIRMED (landmine) | 5 |
| S2-44 | `client_ip` from rightmost `X-Forwarded-For` is spoofable and is never read on the C++ side (written at `routes.rs:776`, no consumer in `src/ffi/cpp`); carrying it across the seam invites misuse. DEV-6072 still open in Backlog although its three items are resolved or accepted. | `routes.rs:2117-2121`; Linear DEV-6072 | CONFIRMED | 6c, 12 |
| S2-45 | `SipiCache::remove` subtracts `fsize` from `cache_used_bytes` (unsigned) with no underflow guard; with S2-26's double-counting this can wrap and disable eviction. Found while reviewing S2-26. | `SipiCache.cpp:558` | CONFIRMED | 8 |
| S2-46 | No startup check that `docroot` is disjoint from `imgroot`, `tmpdir`, `scriptdir` and `cache_dir`, although the docroot executes any `.lua`/`.elua` it contains; an overlapping configuration turns any write path into remote code execution. Operator-misconfiguration flow found while reviewing S2-23. | `src/server/rust/src/lib.rs` `server_main` (config boundary) | CONFIRMED | 7 |

### Checked and clean (do not spend time here)

Path handling (R1 string check on decoded values, R2 `canonicalize` + boundary-char
`is_within`, applied to the hook-returned path too); header injection (every emission
goes through `HeaderValue::from_str`); Range arithmetic; JWT algorithm pinning; secret
redaction in `Debug` and config-parse errors; body limits on Lua/docroot routes;
fail-closed startup; preflight-cache data structure (fixed slots, full-key compare,
presence markers); `sipi_guard` exception containment and `catch_unwind` on every
Rust callback; ABI lock-step asserts; interior-NUL handling; admission ordering on the
IIIF path; `MemoryBudgetGuard` lifetime; eviction vs in-flight reads; `set_pixels`
geometry invariant; `checked_buf_size` usage; `crop_coords` clamping; `exif.cpp`,
`iptc.cpp`, `xmp.cpp`, `icc.cpp` beyond DEV-7078; Essentials protobuf path; the Lua
`require` loader, sqlite parameter binding, `requireAuth` parsing, kill/commit
contract. DEV-6075's four items: realloc-null fixed, strncpy not present, MEMTIFF
unreachable, "J2K cast" is S2-02/03.

## Proposed Solution

Sixteen phase units (0 to 13, with 6 split into 6a/6b/6c), in the order below. The
sipi-repo code phases (1 to 10) land as one all-or-nothing PR (see Overview); the operator
and cross-repo phases (0, 11, 12, 13) stay separate. Phase 0 is deployment prerequisites (operator-applied,
proposed as diffs). Phases 1 to 5 are the security fixes and 6a to 10
the hardening; they ship together in the one PR, released and rolled out by Phase 13 (SIPI
release, dsp-api two-pin bump, ops-deploy, dev → stage → prod). Phase 11 is deployment
posture hardening (operator-applied). Phase 12 reconciles Linear.

Every fix is placed at the boundary where the untrusted value enters (codec tag read,
IIIF parser, FFI seam entry, Lua binding entry, startup config), per CLAUDE.md's
"validate at system boundaries only". Where a finding could be fixed at two layers, the
plan names one and says why.

## Alternative Approaches Considered

- **External penetration test instead of code review.** Complementary, not a
  substitute: none of the six Criticals is findable black-box without the source
  (they need crafted TIFF/JP2 layouts or knowledge of the clamp ordering). A pentest
  after Phases 1 to 7 is the right sequencing and is listed under Success Metrics.
- **Restrict semantics: reject regions vs scale regions.** For an absolute `!w,h`
  restriction, a region request whose source extent exceeds the restricted box could be
  rejected (400/403) or scaled so the region's *output* never exceeds the box. Scaling
  preserves viewer behavior (OpenSeadragon tiles keep working at low resolution) and is
  what "restricted view" means; rejecting breaks deep-zoom viewers on restricted images.
  The plan scales, with the decision recorded in the phase.
- **Fixing S2-05 in C++ (`fmod`) vs in the parser.** The parser is the boundary and
  IIIF 3.0 already requires 0 ≤ rotation ≤ 360, so the parser rejects; `geometry.cpp`
  is unchanged (a `fmod` there would be defense-in-depth).
- **Vendoring libexpat vs waiting for BCR.** BCR has only 2.7.1 and no maintainer
  activity since February 2026. Vendor as a native `cc_library` (the ADR-0015 pattern
  already used for libtiff, exiv2, lcms2) and optionally contribute the BCR module
  afterwards.

## Technical Considerations

- **Error signalling.** All C++ fixes return `std::unexpected(SipiValueError{
  ErrorCode::kMalformedInput, … })` per ADR-0024; no new throws. Illustrative shape for
  S2-07:

  ```cpp
  if (nc < 3) {
    return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
      "YCbCr conversion needs at least 3 channels, got " + std::to_string(nc) });
  }
  ```

- **Fixtures.** Crafted inputs go to `test/_test_data/images/malformed/` (Git LFS;
  fresh worktrees need `git lfs pull`) and are seeded into the fuzz corpora. Planar-
  separate and multi-sample TIFFs can be produced with libtiff tools or Python
  `tifffile` (`planarconfig='separate'`); multi-component and sYCC/Csiz=1 JP2s need
  Kakadu tooling (`kdu_compress`), which is license-gated (`docs/src/development/
  kakadu.md`). The DEV-7080 hang input must **not** enter the seed corpus (corpus
  replay would hang `bazel test`); it is pinned by a unit test with a deadline instead.
- **Approval goldens.** None of the phase-1 fixes changes output for valid input;
  `just bazel-test-approval` must stay byte-identical. S2-14's IPTC length fix changes
  emitted APP13 bytes for images with IPTC and is recorded in
  `test/approval/CHANGELOG.approval.md`.
- **Breaking changes.** Two commits carry `!` with a `BREAKING CHANGE:` footer: the
  `admission_mode` default flip
  (S2-33, `feat(throttling)!:`, Phase 6b), and the removal of the dead `--adminuser`/
  `--adminpasswd` flags (S2-42, `refactor(cli)!:`, Phase 7). They ride in the one PR, so
  release-please cuts a **major** version (the accepted all-or-nothing consequence).
  Restrict tightening (S2-04, S2-09, S2-16) and the rotation range (S2-05) are `fix:`; they
  restore documented/spec behavior. Dependency bumps are `build(deps):`.
- **Runtime knobs** stay env-only (`SIPI_*` via clap `env=`) with a `DSP_IIIF_*`
  default in ops-deploy; no new TOML section.
- **Hot paths.** S2-08's scanline-size check (decode path) and S2-05's range check (parse
  path) are O(1) comparisons. The `just bench` tiers (`parse|decode|encode|
  process`, `justfile:449-464`) are C++ Google Benchmarks; Phase 1 runs `just bench
  decode` before/after per the hot-path rule, and no Rust-shell phase has a bench gate
  because no request-path tier exists (Phase 6b records an `oha`-style smoke instead).
- **Sanitizers.** Local macOS ASan link is broken (memory: CI-only sanitizers); every
  phase-1 commit is verified by the `asan-ubsan` CI leg and by replaying the new
  fixtures through the fuzz targets.
- **Test-first ordering.** Every defect-replication phase writes the fixture and the
  failing test **before** the fix, and the phase checklists below are ordered that way
  (fixture → red test → fix → verification). Acceptance criterion 1 is the gate: the
  test must fail on `main@5007032c` and pass after the fix. How "red" appears differs by
  class, and this is why the ordering is not uniform local red-green:
  - *Behavioral findings* (authorization S2-04/09/16, truncated-`200` S2-27, parser
    bounds S2-05-parse, cache S2-25/26/45, HTTP-shell exhaustion S2-18/19/20/21/33
    (Phase 6b), host-trust/cookies/secrets/Lua (Phases 6a/6c/7), Sentry redaction S2-28
    (Phase 10)): the red is a clean assertion failure, observable locally. Standard TDD.
  - *Memory-corruption Criticals* (S2-01/02/03/06/07/08, S2-14/15/38): the red is a
    crash/abort under ASan, and local ASan is broken, so red is observed on the CI
    `asan-ubsan` leg — not locally. Write the test as a positive `kMalformedInput`
    assertion (green after the fix); confirm the pre-fix red on CI ASan (or, for S2-08,
    against the committed nightly crash artifact). A plain non-ASan unit test can pass
    green while still buggy, so it is not sufficient evidence of red on its own.
  - *Hang class* (S2-05 rotation, S2-13 JP2): the red is a timeout, so the test is
    deadline-bounded and the hang input is kept out of any corpus-replay target (it would
    hang `bazel test`); it is pinned by a deadline unit test instead.
  - *Not defect-replication* (Phase 2 fuzz-workflow recovery, Phase 9 dependency bumps,
    Phases 0/11 deployment, Phase 12 Linear): no unit-level red exists; these phases keep
    their existing order. Phase 2 is a special case — the fuzz corpus *is* the red
    (the nightly is already failing on the seeded crash inputs).
- **Infra boundary.** Phase 11 produces diffs and commands for the maintainer; nothing
  in this plan applies to deployed infrastructure.
- **Lint gates.** Every Rust-touching phase runs `just bazel-rustfmt-check` and
  `just bazel-clippy-check` before commit (hard rule).

## Implementation Phases

#### Phase 0: Deployment prerequisites (ops-deploy; operator-applied) (S2-11) (DEV-7132)

_Execution: sonnet, low (one verification and a possible rotation diff; the maintainer applies)._

- [ ] Verify (read-only) that `sipi.prod-config.lua.j2` supplies a non-default `jwt_secret` of at least 32 bytes in every environment, so Phase 7's startup check cannot trip a deployment; record the result in this folder's journal
- [ ] If any environment's secret is short or the shipped default: propose the rotation diff (ops-deploy) together with the dsp-api coordination step (dsp-api signs with the same key), to be applied before Phase 7 merges

#### Phase 1: Codec memory safety, wave 2 (S2-01, 02, 03, 06, 07, 08, 14, 15, 38) (DEV-7133)

_Execution: sonnet, high (every deliverable names the file, the wrong expression and the
intended expression; the judgment is in fixture crafting, which is scripted)._

_Test-first (Technical Considerations § Test-first ordering): the fixtures and red tests
below land before the fixes. The red for this phase is a crash/abort under the
`asan-ubsan` CI leg — local ASan is broken, and a non-ASan unit test can pass green while
still buggy — so confirm the pre-fix red on CI (S2-08 also has the committed nightly crash
artifact)._

- [x] Fixtures (LFS, `malformed/`): stripped LZW planar-separate RGB TIFF; tiled planar-separate RGBA TIFF; 8-sample-per-pixel 8-bit TIFF (drives S2-02 via `.jp2` output); JP2 with `Csiz = 8` (S2-03); JP2 with `colr = sYCC` and `Csiz = 1` (S2-07); the nightly `crash-2866a0e1…` TIFF (S2-08); JPEG with a 65 530-byte Exif APP1 (S2-14); JP2 with a rubber-length UUID box and one with a binary `COM` marker (S2-15). Default production method for the JP2 fixtures is a scripted byte edit of the SIZ `Csiz` field / `colr` box / UUID box length over an existing small JP2 from `test/_test_data/images/unit/` (no Kakadu license needed); `kdu_compress` is the alternative when available
- [x] Unit tests in `src/format_handlers/cpp/*_test.cpp` that decode each fixture with a region near the bottom edge (S2-01) and assert `kMalformedInput` or a clean decode, run under the `asan-ubsan` CI leg; confirmed red on `main@5007032c` before any fix lands
- [x] `SipiIOTiff.cpp` `read_standard_data` planar-separate branches (uncompressed `:741-772` and compressed `:825-846`): `inbuf` is `std::vector<T>`, so the destination is in **elements**: `T *dst = inbuf.data() + (c * roi_h + (i - roi_y)) * roi_w;` and every memcpy length is `roi_w * psiz` bytes (source `line.get() + roi_x` for bps 1/4/12, `scanline.get() + roi_x` for bps 8, `scanline.get() + roi_x * psiz` for bps 16). The uncompressed loop bound becomes `i < roi_y + roi_h`; the compressed loop (`:818-825`) already windows on `roi_y` and needs only the offsets. `separateToContig` (`:632-646`, layout `c*ny*sll + y*nx + x`) is already consistent with this layout and is shared with the tiled path: do **not** change it
- [x] `SipiIOTiff.cpp:806` and `:840` (compressed, bps 12): the source offset multiplies a `T*` by `psiz` (`line.get() + nc * roi_x * psiz` and `line.get() + roi_x * psiz`; `line` is `std::unique_ptr<T[]>` with `T = uint16_t` here), doubling the source offset and over-reading `line` for any `roi_x > 0`; the uncompressed twins at `:733`/`:765` use `line.get() + nc * roi_x` / `line.get() + roi_x`. Align to the element-unit form (the bps-16 cases at `:809`/`:843` read `scanline`, a byte buffer, so their `* psiz` is correct). Same commit
- [x] `SipiIOTiff.cpp:704`: keep `sll = TIFFScanlineSize(tif)` (bytes) as the allocation and reject when it is smaller than what the copy and the unpackers consume: `samples_per_line = (planar == PLANARCONFIG_CONTIG ? uint64_t(nx) * nc : uint64_t(nx))`, `expected_sll = (samples_per_line * bps + 7) / 8`; `sll < expected_sll` → `kMalformedInput`. (A plain `sll >= nc * nx * psiz` would reject every valid 1/4/12-bit TIFF and miss the one-sample-wide planar-separate scanline.) `roi_x + roi_w <= nx` is already guaranteed by `crop_coords` clamping (`SipiRegion.cpp:126-136`)
- [x] `SipiIOTiff.cpp:938-951`: check `TIFFTileSize(tif) > 0` (it is `tmsize_t`, signed; the `uint32_t` truncation at `:938` hides `-1`); for `PLANARCONFIG_SEPARATE` only, allocate a plane buffer of `nc * plane_elems` (`plane_elems = bps == 8 ? tile_size : tile_size >> 1`; for CONTIG `TIFFTileSize` already includes `nc`), read each plane with `TIFFReadTile(tif, plane + c * plane_elems, x, y, 0, /*sample=*/c)` (the 6th argument; `:943` passes `0` today), and de-interleave into a **separate** destination buffer allocated once outside the tile loop. The current `tilebuf = separateToContig(std::move(tilebuf), …)` reassignment inside the loop (`:951`) must go: after the first tile it hands a differently-sized interleaved buffer to the next `TIFFReadTile`
- [x] `SipiIOJ2k.cpp:721` (decode): `std::vector<int> stripe_heights(codestream.get_num_components(), dims.size.y)` — size from the **codestream** component count, which is what `pull_stripe` iterates, not `img->getNc()`; give the `get_signed` vectors at `:749`/`:774` the same count (same latent under-sizing). `:1564` (encode): `std::vector<int> stripe_heights(img->getNc())` — `compressor.start(..., img->getNc(), ...)` at `:1559` fixes that count and the three assignment loops overwrite every element. Pass `.data()` at all six push/pull sites (`:740`, `:760`, `:785`, `:1572`, `:1582`, `:1588`); the 16-bit overloads take `int *` in the same position. Confirm the overload signatures against the fetched `@kakadu` headers (license-gated, not in this checkout)
- [x] `color.cpp` `convertYCC2RGB`: reject `nc < 3` with `ErrorCode::kMalformedInput` (the codes live in `src/error/cpp/SipiValueError.h:31-38`) at the top of both the 8-bit (`:34`) and 16-bit (`:64`) paths. Same commit: the 16-bit path subtracts the 8-bit chroma midpoint `0x80` (`:80-91`) where 16-bit data needs `32768`; a pre-existing correctness bug in the same function (`fix(image_processing):`)
- [x] `SipiIOJpeg.cpp:1244-1264`: libjpeg's `write_marker_header` rejects `datalen > 65533`; the Exif (`:1244`) and XMP (`:1258`) guards become `start_l + buf.size() <= 65533` and IPTC becomes `start_l + 4 + buf.size() <= 65533` (ICC at `:1291-1299` already complies: three guards, not four). Fix the IPTC APP13 emitted length at `:1322` to `start_l + 4 + buf.size()` (record the golden change in `CHANGELOG.approval.md`). Do **not** hoist the buffers above the `setjmp` at `:1127`: a non-`volatile` automatic modified between `setjmp` and `longjmp` is indeterminate afterwards, so destroying a hoisted owner is itself undefined; the guard alone prevents the `longjmp`, and the bounded-leak comment at `:1121-1125` stays (S2-14)
- [x] `SipiIOJ2k.cpp:370-405,936-938`: `const std::int64_t len = box.get_remaining_bytes();` (Kakadu `kdu_long`, `-1` for a rubber-length box; confirm in the `@kakadu` header) and reject `len < 0 || len > kMaxMetadataBytes` with `kMalformedInput` before every `make_unique`. `kMaxMetadataBytes` does not exist yet: introduce it as a file-local `constexpr` in an anonymous namespace at the top of `SipiIOJ2k.cpp` (its only consumer; `SipiIO.h` would imply a cross-handler contract that does not exist). Check every `box.read()` return against the requested length, including the 16-byte UUID reads at `:368` and `:934` that feed `memcmp` from a possibly indeterminate buffer
- [x] `SipiIOJ2k.cpp:466,1032`: skip codestream comments whose `get_text()` is `NULL` (confirm the NULL-for-binary-comment semantics against the Kakadu SDK header in the fetched `@kakadu` repo before implementing; Kakadu is license-gated and not readable in this checkout)
- [x] `essentials.cpp:27-58` `base64Decode`: compute the decoded length in `size_t` integer math (`len * 3 / 4` minus padding, saturating at 0; `calcDecodeLength` returns `-1` today and `buffer(decodedLength + 1)` becomes a zero-size vector) and bound `BIO_read` by `buffer.size()` (S2-38)
- [x] `validate_decode_dims` on the Essentials fast path in `read_shape` (J2K `SipiIOJ2k.cpp:950-959` and TIFF `SipiIOTiff.cpp:1715`) and in `read_watermark` (`SipiIOTiff.cpp:436-506`) (S2-38)
- [x] `SipiIOPng.cpp:249` and `:492`: `png_set_chunk_malloc_max(png_ptr, 16u << 20)` plus `png_set_chunk_cache_max` after **both** `png_create_read_struct` sites, after confirming the BCR libpng is built with `PNG_USER_LIMITS_SUPPORTED` (the calls are no-ops otherwise) (S2-38)
- [x] `SipiIOJ2k.cpp` `jp2_colour::init`: fall through to the colorspace guess when `icc_buf.empty()` at all six call sites (`:1388`, `:1393`, `:1407`, `:1412`, `:1421`, `:1447`), and catch `iccBytes()`'s possible `SipiError` there (the surrounding `try` catches only `kdu_exception`) (S2-38)
- [x] `compose.cpp:365-403` `subtract`: `maxmax == 0` means identical inputs; decide and record in the code comment: fill the output with the mid-grey the formula converges to (`UCHAR_MAX/2` / `USHRT_MAX/2`) rather than an early `return {}` that leaves `lhs` untouched, since `subtract` feeds the `compare` verb. Recommendation: mid-grey fill
- [ ] `just bench decode` before/after on the same `-c opt` binary and machine for the TIFF and J2K handlers (hot-path rule); trust a delta only per `docs/src/development/benchmarking.md`
- [x] Approval suite byte-identical except the recorded IPTC change; `just bazel-test` green on all three platforms

#### Phase 2: Fuzz workflow recovery and harness coverage (S2-31, S2-32, DEV-7081) (DEV-7134)

_Execution: opus, medium (harness input-shaping and log-flood handling are design
choices; the workflow edits are mechanical)._

Dependency: the workflow-recovery items (warning handler, log cap, reproducer download,
DEV-7081, `continue-on-error`) are independent of Phase 1 and may land first as their own
commit; the new-target seeding waits for the Phase 1 fixtures.

- [x] Route libtiff warnings in the fuzz harness to a counter, not stdout (`TIFFSetWarningHandler` in the harness `main`/init only; production logging unchanged) so `libfuzzer.log` stays in the MB range
- [x] Cap `libfuzzer.log` / `merge.log` in `fuzz.yml` at the file, not only the console: `tee >(tail -c 50M > .fuzz/libfuzzer.log)` (or a post-run truncation step), since a `| tail` after the existing `tee` (`:235`, `:262`, `:312`) caps only the GitHub console while the artifact upload (`:331-341`) ships the full file. One mechanism: once the warning handler above lands, `-close_fd_mask` is not needed
- [x] Harness consumes a leading fixed-size header from the fuzz input as `(region x,y,w,h ; size w,h ; rotation ; reduce)` and calls the region/size-aware `read` overload (S2-32); keep the plain `read` as a second target so existing corpora stay valid
- [x] Add `{tiff,jpeg,png,j2k}_roundtrip_fuzz` `cc_fuzz_test` targets in `src/format_handlers/fuzz/BUILD.bazel` that decode then `write()` to each output format into a temp file (encoder coverage for S2-02 and S2-14)
- [x] `fuzz.yml`: four new `matrix.include` rows (`target`, `bin`, `corpus_out`, `max_len`, `dict`, `asan_options`) for the round-trip targets; the `matrix.target`-scoped concurrency group (`:126-130`) and the job-level `GH_TOKEN` (`:75-84`) need no change
- [x] `docs/src/development/fuzzing.md`: update the target list, the leg count ("5-leg matrix"/"all five" become nine) and the per-target `-max_len`/dictionary/corpus tables
- [x] Seed the new targets from the Phase 1 fixtures and the smallest valid Service Files under `test/_test_data/images/unit/`; regenerate AFL++ dictionaries where the header prefix changed
- [x] Raise J2K throughput by reducing `-max_len` for the header-prefixed target and record exec/s before/after in the PR. Do **not** add `-jobs=2 -workers=2`: libFuzzer's job mode writes each worker's output to `fuzz-<N>.log` in the CWD, bypassing the `tee` at `fuzz.yml:235` and the `.fuzz/*.log` artifact glob at `:331-341`, and the ASan-paired pass (`:299-312`) gains nothing from two workers under ASan on a 4-vCPU runner
- [x] Download the DEV-7080 reproducer (`timeout-7e09ed207028df93676ff1568eca38c6abdcc091`, artifact `fuzz-crashes-j2k` of run 33254456688) and the current nightly's `timeout-a447eb95…` before the artifacts expire; commit both under `test/_test_data/images/hang/` (LFS), a directory no corpus-replay target loads
- [ ] Record the nightly runtime cost of the added targets in the PR; file a Linear follow-up (per the follow-ups rule) to sunset the plain-`read` targets once their corpora are migrated into the header-prefixed targets, and reference it from the harness comment
- [x] DEV-7081: set `ASAN_OPTIONS=detect_container_overflow=0` on the `parse_request` leg only, with the rationale comment
- [x] Until Phase 3 lands, mark the J2K libFuzzer step `continue-on-error` with a linked comment to DEV-7080 so the nightly reports real regressions on the other legs. Caveat: `continue-on-error` on a step makes the matrix **job** report success, so the Phase 13 gate and anyone reading the nightly must inspect the step annotation (or a final step that fails on a recorded J2K-timeout flag) while the exception is in place; remove it in Phase 3
- [ ] `fuzz.yml` `workflow_dispatch` run green on all five legs on the branch; corpus replay green in `bazel test //src/...`

#### Phase 3: JP2 decode watchdog (S2-13, DEV-7080) (DEV-7135)

_Execution: opus, high (choosing the interruption mechanism for a licensed C++ library
that cannot be modified is the design decision of this plan)._

- [x] Design note in this folder (`02-jp2-decode-watchdog-design.md`): options are (a) a structural pre-pass over the JP2 box chain rejecting zero-length boxes before Kakadu opens the file (input validation at the genuine boundary; covers the known class only), (b) a wall-clock deadline at the FFI seam that fails the request when `read_shape`/`read` exceeds it (an availability guard over a library that cannot be patched, not input validation; a wedged thread cannot be joined, so the note must state how the leaked thread is bounded and surfaced). Pick exactly **one** (the plan's own rule: one layer, with the reason); "both" is the defense-in-depth CLAUDE.md rules out. Recommendation: (b), because the maintainer ruling already names the seam and (a) only covers the box shape the fuzzer happened to find
- [x] Unit test with the DEV-7080 reproducer committed in Phase 2 (`test/_test_data/images/hang/`) under a test deadline — red on `main` by timeout (the hang) before the mechanism lands
- [x] Implement the chosen mechanism at `serve_image.cpp` (seam) or `SipiIOJ2k.cpp` (box pre-pass)
- [x] If (b): surface and bound leaked engine threads with a `wedged_threads` gauge, following CONVENTIONS.md § Metrics end to end (the offset lock alone is not the path): the `Sipi::observability::Metrics` singleton member and its bump site (`src/observability/cpp/metrics.h`), the `SipiMetricsSnapshot` field plus size/offset asserts (`src/ffi/cpp/metrics_snapshot.h`), the populate site (`src/ffi/cpp/sipi_ffi.cpp`), the Rust mirror and layout test (`src/server/rust/src/ffi.rs`), the GAUGES row (`src/server/rust/src/metrics.rs`), and the inventory count in `metrics_registry_test` with `every_snapshot_field_is_accounted_for`. Exported over OTLP and reported by `/health` (payload documented in `docs/src/operation/health-endpoint.md`, also consumed by the Docker `HEALTHCHECK` and UptimeRobot; Phase 6a edits the same file), with a documented restart rule when it reaches `nthreads - 1`; without this, a wedged pool at `SIPI_NTHREADS=2` is a silent outage
- [x] Remove the Phase 2 `continue-on-error` exception; J2K leg green on a dispatch run <!-- code removed from fuzz.yml; the "green on a dispatch run" half is a CI action for the maintainer -->


#### Phase 4: IIIF input bounds and engine DoS (S2-05, S2-27, S2-37) (DEV-7136)

_Execution: sonnet, high (parser changes are spec-driven; the default-flip decision is
recorded here, not made by the executor)._

- [x] Property test (red) in `test/e2e/tests/proptest_iiif_uri.rs` (the only `proptest!` site today; `proptest` is an e2e-only crate at `MODULE.bazel:596`) generating digit-only rotation/region/pct strings of 39 to 45 digits and asserting 400; a parser-crate unit test would need `@crates//:proptest` added to `src/iiifparser/rust/BUILD.bazel`, which is not worth a new dependency edge for one generator. Red on `main` (the oversized rotation hangs; the proptest is deadline-bounded)
- [x] Fuzz corpus (red) for `parse_request` gains the oversized-rotation inputs; e2e test `GET /unit/lena512.jp2/full/max/99999999999/default.jpg` → 400
- [ ] e2e (S2-27, red): assert a truncated image body is not a clean EOF, using a fixture that fails mid-encode (a JP2 whose decode read-errors partway, matching Sentry SIPI-1Q); red on `main`, which delivers the partial as a clean `200` chunked EOF today
- [x] `parse.rs` `parse_rotation` (`:256-267`): reject anything outside `0.0..=360.0` (IIIF 3.0 §4.3, 360 inclusive) and non-finite results, returning the existing `ParseError(String)` shape; `parse_region`'s four coords (`:129-133`) and `parse_size`'s `pct:` (`:177`) reject non-finite. The `w,h`/`w,`/`,h` size branches already fail through `usize::from_str` (`parse_dim`, `:206-209`) and need no change. `consume_posfloat` (`:22-28`) already excludes `inf`/`nan`/negatives/exponent forms at classification, so the only reachable shape is a digit-only string of 39+ digits overflowing `f32::from_str` to `inf`
- [x] `parse.rs:251-252` clamps `nx`/`ny` to 32 000 but leaves `percent` free, so `^pct:1000000` on a 5000-pixel source yields a 5e10-pixel output that is finite and admitted under the shipped `basic` mode: clamp or reject `pct:` so the derived output cannot exceed the same 32 000 cap. Region `COORDS` outside `0.0..=2_147_483_647.0` → 400 (`crop_coords` does `lroundf` into `int`, `SipiRegion.cpp:74-77`, undefined for larger values); region `pct:` outside `0.0..=100.0` → 400 (S2-05)
- [x] `serve_image.cpp` `ImageEncodeProducer::produce`: a post-commit encode failure signals the sink so `cb_write`'s owner returns `Err(BodyAbort)` and hyper resets the connection (S2-27; live in prod as Sentry SIPI-1Q, a JP2→JPEG write error)
- [x] `serve_response.cpp:34-38`: length-cap `Range` (a header over 128 bytes → 400) before the regex (S2-37)
- [x] No benchmark gate: the changes are in the Rust parser and the response sink, and the `just bench` tiers (`parse|decode|encode|process`) are C++ Google Benchmarks with no request-path tier (`justfile:449-464`); the hot-path rule applies to codec/`SipiImage` changes, which this phase does not make

#### Phase 5: Authorization semantics (S2-04, S2-09, S2-16, S2-43) (DEV-7137)

_Execution: opus, high (IIIF-facing behavior for restricted images is decide-and-
implement; several choices affect viewers)._

_Test-first: the two e2e checkboxes below are written and confirmed red on `main@5007032c`
(restricted images reconstructable, restrict-to-nothing served) before the fixes land._

- [x] e2e (red): restricted image with `!128,128` **and** with `pct:10`: region requests at native scale and with an absolute `w,h` size return tiles at the restricted sampling factor; a restrict hook with no size/watermark, with `size = "max"` and with `size = "pct:100"` → 403; `restrict` on `/file` → 403; `knora.json` on a `deny` path → 404; `info.json` on restrict reports restricted dims
- [ ] e2e matrix (red), preflight decision × credential channel: for each of `allow`, `restrict`, `deny`, `login`, `clickthrough`, `kiosk`, `external` and each of {anonymous, `Cookie`, `Authorization: Bearer`}, assert the image route, `info.json` and `knora.json` agree; with the preflight cache enabled (TTL 2 s), assert an anonymous request never receives a decision populated by a Cookie or Bearer request and that Cookie and Bearer are distinct key material
- [x] `serve_image.cpp:543-557`: for **every** non-`undefined()` `restricted_size` (absolute and `pct:` alike; the bypass is in the `*size > *restricted_size` comparison at `:557`, which compares full-image outputs, not in the restriction type), derive a scale factor rather than an absolute box: `region->crop_coords(img_w, img_h, rx, ry, rw, rh)` (inside the existing `try` at `:550`; it throws `SipiError` for out-of-image coordinates and sets `canonical_ok` that `build_canonical_url` at `:568` relies on, so the call order is idempotent but must be asserted), `f = min(rest_w / img_w, rest_h / img_h)`, and when the requested output for the region exceeds `rw * f × rh * f`, replace `size` with a percent `SipiSize(f * 100)`. The invariant is "effective sampling factor ≤ f for any region", which composes across regions where an output box does not. Record the "scale, not reject" decision in the code comment (S2-04)
- [x] Two residuals, recorded in the same comment and the register: (1) many overlapping sub-pixel-offset regions each resampled at `f` from the full-resolution source form a super-resolution attack that recovers more than one `f`-scaled image — accepted (the alternative is rejecting region requests on restricted images, which breaks viewers); (2) the canonical `Link` header emits the region in native pixel coordinates (`SipiRegion::canonical`, `serve_image.cpp:262`), so `pct:0,0,100,100` discloses native W×H — decide and record: omit the canonical `Link` header for non-`Allow` permissions (recommendation) or scale its coordinates
- [x] `src/ffi/cpp/serve_response.h:45-55`: add `Forbidden = 403` to `enum class SipiStatus` (today: `Ok`, `BadRequest`, `NotFound`, `InternalError`, `ServiceUnavailable`, `ClientGone`); the Rust side renders the status as a plain integer (`src/server/rust/src/ffi.rs:465` doc comment lists the codes; update it). No offset-locked struct changes
- [x] `serve_image.cpp:557,603`: test the *resolved* restriction, not the presence of the kv: after `restricted_size->get_size(img_w, img_h, rest_w, rest_h, …)`, require `rest_w < img_w || rest_h < img_h || !watermark.empty()`, else `SipiStatus::Forbidden` at seam entry. This catches the missing kv, `size = "max"`, `size = "pct:100"`/`^pct:200` (`undefined()` is `nx == 0 && ny == 0 && percent == 0`, `SipiSize.h:168`, so a `pct:` value passes it) and never reaches the raw-file passthrough predicate at `:603` (S2-09)
- [x] `/file` route: `file_access` (`routes.rs:655-658`) accepts `Restrict` and `serve_file` streams the original with no clamp or watermark possible; a `restrict` decision on `/file` → 403 (S2-09)
- [x] `routes.rs:436-445`: gate `KnoraJson` by the same `Allow | Restrict` predicate as `Iiif`; `serve_info_json` builds the dims payload before the auth branch (`routes.rs:1829-1860`), so for **every** non-`Allow` permission (`restrict`, `login`, `clickthrough`, `kiosk`, `external`) pass clamped `SipiImageDims` into `info::image_info_json` (`info.rs:91-127`, which derives `sizes`/`scaleFactors` via `pyramid_scale_factors` at `:112`) so the 401/restricted bodies never carry the native tiling grid (S2-16); `Deny`/`Login`/… on `knora.json` behave as on the image route
- [x] The restricted size arrives as an IIIF size string in `access.kv["size"]`; expose `pub fn parse_size(&str) -> Result<SizeKind, ParseError>` from `src/iiifparser/rust/lib.rs`, returning the already-exported domain type (`SizeKind`) rather than making the private `SizeParts` struct `pub` (the crate's public vocabulary at `lib.rs:17-18` stays narrow; `SizeParts` remains private), so `server` never re-implements a security-relevant clamp
- [x] `info.json` keeps `Access-Control-Allow-Origin: *` without credentials (a browser cannot read a credentialed cross-origin response under `*`, so authorized dims are not exposed cross-origin). Both `info.json` and `knora.json` gain `Vary: Cookie, Authorization` and `Cache-Control: private, no-store` whenever a preflight hook is configured (`knora.json` has neither today, `routes.rs:1911-1912`), so an intermediary cache never serves an authorized payload to an anonymous client. `json_response` (`routes.rs:2029-2081`) can only emit `Vary: Origin` via its `vary: bool` parameter (`:2055-2057`); change the parameter to a `&[&str]` list, updating the knora.json caller at `:1918` and the doc comment at `:2027-2028` (S2-16)
- [x] `preflight_cache.rs:139-168` `make_key`: do **not** add the raw query string (the cycle-2 wording). The shipped dsp-api hook reads only `prefix`, `identifier`, `Cookie`, `Authorization` and `server.request["token"]` (`authentication.lua:134-164`), and preflight stays query-free, so keying the query buys nothing today and would turn `…/default.jpg?x=<random>` into an unbounded cache-busting amplifier against dsp-api (one `/admin/files` call and SPARQL query per anonymous request). The structural protection is the explicit `RequestData` literal and its classification test (two checkboxes down); if query fields are ever exposed to the hook, key a normalized allowlist of consumed parameters, never the raw string. Update the module doc (`:16-25`), the inline comment at `routes.rs:523-529`, and the **Preflight cache** glossary row to state this (S2-43)
- [x] Decide and record in the `preflight_cache.rs` module doc: preflight `build_request_data` (`routes.rs:693-704`) stays query-free (recommendation; dsp-api's `?token=` path then keeps failing closed), and the key carries the query anyway so a later change cannot open the bypass
- [x] `routes.rs:693-704`: replace `..Default::default()` with an explicit field-by-field `RequestData` literal (the pattern `OverridesHolder::new` uses at `config.rs:427`), so a new `RequestData` field fails to compile until it is classified as keyed or excluded; the unit test asserts that classification list against `make_key`'s inputs
- [ ] Coordinate with dsp-api **before Phase 13**: `sipi.init.lua:129-141` returns a bare `{type = "restrict"}` when `permission_info.restrictedViewSettings` is `nil`, which today streams the original at full resolution (S2-09 is live in DSP for every restricted-view image without settings) and after this phase yields 403. dsp-api must default a size (e.g. `!128,128`) in that branch, and its CHANGELOG notes both the 403 and the restricted-image `info.json` dims change
- [x] `docs/src/lua/index.md` and `UBIQUITOUS_LANGUAGE.md:126`: refine the existing **restrict** permission row ("size cap and/or watermark") so the size cap is stated as a bound on the effective sampling factor of *any* region, not on the full image; do not coin a second term. Phases 6c and 7 also edit `docs/src/lua/index.md`; Phase 5 lands first of the three

#### Phase 6a: HTTP shell, host trust and docroot hardening (S2-17, S2-40) (DEV-7138)

_Execution: opus, medium (every change is named)._

- [x] e2e (red): hostile `X-Forwarded-Host` → 303 `Location` and `@id` carry the configured host; two requests differing only in `X-Forwarded-Host` share one cache entry. Red on `main` (host reflected)
- [x] `routes.rs:2086-2097` `forwarded()`: accept `X-Forwarded-Host`/`Host` only when it is on the `SIPI_PUBLIC_HOSTS` allowlist (comma-separated; empty = today's behaviour), read from env in `AppState::load` next to `allowed_origins` (`lib.rs:451-453`, `routes.rs:86`) exactly like the DEV-6061 knob; otherwise substitute the first allowlisted host. `config.hostname` is **not** the validation source: it is Lua-config-only, parse-only on the CLI (`network.rs:20-23`), never crosses the seam (`config.rs:427`) and defaults to `localhost` (`lib.rs:573-576`). `forwarded()`'s callers (`:692`, `:999`, `:1821`, `:1881`, `:1924`) then all see the validated host, so the 303 `Location`, `@id`, `Link` and Lua `server.host` are covered in one place (S2-17)
- [x] Cache key: **keep** the host in `build_canonical_url`'s `.second` (`src/ffi/cpp/serve_image.cpp:148,262`). Once `forwarded()` validates against `SIPI_PUBLIC_HOSTS`, the host component is bounded to the allowlist, which closes the eviction lever (S2-17) without touching the key. Dropping the host (the cycle-2 wording) would cross-serve derivatives whenever a hook maps `(host, prefix, identifier)` to different files, because the key contains neither the host nor the resolved `infile`. No glossary change
- [x] `routes.rs:719-725`: traversal and not-found both answer 404 on the image path; docroot skips dotfiles (S2-40)
- [x] Decide and record in `docs/src/operation/health-endpoint.md`: `/health` keeps `version` (recommendation: UptimeRobot and the release-train checks read it, and the Docker image tag already discloses the version) or drops it. Phase 3 also edits this file (`wedged_threads`); whichever lands second rebases

#### Phase 6b: HTTP shell, resource exhaustion and shipped defaults (S2-18, S2-19, S2-20, S2-21, S2-33) (DEV-7139)

_Execution: opus, medium (the serve-loop replacement is a design change with a
graceful-drain contract to preserve; the rest names the site)._

- [x] e2e: a handler exceeding `SIPI_REQUEST_TIMEOUT` answers 408 (S2-18, `handler_exceeding_request_timeout_answers_408` in `resource_limits.rs` — `/hardening/loop` route with `SIPI_LUA_TIMEOUT_MS` high so axum's TimeoutLayer, not the Lua deadline, fires first). Option (b) chosen for header-read timeout, so no slow-header/SIGTERM e2e
- [x] e2e (red): a client that trickles its request body is cut at `SIPI_BODY_READ_TIMEOUT` and never holds a Full permit. Red on `main` (no body-read timeout today; the trickling client is served or hangs) (S2-19) — `trickling_body_is_cut_off_by_body_read_timeout` in `resource_limits.rs` (raw socket, 8 s failsafe, completes ~2.7 s)
- [x] a decode over the budget via `SipiImage.new` is refused by the budget (S2-20) — covered by C++ unit tests `ImageNewRefusedWhenDecodeExceedsAdvancedBudget` / `ImageNewSucceedsWhenBudgetDisabled` in `seam_probe_test.cpp` (deterministic tiny-budget injection; no bomb fixture needed — a full bomb-PNG e2e is disproportionate). ~~a 65-part multipart body → 400~~ done in S2-19 (`multipart_part_count_over_limit_answers_400`)
- [x] Add a "shipped defaults are safe" test (red) over the effective config with no overrides: `admission_mode == advanced`, `max_post_size` finite; fails if a default regresses. Red on `main` (shipped default is `basic` with unlimited `max_post_size`) — C++ `SeamProbe.ShippedAdmissionModeDefaultIsAdvanced` (default-constructed `SipiConf`) + Rust `config::tests::default_max_post_size_is_finite`
- [x] `MODULE.bazel`: add `crate.spec(package = "tower-http", version = "0.6", features = ["timeout"])` next to the `tower` spec at `:547` and relock (the `tower` spec stays `["util"]`); a `MODULE.bazel` edit triggers the three-platform `just bazel-build` gate. `tower::timeout::TimeoutLayer` is **not** usable directly: its error type is `BoxError` and an axum 0.8 `Router` requires `Infallible`, which would need `HandleErrorLayer` plumbing; `tower_http::timeout::TimeoutLayer` is infallible and answers 408
- [x] `lib.rs:489`: `tower_http::timeout::TimeoutLayer` on the router with `SIPI_REQUEST_TIMEOUT` (default e.g. 60 s, must exceed the admission `queue_timeout` plus the largest expected full decode). It bounds the handler future up to the response head (request-body read, preflight, engine dispatch) and does not bound a streaming image body after `Outcome::StreamHead` (`routes.rs:1145-1148`). On the IIIF path the `Permit` is moved into `spawn_blocking` (`routes.rs:338-339`) and dropping the handler future does not cancel that task, so the 408 bounds client-visible latency, not permit occupancy; record this in the `lib.rs` comment (S2-18)
- [x] Decide and record: header-read timeout and connection cap. `axum::serve` (`lib.rs:489-507`) exposes no hyper builder, and neither `hyper` nor `hyper-util` is a declared crate. Option (a): add `hyper` + `hyper-util` (`server`, `http1`, `tokio` features) and replace `axum::serve` with a `hyper_util::server::conn::auto::Builder` accept loop that sets `.http1().header_read_timeout(SIPI_HEADER_TIMEOUT)`, caps concurrent connections with a `Semaphore` (`SIPI_MAX_CONNECTIONS`), and re-implements the graceful drain (`drain_tx`/`watch` + `drain_timeout`, `lib.rs:475-507`) with an e2e regression for SIGTERM draining. Option (b): keep `axum::serve` and tighten Traefik's `respondingTimeouts.readTimeout` on the shared `websecure` entrypoint, which is `60m` today (`ops-deploy roles/traefik/templates/traefik.yml.j2:47-49`) and so protects nothing as configured; that is a cross-service `roles/traefik` change (Phase 11) and leaves no in-process connection cap. Recommendation: (b) for this PR with the Traefik tightening as a hard Phase 11 item, (a) filed as a Linear follow-up if a slow-header incident is ever observed. Ruling recorded in the PR description and in a comment next to `axum::serve` in `lib.rs`
- [x] `routes.rs:1103`: **keep** `state.admission.acquire(AdmissionKind::Full)` after the body read. Moving it before the read (the cycle-2 wording) would let a client trickling one byte per 50 s hold a Full permit for the whole `SIPI_REQUEST_TIMEOUT`; at production `SIPI_NTHREADS=2` two such POSTs take the service down, the S2-05 shape re-created. Bound the read instead: the finite `DefaultBodyLimit` (next checkbox) plus `tower_http::timeout::RequestBodyTimeoutLayer` with `SIPI_BODY_READ_TIMEOUT` (e.g. 10 s) on the Lua-route (`:974-978`) and docroot (`:1365-1369`) routers, so the multipart spool (`:1019-1080`) and the raw `to_bytes` path (`:1087`) are both time- and size-bounded without a permit. Cap multipart parts at a constant (64) with 400 beyond (S2-19)
- [x] `routes.rs:181` (`AppState::load`, the single source of `max_post_size`): `ffi::max_post_size().unwrap_or(0)` becomes `.filter(|&n| n > 0).unwrap_or(DEFAULT_MAX_POST_SIZE)` (e.g. `256M`); the three `== 0` unlimited branches (`:974-978` `DefaultBodyLimit::disable()`, `:1082-1086` `usize::MAX`, `:1346` docroot) become dead and are deleted (S2-19)
- [x] `image_handle.cpp:94-151`: `sipi_image_new` charges `eng.memory_budget` exactly like `serve_image.cpp:655-656` and returns the existing engine error string on refusal (S2-20)
- [x] `routes.rs:1417-1444`: docroot route takes a `Tile` permit; libmagic database loaded once (S2-21)
- [x] Decide and record: flip the shipped `admission_mode` default to `advanced` (production already runs it; generic deployments get memory enforcement) **or** change `parse_size` to reject `> 32 000` instead of clamping. Recommendation: flip the default; keep the clamp. End-user visible for generic deployments (413/503 where `basic` admitted), so `feat(throttling)!:` with a `BREAKING CHANGE` footer; update `docs/src/operation/admission-control.md` (S2-33)
- [x] Docs for `SIPI_REQUEST_TIMEOUT`: `docs/src/guide/running.md` env table + `docs/src/operation/admission-control.md` pool-knobs list (`sipi.md` skipped — env-only knob, matching the `SIPI_ALLOWED_ORIGINS`/`SIPI_PUBLIC_HOSTS` decision). Option (b) chosen, so no `SIPI_HEADER_TIMEOUT`/`SIPI_MAX_CONNECTIONS` (S2-18)
- [x] Docs for `SIPI_BODY_READ_TIMEOUT`: `running.md` env table + `admission-control.md` (S2-19)
- [ ] No benchmark gate: a `TimeoutLayer` is one timer per request and no `just bench` tier covers the request path (`justfile:449-464`, C++ tiers only); record a before/after `oha`-style 60 s smoke against the local server in the PR description for the record

#### Phase 6c: Cookies, client identity and fairness ruling (S2-24, S2-34, S2-44) (DEV-7140)

_Execution: opus, medium (the fairness ruling is a design decision that determines
whether `client_ip` survives)._

- [x] Decision first, recorded as an ADR-0022 amendment (two-lane admission control; if the ruling is (b) the amendment states "no SIPI code; enforced in Traefik" so the seam decision stays legible): per-client fairness (S2-34). Options: (a) per-`client_ip` cap on concurrently held permits inside `Admission::acquire` (keeps `client_ip` on the seam and re-introduces XFF trust as a security input), (b) Traefik `inFlightReq` middleware keyed on client IP (operator side, no SIPI code; Phase 11 carries the diff), (c) accept. Recommendation: (b)
- [x] e2e (red): a script setting two cookies emits two `Set-Cookie` headers with `HttpOnly; SameSite=Lax`. Red on `main` (`http_only=false`, no `SameSite`, and the second `Set-Cookie` replaces the first)
- [x] Only if the ruling is not (a): drop `client_ip` from `SipiServeRequest`. Sites: `src/server/rust/src/ffi.rs:308` (field) and the `offset_of!` block at `:1421` (re-baseline every following offset), `src/ffi/cpp/sipi_ffi.h:210` (field) and the `static_assert` block at `:500` (likewise), `routes.rs:760` (`c_client_ip`) and `:776` (assignment), and the fixture at `src/ffi/cpp/serve_image_test.cpp:72`. The engine never reads it (no consumer in `src/ffi/cpp`). The Lua-facing `RequestData.client_ip` / `server.client_ip` (`bindings/mod.rs:50`, `server.rs:202`, `routes.rs:695,1000`) is a separate path and stays (S2-44)
- [x] `bindings/mod.rs:79-104`: `http_only` default becomes `true` (`:99`); `ResponseCookie` gains a `same_site` field (there is none today) rendered as `SameSite=Lax` by default in `render()` (`:104`), with a `sendCookie` option in `server.rs` to override; update the struct doc at `:77-78` and the pinned expectation in `bindings_tests.rs:184` (`"sid=s3cr3t; Path=/; Secure; HttpOnly"`) (S2-24)
- [x] `sink.rs:316-330` `apply_headers`: use `map.append` **only** when the name is `header::SET_COOKIE` and keep `map.insert` (last-write-wins, rationale at `:310-315`) for every other header; each cookie already arrives as its own `("Set-Cookie", render())` pair (`bindings/mod.rs:190,210,232`) (S2-24); update `docs/src/lua/index.md`

#### Phase 7: Secrets, shipped scripts and Lua hygiene (S2-11, S2-22, S2-23, S2-39, S2-42, S2-46) (DEV-7141)

_Execution: sonnet, high (each item is a named file and change)._

- [x] e2e + Rust unit test (red): startup refuses the default secret; `token.lua` with a hostile `messageId` returns 400 (only if kept); `upload.lua` without a token returns 401; a Rust unit test on the VM profile asserts `print` output reaches `tracing`, not stdout. Red on `main` (default secret starts; `token.lua` reflects; `upload.lua` has no auth; `print` goes to stdout)
- [x] `lib.rs:252-287` `server_main`, inside the `configured_routes.is_some()` arm after `probe_hooks()` returns (`:274-281`): refuse startup when `effective.jwtkey` (`:270`) is absent, empty, shorter than 32 bytes, or equals the shipped literal (kept as a `const` in `lib.rs`, since the literal leaves `config/sipi.config.lua` in this phase), using the same fail-closed `tracing::error!` + `flush_telemetry` + `ExitCode::FAILURE` shape as `:276-280`. The condition is `configured_routes.is_some()` **alone** (any `--config`): `generate_jwt`/`decode_jwt` are registered on every VM (`bindings/server.rs:129-136`), and routes-only and docroot-only configurations reach a VM with no init script, so gating on `initscript` would leave them minting tokens under an empty key. `SIPI_JWTKEY` documented as required for any Lua-using deployment (S2-11)
- [x] `config/sipi.config.lua`: remove the literal `jwt_secret` and the `admin` block from the shipped config; the image `cmd` config carries a comment that the secret must be supplied by env; move the example values to `docs/src/guide/running.md`
- [x] Delete the dead `adminuser`/`adminpasswd` surface end to end: clap args (`--adminuser`, `--adminpasswd`, `SIPI_ADMINUSER`, `SIPI_ADMINPASSWD`), `ServerOverrides`, `SipiServerConfig` fields (relock), `SipiConf` setters/getters, `server/cache.elua`'s `authorize_page` usage (S2-42). Removing CLI flags an operator may pass is `refactor(cli)!:` with a `BREAKING CHANGE` footer
- [x] Decide and record: `scripts/token.lua` and its `/api/token` route are **deleted** (an iframe `postMessage` token relay is not a pattern SIPI should ship; not in the Docker image; no DSP usage) rather than hardened (S2-22). If the maintainer keeps it: `messageId` must match `^%d+$`, `origin` matched against `SIPI_ALLOWED_ORIGINS`, payload built with `server.table_to_json`
- [x] `scripts/upload.lua` stays as the reference upload example (the e2e suite drives it) and is hardened: require a valid token (`authorize_api`) at the top; derive the stored basename from `server.uuid62()`, never from `origname`; delete the dead non-image branch; the e2e fork `test/_test_data/scripts/upload.lua` is deleted and the test loads `scripts/upload.lua` so there is one script (S2-23, wave-1 maintainer flag)
- [x] Startup validation (boundary: config parse) at `lib.rs:229-237`, after `ffi::init(&effective)` and before the Lua env is built, where `docroot`, `imgroot`, `tmpdir`, `scriptdir` and `cache_dir` are all resolved on `effective` (`config.rs:91`, `lib.rs:579-599`): refuse to start when `docroot` is inside or equal to any of the other four (the docroot executes `.lua`/`.elua`; overlap with any writable root is remote code execution) (S2-46)
- [x] `runtime.rs:49-63` `base_vm`: install a `print` shim that joins its varargs with tabs (Lua `print` semantics) and emits one `tracing` line at INFO. `BASE_SCRUB` (`:42`) is a nil-out list (`globals.set(name, Value::Nil)`, `:53-55`), not a rebinding mechanism, and `server.log`'s `(message, level)` arity (`server.rs:1246-1258`) makes a direct alias wrong (S2-39)
- [x] `server.rs:460-486` `fs_mkdir`: mask the mode with `0o777 & !0o002` (S2-39)
- [x] `server.rs:830-890` `lua_to_json`: depth counter (64) returning the existing `(false, msg)` shape (S2-39)
- [x] `server.rs:783-823` `require_auth`: constant-time compare for Basic credentials host-side (S2-39)
- [x] `docs/src/lua/index.md` sandbox section states explicitly that filesystem-taking bindings are **not** path-confined and that `docroot` must not overlap `imgroot`/`tmpdir`/a Lua-writable dir. Decide and record there: no `confine(path)` helper (recommendation: scripts are operator-supplied and the S2-46 startup check closes the one misconfiguration that turns a script bug into RCE; a helper at the ~12 path-taking bindings would be a second layer). Phases 5 and 6c also edit this file; Phase 7 rebases onto whichever landed first

#### Phase 8: Cache integrity (S2-25, S2-26, S2-45) (DEV-7142)

_Execution: sonnet, medium (record-format change is mechanical; the freshness rule is a
one-line semantic fix)._

- [x] Unit tests (red) in `src/cache/cpp/sipicache_test.cpp`: replaced-source with older or equal mtime and different size is a miss; same-size same-mtime replacement is documented as a hit (pinned so the residual is visible); forged index records are rejected; long keys round-trip. Red on `main` (mtime-only freshness; raw-struct index with 255-byte key truncation)
- [x] `SipiCache.cpp:413`: invalidate on any mtime difference and additionally compare the recorded source `st_size` (add the field to `FileCacheRecord`) (S2-25). Residual risk stated in the code comment and in `docs/src/operation/`: a same-size replacement with preserved mtime still hits; operators replacing a service file in place must touch it or purge the cache. Decide and record whether a content digest of the first 64 KiB (or a per-file cache generation) is worth its stat+read cost; recommendation: document the operational rule, no digest
- [x] `SipiCache.h:44-60` / `SipiCache.cpp:228`: persist a SHA-256 digest of the canonical key, truncated to no less than 128 bits (a short or non-cryptographic digest is forgeable and becomes a content-substitution primitive on a restricted image), instead of the 255-byte truncation (S2-26)
- [x] `FileCacheRecord` gains the source `st_size` field for S2-25; `check()` (`SipiCache.cpp:381-420`) then takes the source size alongside `mtime`; update its callers
- [x] Introduce a fixed index header (magic, version, `sizeof(FileCacheRecord)`) validated on load with `(length - sizeof(header)) % sizeof(record) == 0`; a missing or mismatching header means "start empty and log once". There is no version or magic field today and the only integrity check is `length % sizeof(FileCacheRecord) == 0` (`:125`), so changing `sizeof` would otherwise parse an old index as garbage; recording `sizeof` also makes the existing macOS/Linux `mtime` layout difference (`SipiCache.h:53-57`) fail closed (S2-26)
- [x] Record the on-disk format as a short ADR following ADR-0005's shape (`docs/adr/00XX-cache-index-header.md`: header layout, version-mismatch-means-empty rule, the same-size-replacement residual), linked from the `SipiCache.h` file-head banner
- [x] `SipiCache.cpp:137-164`: bound every `char[]` field with `strnlen`; reject any record whose `cachepath` contains `/` or whose `canonical` is empty; recompute each record's `fsize` from `stat()` at index load and drop records whose file is missing (a forged `fsize` otherwise thrashes eviction or fills the disk); unreadable index → start empty and log once (S2-26)
- [x] Cache-file reads (`full_file_body` at `serve_image.cpp:622` and the cache-hit path) open with `O_NOFOLLOW` and reject non-regular files: a symlink planted in the cache dir plus a forged record would otherwise read any file the process can (e.g. the config with the JWT secret) (S2-26)
- [x] `SipiCache.cpp:558`: underflow guard on `cache_used_bytes` (S2-45)

#### Phase 9: Dependencies and supply chain (S2-12, S2-29, S2-30, S2-41) (DEV-7143)

_Execution: sonnet, medium (version bumps and CI wiring; vendoring libexpat follows the
existing native-cc_library pattern)._

- [x] `MODULE.bazel`: libtiff → 4.7.2 (native pin; re-verify `bazel/libtiff.BUILD.bazel` codec flags) as its own `build(deps):` commit
- [x] `MODULE.bazel`: curl → `8.21.0.bcr.1` (closes DEV-6564; revisit `--@curl//:ssl_lib=openssl`) as its own `build(deps):` commit
- [x] `MODULE.bazel`: lcms2 → 2.19.1 (native) as its own `build(deps):` commit
- [x] `MODULE.bazel`: exiv2 → newest 0.28.x (native; verify the release list on GitHub) as its own `build(deps):` commit
- [x] libpng → `1.6.58` and openssl → newest `3.5.x` in a separate `build(deps):` commit so they can be dropped without touching the security bumps (labelled discovery, not a finding: routine freshness noticed while checking the registry) — libpng 1.6.58 landed as c5067b54; openssl newest 3.5.x (3.5.5.bcr.4) is held by the curl commit's single_version_override (f5ac3c18)
- [x] Vendor libexpat 2.8.4 as `bazel/libexpat.BUILD.bazel` (native `cc_library`, ADR-0015) and point exiv2's XMP SDK and the three first-party consumers (`src/image/BUILD.bazel:73`, `src/format_handlers/BUILD.bazel:79`, `src/BUILD.bazel:211`) at it; note in DEV-7075 that it depends on this bump
- [x] `bazel/libexpat.BUILD.bazel` visibility narrowed per CONVENTIONS.md § Visibility, mirroring `bazel/lcms2.BUILD.bazel`: grant only the exiv2 package and the three named consumers, never `//src:__subpackages__` or public (an XML parser with ~25 CVEs behind it must not be reachable from arbitrary engine packages once DEV-7075 makes it live)
- [x] Per bump: `just bazel-build` on all three platforms, `just bazel-coverage` green, approval goldens byte-identical (lcms2 and libtiff bumps can move ICC/TIFF output; record any golden change) — local macOS `just bazel-build` + targeted tests + approval verified per bump (byte-identical except the recorded libtiff-TIFF and lcms2-ICC re-approvals, in CHANGELOG.approval.md); linux-x86_64 / linux-aarch64 are CI's build-completeness invariant (green CI run on the branch confirms them); `just bazel-coverage` is the known non-gating/broken leg (MEMORY: sipi coverage stretch-not-gate, rules_cc #385)
- [x] CI advisories: there is no `Cargo.lock` in the repo; `crate.from_specs` resolves into `MODULE.bazel.lock`, which `cargo-audit`/`cargo-deny` cannot read. Set `cargo_lockfile` on the `crate.from_specs` call so `crate_universe` materializes a checked-in `Cargo.Bazel.lock` (Cargo.lock format), then add a `just audit` recipe (none exists today) running `cargo audit -f Cargo.Bazel.lock` and an OSV-Scanner run over the same file; wire it as a CI job that fails on Critical/High (S2-30: Docker Scout scans image layers only and cannot see the statically linked codecs; Dependabot's `cargo` entry covers only `/test/e2e`)
- [x] `.github/dependabot.yml`: document that production crates and `http_archive` pins are not covered; add a quarterly native-pin review checklist to `docs/src/development/ci.md` listing every `http_archive` dep and its upstream release page
- [x] Docker Scout: fail the `publish.yml` scan on Critical (image layers only; documented as such). No `exit-code` input is used today (`ci.yml:229-251`); verify the input name against the pinned `docker/scout-action@v1` schema before wiring it
- [x] Pin third-party actions by commit SHA (Dependabot keeps them fresh) (S2-41)
- [x] `ci.yml:401`: replace the self-referential `dasch-swiss/sipi/.github/actions/setup-python@main` with the local `./.github/actions/setup-python` form every other in-repo composite uses (`ci-setup`, `commit-lint`, `bazel-rbe`); as written it floats on `main` and Dependabot does not track it (S2-41)
- [x] Decide and record in a comment in `claude.yml`: the general job drops `contents: write` (recommendation: it can still comment on issues and PRs; a member pasting attacker-crafted text into a prompt then cannot cause a push) or gains an explicit `--allowed-tools` list (S2-41)
- [x] Add `SECURITY.md` (disclosure contact, supported versions, response expectation) at repo root (S2-41)

#### Phase 10: Observability data handling (S2-28) (DEV-7144)

_Execution: sonnet, medium (small code change plus a documented decision)._

- [x] Unit test (red) on the Sentry event builder in `ffi.rs`: `input_file` reaches the event as a basename only and `request_uri` is not an indexed tag. Red on `main` (absolute path in context; full `request_uri` as a tag)
- [x] `ffi.rs:1022-1069`: `input_file` reduced to a basename; `request_uri` moved out of Sentry tags into the context only
- [x] ADR-0018 amendment: record that production DSN is Sentry SaaS (US); decide between (a) `before_send`/minidump scrubbing is unavailable for out-of-process dumps, so route SIPI crash reports to a self-hosted Sentry, (b) disable minidump upload in production and keep panic/handled-error reporting, (c) accept with a documented retention setting. Recommendation: (b) until (a) exists; restricted image bytes must not leave the EU on a crash
- [ ] The operator-side follow-through (Sentry project scrubbing/retention or DSN change) is a Phase 11 checkbox

#### Phase 11: Deployment posture (ops-deploy; operator-applied) (S2-35, S2-36, knobs from Phases 4 to 7) (DEV-7145)

_Execution: sonnet, low (proposed diffs only; every change is applied by the maintainer)._

- [ ] Proposed diff: a `DSP_IIIF_*` default plus compose env line for every knob introduced by Phases 4 to 7 (`SIPI_REQUEST_TIMEOUT`, `SIPI_HEADER_TIMEOUT`, `SIPI_MAX_CONNECTIONS`, `SIPI_PUBLIC_HOSTS`, the finite `max_post_size` default if it moves to env), so no new knob is env-only in SIPI but absent in ops-deploy (binding rule)
- [ ] Proposed diff: remove the four dead `routes` entries from `sipi.prod-config.lua.j2`. Decide and record in the ops-deploy PR description: remove `docroot`/`wwwroute` (`/server`, `test.html`) and the `/sipi/server` mount from production (recommendation: `test.html` is a developer test page, and the docroot executes any `.lua`/`.elua` placed in the mount) (S2-36)
- [ ] Proposed diff: `:ro` on `/sipi/config` and `/sipi/server`; `read_only: true` with `tmpfs` for `/tmp` where the NFS `/tmp` is not needed; `cap_drop: [ALL]`; `security_opt: [no-new-privileges:true]` (S2-35). Verify before applying: the NFS `sipi-tmp` export's `root_squash` setting (SIPI still runs as uid 0; with `root_squash`, `cap_drop: [ALL]` can break writes that today rely on root's override), and record that `no-new-privileges` is safe for the minidump reporter (same-privilege re-exec of `current_exe`, `src/cli/rust/src/main.rs:36-56`; no setuid or file capabilities involved)
- [ ] Proposed diff: Traefik `inFlightReq` middleware on the iiif router if Phase 6c's fairness ruling chooses (b) (dsp-deploy compose labels). Separately, if Phase 6b's header-timeout ruling chooses (b): tighten `respondingTimeouts.readTimeout` in `roles/traefik/templates/traefik.yml.j2:47-49` (today `60m`, on the shared `websecure` entrypoint used by every DSP service) — a `roles/traefik` change with its own review and blast radius, not a dsp-deploy diff
- [ ] Sentry follow-through per Phase 10's decision: DSN change to a self-hosted instance, or minidump upload disabled via env, or scrubbing/retention settings on the SaaS project (read-only verification afterwards)
- [ ] Non-root (DEV-5920) and `/bin/sh` + ffmpeg removal (DEV-6321) stay their own issues; link them from this plan and raise DEV-6321's priority above DEV-5920 (it needs no NFS coordination)
- [ ] `SIPI_JWTKEY` handling is covered by Phase 0's verification; nothing further unless Phase 0 found a gap

#### Phase 12: Linear reconciliation (DEV-7146)

_Execution: haiku, low (dispositions are listed here; the work is bookkeeping)._

- [x] Close DEV-6072 with the disposition recorded in the wave-1 plan (S16 fixed, S25 gone, S15 accepted) (S2-44)
- [x] Close DEV-6075 as superseded: realloc-null fixed, strncpy absent, MEMTIFF unreachable, J2K cast = S2-02/03 (Phase 1)
- [x] Close DEV-6368 and DEV-6369 (duplicate pair; libjwt fork died with shttps, JWT is `jsonwebtoken`)
- [x] Close DEV-6117 (2025 report against the removed C++ server) or re-triage against the Rust shell with a reproduction request
- [x] Verify DEV-6640 against PR #795 (hermetic-llvm 0.8.18 unlocked macOS libFuzzer) and close if done
- [x] Verify DEV-7079 status against commit `dc12a6d1` (PNG eXIf fix) and close
- [x] Verify DEV-7132 to DEV-7147 are children of DEV-7131 with the finding IDs in each description and that their blocking edges match the dependencies stated in this plan (created 2026-09-04; keep them in sync when the plan changes)

#### Phase 13: Security release and rollout (after the one PR merges) (DEV-7147)

_Execution: sonnet, medium (the steps are the documented release train; judgment is in
reading the stage soak)._

- [ ] release-please cuts the SIPI release containing the one PR's commits (all code phases 1 to 10). Operational control: release-please accumulates every commit on `main` since the last tag into one PR that is cut only when the maintainer merges it (release-please `simple` type, `release-please-config.json`; see `docs/src/development/ci.md`); the `!` commits of Phases 6a, 6b and 7 ride in this release and cut a **major** version by design, the accepted consequence of shipping all-or-nothing
- [ ] dsp-api: bump the two `oci.pull` digests (`sipi_base_amd64`, `sipi_base_arm64`) in dsp-api `MODULE.bazel:307-332`; they are the sole source of the SIPI version (the image's own OCI label carries the tag, no `Dependencies.scala` entry exists any more). Note the restricted-image `info.json` change in the dsp-api CHANGELOG
- [ ] ops-deploy: image reference synced; Phase 0 diffs confirmed applied in every environment before the image rolls; if Phase 8 has merged, expect a cold derivative cache on first start (new index header) and roll during low traffic
- [ ] Rollout dev → stage → prod with the Phase 5 restricted-view e2e replayed against stage and a dsp-app deep-zoom check on a restricted image
- [ ] Release-readiness gate: a `workflow_dispatch` `fuzz.yml` run green on every leg (five today; nine after Phase 2 adds the round-trip targets) on the release commit; if the Phase 2 `continue-on-error` exception is still in place, the J2K step annotation is inspected because the job reports success regardless (the nightly is a monitor, not a gate)

## Operator deployment checklist (ops-deploy; run last)

All ops-deploy work is operator-applied and gathered here in execution order; the detail
and Linear issues stay in Phases 0, 11 and 13 (this is the run-order index, not a second
source of truth). Run after the one-PR has merged and the release image is built. The two
prerequisites gate the image roll per environment; the posture items follow.

- [ ] **Before the image rolls (per environment):** confirm `sipi.prod-config.lua.j2` supplies a non-default `jwt_secret` ≥ 32 bytes; if not, apply the rotation diff coordinated with dsp-api (same signing key) so Phase 7's startup check cannot trip the deploy (Phase 0 / S2-11)
- [ ] Sync the ops-deploy image reference to the new release and roll dev → stage → prod during low traffic (cold derivative cache on first start if Phase 8 shipped) — only after the two prerequisites above are applied in that environment (Phase 13)
- [ ] Add a `DSP_IIIF_*` default + compose env line for every knob from Phases 4 to 7 (`SIPI_REQUEST_TIMEOUT`, `SIPI_HEADER_TIMEOUT`, `SIPI_MAX_CONNECTIONS`, `SIPI_PUBLIC_HOSTS`, finite `max_post_size`) (Phase 11)
- [ ] `sipi.prod-config.lua.j2`: remove the four dead `routes` entries and the `docroot`/`wwwroute` (`/server`, `test.html`) + the `/sipi/server` mount (Phase 11 / S2-36)
- [ ] `docker-compose-iiif.yml.j2`: `:ro` on `/sipi/config` and `/sipi/server`, `read_only` + `tmpfs`, `cap_drop: [ALL]`, `no-new-privileges` — after verifying the NFS `root_squash` setting (Phase 11 / S2-35)
- [ ] Traefik, only if the rulings chose (b): `inFlightReq` on the iiif router (Phase 6c); tighten `respondingTimeouts.readTimeout` in `roles/traefik/templates/traefik.yml.j2` — shared `websecure` entrypoint, cross-service (Phase 6b) (Phase 11)
- [ ] Sentry follow-through per Phase 10's decision: DSN change / disable minidump / retention setting (Phase 11 / S2-28)
- [ ] Verify read-only on stage: posture flags applied; the dead routes 404 (Phase 11)

## Acceptance Criteria

- [ ] All six Critical findings have a regression fixture or test that fails on `main@5007032c` (for S2-05 the main-branch failure is a hang, so the test is deadline-bounded and fails by timeout) and passes after the fix
- [ ] `asan-ubsan` CI leg green on the Phase 1 head; a `workflow_dispatch` `fuzz.yml` run is green on every leg on the Phase 2 and Phase 3 heads (per-PR gate; while the Phase 2 `continue-on-error` exception is in place the J2K job reports success regardless, so the step annotation must be inspected); the nightly staying green for three consecutive nights is the release-readiness signal owned by the maintainer, not a PR gate
- [ ] Phase 2: the header-prefixed decode targets reach the region/size branches and the round-trip targets reach every encoder, shown by libFuzzer `-print_coverage=1` / feature-count deltas recorded in the PR (C++ lcov runs only in the separate non-gating `coverage.yml`), plus the Phase 1 fixture unit tests that exercise those branches deterministically
- [ ] A restricted image cannot be reconstructed at native resolution by region requests, for absolute **and** `pct:` restrictions (e2e); a `restrict` decision that resolves to no effective reduction (no size, `max`, `pct:100`, no watermark) yields 403 on the image route and on `/file`
- [ ] Oversized or non-finite rotation, region and `pct:` values return 400, including `^pct:` beyond the dimension cap and region coordinates beyond `i32::MAX` (e2e + proptest)
- [ ] Startup refuses an empty, default or short `jwt_secret` whenever a `--config` is given (every Lua VM registers `generate_jwt`/`decode_jwt`, so routes-only and docroot-only configurations count)
- [ ] libtiff ≥ 4.7.2, curl ≥ 8.21.0, libexpat ≥ 2.8.4, lcms2 ≥ 2.19.1 pinned; `just bazel-build` and `just bazel-test` green on darwin-aarch64, linux-x86_64, linux-aarch64 (`just bazel-coverage` is a single non-gating leg)
- [ ] A dependency-advisory step runs in CI over the crate lockfile and fails on Critical/High
- [ ] Every phase PR passes `just bazel-rustfmt-check`, `just bazel-clippy-check`, `just commit-lint`
- [ ] Approval goldens byte-identical except the two recorded changes (IPTC APP13 length; any lcms2/libtiff-induced change)
- [ ] Phase 0: the production `jwt_secret` is confirmed non-default and ≥ 32 bytes (or its rotation is applied before Phase 7)
- [ ] Phase 6a: two requests differing only in `X-Forwarded-Host` share one cache entry; the 303 `Location` and `@id` carry the configured host under a hostile `X-Forwarded-Host` (e2e)
- [ ] Phase 6b: a client that trickles its request body is cut at `SIPI_BODY_READ_TIMEOUT` without ever holding a Full permit; a slow handler is cut at `SIPI_REQUEST_TIMEOUT`; `SipiImage.new` refuses a decode over the budget; a 65-part multipart body → 400; a raw body above the finite default → 413 (e2e); the shipped-defaults test passes with `admission_mode == advanced`
- [ ] Phase 7: `/api/token` is gone (or hardened per the recorded decision); `upload.lua` without a token → 401 and stores under a `uuid62` name; startup refuses an overlapping `docroot` (e2e); `print` output reaches `tracing`, not stdout (Rust unit test on the VM profile)
- [ ] Phase 11: every `SIPI_*` knob introduced by Phases 4 to 7 has a `DSP_IIIF_*` default and a compose env line; the four dead route entries are gone from the prod config; `:ro`, `cap_drop`, `no-new-privileges` are applied — all verified read-only on stage
- [ ] Phase 6c: cookies default to `HttpOnly; SameSite=Lax`; two `Set-Cookie` headers survive (e2e); the fairness ruling is recorded in ADR-0022
- [ ] Phase 4: a post-commit encode failure resets the connection instead of a clean EOF (e2e asserts an incomplete chunked body); a `Range` header over 128 bytes → 400
- [ ] Phase 5: the preflight decision × credential matrix passes with `SIPI_PREFLIGHT_CACHE_TTL=2` set on the test server, including "anonymous never reads a Cookie/Bearer-populated cache entry"; `info.json` and `knora.json` carry `Vary: Cookie, Authorization` and `Cache-Control: private, no-store` when a hook is configured; `login`/`clickthrough`/`kiosk`/`external` responses report clamped dims, not native
- [ ] Phase 8: forged `.sipicache` records are rejected; a symlink planted in the cache dir is not followed and a forged `fsize` is corrected from `stat()` at index load; a same-mtime, different-size replacement is a cache miss; the same-size residual is documented
- [ ] Phase 10: `input_file` reaches Sentry as a basename only and `request_uri` is not a tag (unit test on the event builder); the ADR-0018 amendment is merged with the DSN/minidump decision
- [ ] Phase 13: release rolled out to prod with both `oci.pull` digests in dsp-api `MODULE.bazel` bumped and the Phase 0 diffs applied first; no `!` commit merged to `main` before the release PR was cut
- [ ] Linear reflects the register: one issue per phase, the six stale issues closed with dispositions

## Dependencies & Risks

- **Kakadu tooling** for the JP2 fixtures (S2-02, S2-03, S2-07, S2-15) is license-gated; the fixtures for S2-02 can be driven from a TIFF input instead (encode path), and S2-03/S2-07 can be pinned with hand-edited SIZ/`colr` boxes over an existing small JP2 if `kdu_compress` is unavailable.
- **DEV-7080 mechanism** (Phase 3): a wall-clock abort of a wedged Kakadu thread has no clean join; the design note must settle whether the seam kills the request and leaks the thread (bounded by `nthreads`, triggers a restart via health) or whether a box pre-pass is sufficient for the known class.
- **Restrict semantics change** (Phase 5) alters `info.json` for restricted images; dsp-app viewers must be checked against the restricted-dims pyramid before the release.
- **libtiff 4.7.2 and lcms2 2.19** may shift TIFF/ICC output bytes; approval goldens and the ICC determinism invariant (`SOURCE_DATE_EPOCH`) must be re-verified.
- **Vendoring libexpat** adds a native `BUILD.bazel` to maintain; the alternative (BCR contribution) has no timeline.
- **Reviewer-cited CVE identifiers** for libtiff and curl came from web search and were not all verified against NVD in this analysis; the libexpat and BCR version facts were verified against upstream pages. Verify before quoting externally.

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| A Phase 1 fix changes valid-image output | L | M | Approval suite byte-identical gate; per-handler before/after `just bench decode` (C++ tiers exist for the codecs) |
| Restrict scaling breaks deep-zoom viewers on restricted images | M | M | e2e with OpenSeadragon-shaped tile requests; dsp-app check before release |
| Watchdog kills leak threads until restart | M | M | Phase 3 `wedged_threads` gauge on `/health` and OTLP with a documented restart rule; design note decides the mechanism |
| Dependency bump breaks a platform build | M | M | Build completeness invariant: all three platforms before merge |
| Fuzz harness header prefix invalidates corpora | H | L | Keep the plain-`read` target; seed new targets from fixtures |
| Phase 8 index header makes every deployment start with a cold derivative cache on upgrade | H | L | Roll during low traffic (Phase 13 note); tiles regenerate on demand under admission control |
| Startup secret check trips a deployment with an unset secret | L | H | Phase 0 verifies ops-deploy supplies one before Phase 7 lands; check only when Lua is configured |

## Success Metrics

- Nightly `fuzz.yml` green for 30 consecutive days after Phase 3; new crash artifacts triaged within the next working day.
- Zero Critical/High findings from an external pentest scoped to IIIF + Lua routes after Phases 1 to 7 (recommended follow-up; not in this plan's scope).
- Dependency advisory step reports zero Critical/High against pinned versions at each release.
- Restricted-view e2e suite exists and runs in CI.
- After the Phase 13 rollout: Sentry SIPI-1Q no longer recurs as a silent write error (encode failures now reset the connection per S2-27); SIPI-1T's uncached-preflight burst rate is reviewed once the fairness ruling (Phase 6c) is deployed.

## References

- Prior plan and journal: `docs/specs/2026-08-27-sipi-codec-memory-safety/`; fuzz/value-errors: `docs/specs/2026-08-28-codec-fuzzing-and-value-errors/`; Lua runtime: `docs/specs/2026-08-20-sipi-lua-runtime-hardening/`; admission: `docs/specs/2026-08-11-sipi-cost-based-admission-control/`
- ADRs: `0015` (native cc_library), `0018` (minidump accepted risk), `0020` (oracle removal), `0022` (two-lane admission), `0023` (mlua runtime), `0024` (value-based errors)
- Linear: DEV-6418 (Done), DEV-7080, DEV-7081, DEV-7082, DEV-7075, DEV-7079, DEV-6072, DEV-6075, DEV-6564, DEV-6368/6369, DEV-5920, DEV-6321, DEV-6117, DEV-6640
- PRs: #796, #797, #789, #783, #777, #798, #795
- Fuzz runs: 33833567222 (2026-09-04), 33354275318 (first red, 2026-08-31), 33290462514 (last green, 2026-08-30); DEV-7080 reproducer run 33254456688
- Sentry (`dasch/sipi`, checked 2026-09-04): [SIPI-1Q](https://dasch.sentry.io/issues/SIPI-1Q) (S2-27, 117 events), [SIPI-1T](https://dasch.sentry.io/issues/SIPI-1T) (S2-34/S2-43 corroboration, 8.0.0), [SIPI-1S](https://dasch.sentry.io/issues/SIPI-1S) (OTLP export noise, out of scope)
- Upstream: libexpat Changes (2.8.4, 2026-08-31); libtiff tags (4.7.2, 2026-06-27); BCR `curl` (8.21.0.bcr.1), `libpng` (1.6.58), `libexpat` (2.7.1 only); Little-CMS releases (2.19.1)
- Institutional learnings: `dasch-specs/learnings/runtime-errors/handler-map-operator-bracket-null-insert-segfault.md`; `learnings/design-decisions/security-logic-authenticates-body-buffering-precedes-it.md`; `learnings/configuration-errors/github-actions-composite-action-main-ref-pr-isolation.md`; `learnings/build-errors/bazel-fortify-source-libmagic-glibc-conflict.md`
- Deployment (read-only): `ops-deploy` `roles/dsp-deploy/defaults/main.yml`, `templates/docker-compose-iiif.yml.j2`, `templates/iiif/conf/sipi.prod-config.lua.j2` @ `02a104ba`; dsp-api `modules/sipi/scripts/{sipi.init,authentication}.lua` @ `1eccdea43`
