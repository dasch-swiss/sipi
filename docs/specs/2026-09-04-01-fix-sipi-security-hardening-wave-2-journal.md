# Execution Journal — SIPI security hardening wave 2 (DEV-7131)

Tier-2 orchestration journal for plan `01-fix-sipi-security-hardening-wave-2-plan.md`.
Scope: sipi-repo code phases **1-10** only (priority order 1,2,3,4,5,6a,6b,6c,7,8,9,10).
Out of scope (operator/cross-repo, not touched here): Phase 0, 11, 12, 13.

Base commit: `5007032c`. Branch: `feature/dev-7131-sipi-security-hardening-wave-2-deep-analysis-after-dev-6418`.
Recover from: this journal + `git log 5007032c..HEAD --oneline` + plan checkboxes.

Environment notes for any resuming orchestrator:
- Bazel is only on PATH inside `nix develop`. All builds/tests: `GH_TOKEN=$(gh auth token) nix develop --command <cmd>` (Kakadu fetch needs GH_TOKEN).
- Local macOS ASan is broken; memory-corruption Criticals are verified green (post-fix, non-ASan) + fixture crafted + build clean. Pre-fix red confirmed on CI asan-ubsan later.
- Local build is a cold from-source compile of native deps; first build is slow.

## Chunk table

| chunk | finding(s) | phase | status | commit(s) | summary |
|---|---|---|---|---|---|
| P1-S2-01 | S2-01 | 1 | done | afb1eaa8 | TIFF planar-separate ROI dest offsets `(c*roi_h+(i-roi_y))*roi_w` + loop bound + bps-12 source offset; LZW planar-separate fixture + bottom-ROI test; formats_test green |
| P1-S2-08 | S2-08 | 1 | done | 1f11444a | TIFF scanline-size check `sll < (samples*bps+7)/8` → kMalformedInput before alloc; fixture = verbatim nightly fuzz crash reproducer (crash-2866a0e1, 145B); formats_test green |
| P1-S2-06 | S2-06 | 1 | done | daaf582f | Tiled planar-separate: read each sample plane (6th-arg sample idx) into nc*plane buffer + de-interleave once; reject non-positive TIFFTileSize; also fixed tile-count check for SEPARATE (nc tile sets) — needed for reachability; tiled RGBA fixture + pixel-exact test; formats_test green |
| P1-S2-07 | S2-07 | 1 | done | 3d17f01c | convertYCC2RGB nc<3 guard (kMalformedInput) + 16-bit chroma midpoint 0x80→32768 (pre-existing correctness bug); nc=1 rejection tests. WATCH: 16-bit midpoint may shift approval goldens for any 16-bit sYCC JP2 — verify approval suite at Phase 1 close |
| P1-S2-02-03 | S2-02, S2-03 | 1 | done | 45c8b606 | JP2 stripe_heights → std::vector sized get_num_components()/getNc(); get_signed sized to component count; .data() at all push/pull sites; 8-sample TIFF + 8-component JP2 fixtures (JP2 encoded via fixed convert); >5-component encode+decode tests; formats_test green |
| P1-S2-15 | S2-15 | 1 | done | a0f770f5 | JP2 box lengths: kMaxMetadataBytes(64MiB) cap on all 5 get_remaining_bytes sites (read + read_shape) before make_unique; check every box.read return incl. 16B UUID; get_text NULL guards. Rubber-length UUID fixture + test. NOTE: binary-COM get_text()==NULL is unreachable (kdu returns NULL only when !exists(), already gated) — guard added defensively, untested |
| P1-S2-14 | S2-14 | 1 | done | 81765f81 | JPEG Exif/XMP/IPTC guards bound total payload ≤65533 (was ≤65535, longjmp'd); IPTC APP13 length +4 (size field). 2 goldens (Cmyk/CielabTiffToJpeg) +4B re-approved (APP13 len only, pixels identical) + CHANGELOG. Oversized-Exif fixture + write test. formats_test + approval green |
| P1-S2-38a | S2-38 (metadata slice) | 1 | done | a9570f6e | base64Decode: calcDecodeLength → saturating size_t math (was `int(len*0.75)-padding`, negative→huge size_t→wrapping alloc); BIO_read bounded by buffer.size() not encoded len (heap over-write). Dropped unused <cmath>. Regression test via parse_legacy with degenerate 2-char ICC field ("A=") → catchable shttps::Error, no underflow. essentials_test 13/13 green. Round 2 |
| P1-S2-38b | S2-38 (format_handlers slice) | 1 | done | f8103649 | Two workers, one commit. validate_decode_dims on J2K+TIFF read_shape Essentials fast paths (forged packet dims rejected) + TIFF read_watermark before sll=nx*spp*bps/8 (int-overflow guard). png_set_chunk_malloc_max(16MiB)+png_set_chunk_cache_max(1000) at both PNG png_create_read_struct sites (BCR libpng has PNG_SET_USER_LIMITS_SUPPORTED — confirmed effective). jp2_colour: 6 iccBytes()→init(kdu_byte*) sites now guard icc_buf.empty()→nc-guess lambda; added catch(const SipiError&) alongside catch(kdu_exception) (iccBytes throws SipiError per icc.cpp:236-252). No valid-image change; formats_test + approval byte-identical green. Round 2 |
| P1-S2-38c | S2-38 (image_processing slice) | 1 | done | d250258e | compose.cpp subtract: maxmax==0 (identical inputs) divided by 2*maxmax → SIGFPE/UB in both 8/16-bit rescale loops; feeds `compare` verb so reachable. Guard fills lhs with mid-grey (UCHAR_MAX/2 · USHRT_MAX/2, the formula's convergent value) via ranges::fill_n, returns success. Regression test Subtract.IdenticalImagesFillMidGrey (two identical 8bps 3x3 → all 127). image_processing_test green. Round 2 — S2-38 complete (all 3 slices) |
| P2-hang | Phase 2 item (DEV-7080 reproducers) | 2 | done | 6685a5b5 | Downloaded DEV-7080 j2k reproducer (`timeout-7e09ed20…`, run 33254456688) + nightly instance (`timeout-a447eb95…`, run 33833567222) into test/_test_data/images/hang/ as .jp2 (LFS-caught by extension) + provenance README. Directory loaded by no corpus-replay target; feeds Phase 3 deadline unit test. Round 3 |
| P2-S2-31 | S2-31 | 2 | done | 0ecbf725 | Fuzz workflow recovery: counting libtiff warning handler in tiff_decode_fuzz.cc LLVMFuzzerInitialize (stops 1.6GB log flood); capped 3 tee'd logs at 50M at the file via `tee >(tail -c 50M > …)`; j2k Fuzz step `continue-on-error: matrix.target=='j2k'` until Phase 3 watchdog. tiff_decode_fuzz corpus-replay green. Round 3 |
| P2-DEV-7081 | DEV-7081 | 2 | done | 3954d313 | fuzz.yml parse_request matrix leg `asan_options: "detect_container_overflow=0"` (Rust parser ASan false positive); codec legs unchanged. YAML validated via ruby (no PyYAML locally). Round 3 |
| P2-S2-32a | S2-32 (decode coverage) | 2 | done | aecbbb1d | codec_fuzz_harness.h run_decode 2nd pass: 16-byte LE header (region x/y/w/h, size w/h, reserved rotation/reduce) stripped off front, remainder → distinct temp file → 4-arg region/size-aware read via SipiRegion(x,y,w,h)+SipiSize(PIXELS_XY,…,w,h). Pass 1 (whole-buffer plain read) unchanged so existing corpora stay valid. Templated fuzz_temp_path (fixed latent per-process static caching bug). 4/4 decode corpus-replay green. C++-only, no rust gates. Round 3 |
| P2-S2-31-thr | S2-31 (j2k throughput) | 2 | done | 6b50f8c4 | j2k DECODE leg max_len 32768→8192 in fuzz.yml + justfile _fuzz_target_table (raise exec/s; exec/s before/after deferred to PR nightly). j2k_roundtrip stays 16384. YAML valid, justfile parses. Round 3 |
| P3-design | S2-13 (design note) | 3 | done | e57131e9 | `02-jp2-decode-watchdog-design.md`: mechanism (b) seam wall-clock deadline chosen over (a) box pre-pass (covers the class, not one box shape; maintainer named the seam); records lifetime-safety contract (detached worker only writes packaged_task shared state it co-owns), leaked-thread bound (nthreads) + surface (wedged_threads gauge, restart at nthreads-1), and testability via injectable EngineContext::decode_timeout_ms. Standalone docs(specs) commit. Round 4 |
| P3-metric | S2-13 (wedged_threads plumbing) | 3 | done | 0a399f2a | `wedged_threads` gauge end-to-end: Gauge::Increment() + member (metrics.h), appended int64 field (metrics_snapshot.h, size 168→176, offset 168 assert), populate (sipi_ffi.cpp), Rust mirror + offset_of test (ffi.rs), GAUGES row "sipi.wedged_threads" + "6→7 live gauges" (metrics.rs), kBridgedToOtlp 21→22 + "8 gauges" (metrics_registry_test.cpp), new "Wedged Decode Threads" prose section in health-endpoint.md (file had no metrics table). metrics_registry_test + metrics_snapshot_test + sipi_unit_test green; BOTH rust lint gates green. Landed in the P3 fix commit. Round 4 |
| P3-watchdog | S2-13 (mechanism) | 3 | done | 0a399f2a | EngineContext::decode_timeout_ms (default 120000ms); `run_with_deadline<F>` templated helper (anon ns) in serve_image.cpp runs each decode on a joinable thread via shared_ptr<packaged_task>, join-on-ready / detach+wedged_threads.Increment()+nullopt on timeout; both read_shape (Result<SipiImgInfo>) and read (Result<void>, via local DecodeOutcome struct) wired, timeout → synthetic "read"-phase error + InternalError(500); existing bad_alloc/SipiImageError/SipiSizeError catches wrap the helper (fut.get() rethrows worker exceptions). Parameterized deadline tests over both hang fixtures (trip at ~2.0s under 2s deadline, wedged_threads +1) + good-JP2 positive control. fuzz.yml j2k continue-on-error removed (YAML valid). serve_image_test 15/15 green; sipi_ffi builds. **Phase 3 = one fix commit 0a399f2a (S2-13/DEV-7080).** Round 4 | (decode then write tif/jpx/png/jpg → reaches S2-02 JP2-encode + S2-14 JPEG-marker-write); 4 new *_roundtrip_fuzz.cc + cc_fuzz_test targets (reuse per-format seed_corpus); Phase-1 fixtures seeded into fuzz_seeds_{tiff,jpeg,j2k}; 4 fuzz.yml matrix rows (5→9 legs); fuzzing.md updated; justfile bazel-build-fuzz + _fuzz_target_table + error msgs wired for the 4 new _bin/_corpus (folded in, chunk P2-S2-32c). 8/8 fuzz corpus-replay green; YAML valid; justfile parses + target labels resolve. Full --config=fuzz build not run locally (slow); CI nightly/workflow_dispatch is the build gate. Round 3 |

| P4-S2-05 | S2-05 | 4 | done | 035b030e | Parser input bounds + derived-output clamp. parse.rs: parse_rotation rejects non-finite or outside 0.0..=360.0; parse_region rejects non-finite coords, Coords outside 0.0..=2147483647.0, Percents outside 0.0..=100.0 (reuse existing ParseError shapes via local err closures); parse_size pct branch rejects non-finite. SipiSize.cpp get_size PERCENTS case clamps derived w/h to limitdim(32000) parallel to the PIXELS constructor cap (kills ^pct:1000000 multi-billion-px output). Tests: 3 new parse.rs unit tests; e2e oversized_rotation_returns_400 (input_validation.rs); proptest oversized_numeric_segment_returns_400 over 39-45 digit rotation/region/size-pct (16 cases); 2 corpus seeds (oversized rotation + region). Verified: iiif_parser_test + corpus_regression_test + iiifparser_test green; e2e targets compile; BOTH rust lint gates green. One follow-up worker fixed a rustfmt diff in proptest. Round 5 |

| P4-S2-27 | S2-27 | 4 | done | 8438d987 | Post-commit encode-failure connection reset (Sentry SIPI-1Q). Root cause: `apply()` in serve_response.cpp discarded the body-delivery result (`(void)produce`/`(void)send_file`), so `sipi_serve_*` always returned Ok and the Rust shell never learned the body failed → clean chunked EOF = truncated 200. Fix: `apply` now returns `int` (produce/send_file code, 0 for EmptyBody); sipi_ffi.cpp both entries map non-zero → InternalError; serve_streaming (sink.rs) gains an `else if code != 0` branch after `!head_sent` that pushes `Err(BodyAbort)` so hyper resets the connection; pre-first-write failures still render a clean 500 via the existing `!head_sent` path (comment corrected to state both branches are live). Two deterministic sink unit tests (post_commit_failure_emits_body_abort / clean_success_emits_no_body_abort) drive serve_streaming via the raw write callback — the red/green gate. Verified: sipi_unit_test + serve_image_test + serve_response_test 3/3 green; BOTH rust lint gates green. e2e fixture (plan 389) NOT crafted — see Deferrals. Round 5 |

| P4-S2-37 | S2-37 | 4 | done | c4c50548 | Range-header length cap. parse_range (serve_response.cpp) rejects any Range header >128 bytes (`std::strlen` guard + `<cstring>`) as the first statement, before the `std::regex` — stops a pathologically long header from driving libc++'s recursive matcher into a stack overflow; caller maps false→400. Unit test BuildFileResponse.OversizedRangeHeaderIsBadRequest (130 leading-zero bytes → valid 206 on main, 400 after) in serve_response_test.cpp. serve_response_test green. C++-only, no rust gates. Round 5 |

| P5-S2-04 | S2-04 | 5 | done | b61f5ddc | Restrict-as-scale. serve_image.cpp:595 box-comparison (`*size > *restricted_size`) replaced with sampling-factor logic: derive `f = min(rest_w/img_w, rest_h/img_h)` from resolved restricted_size, `region->crop_coords` the requested region (idempotent — pure recompute, sets canonical_ok; build_canonical_url/read re-derive independently, no stale leak — worker confirmed), `size->get_size` on the region extent, replace `size = SipiSize(f*100)` when output exceeds `rw*f × rh*f`. Invariant + 2 accepted-residual comments (super-res attack accepted; canonical Link native-coord disclosure → recommendation to omit Link for non-Allow, deferred to a later item). docs/src/lua/index.md + UBIQUITOUS_LANGUAGE.md restrict rows refined to "effective sampling factor of any region". e2e `permission_code_1_restricts_the_size_of_a_region_request` (256×256 region on 512×512 perm1file, cap !128,128→f=0.25, decodes to ≤64×64 via `image` crate). serve_image_test + dsp_api_closure e2e green; BOTH lint gates green. Round 6. NOTE: plan 404 (consolidated e2e) partially satisfied (region-scaling part); 403-cases + info.json restricted-dims land with S2-09/S2-16. |

| P5-S2-09 | S2-09 | 5 | done | 76c29aec | Non-restricting restrict → 403. Added `SipiStatus::Forbidden=403` (serve_response.h). serve_image.cpp: seam check keyed off `req.restricted_size != nullptr` (NOT `undefined()` — worker found size=max parses to the same undefined shape as no restriction), before the S2-04 scale block: `restricted_size != null && rest_w>=img_w && rest_h>=img_h && watermark.empty()` → Forbidden (catches size=max, pct:100, ^pct:200). routes.rs: bare restrict (no size AND no watermark → seam sees null/null = allow) refused with 403 on the IIIF edge before the seam via `return complete(outcome_tx, sink::error_response(StatusCode::FORBIDDEN))`; `file_access` Restrict arm → 403 (raw /file can't clamp/watermark). ffi.rs doc comment lists 403. Seam int propagates via `static_cast<int>` → `map_status` (from_u16), no sink change. e2e: 2 test-only prefixes in sipi.init-knora.lua (restrict size=max, bare restrict) + 2 #[test]s in server.rs (extended — BUILD.bazel needs a manual sipi_e2e_test entry per new file, so no new file). server.rs full target 43/43 green; serve_image_test green; BOTH lint gates green. Round 6. RESIDUAL: /file restrict e2e not added (code landed). plan 404 now covers restrict region-scaling (S2-04) + 403 cases (S2-09); info.json restricted-dims + knora deny 404 still pending S2-16. |

| P5-S2-16 | S2-16 | 5 | done | 58623817 | info.json/knora.json restricted-dims info-leak. Added `iiifparser::clamp_dims_to_size(size,w,h)->Result<(u32,u32)>` (parse.rs, re-exported lib.rs) with unit tests — the security-relevant clamp lives in iiifparser so the shell never reimplements it. DEVIATION from plan 412's literal `pub fn parse_size->SizeKind`: omitted (name-collides with the private `parse_size->SizeParts`; SizeKind alone cannot clamp so it would be dead API — the clamp helper is what honors the checkbox's stated goal "server never re-implements a security-relevant clamp"). routes.rs: KnoraJson added to the top-level auth gate → 404 for non-Allow/Restrict (hides existence on the DSP-internal metadata surface; RECONCILES plan tension — checkbox 404 said 404, 411 said "like image route"/401; ruled 404). serve_info_json + serve_knora_json clamp dims + zero tile grid for every non-Allow permission (restricted W/H, no native pyramid); serve_knora_json now takes `access: &Access` + `hook_configured`. json_response `vary: bool`→`&[&str]` + new `private: bool` emitting `Cache-Control: private, no-store`; both docs get `Vary: Cookie, Authorization` + private,no-store when `state.has_preflight`. e2e: 3 tests in security.rs (restricted info.json clamped W + no tiles + Vary/Cache-Control; knora deny→404; knora allow→200). iiif_parser_test + sipi_unit_test + security e2e green; BOTH lint gates green. Round 6. |

| P5-S2-43 | S2-43 | 5 | done | 2f9d390e | Preflight cache key hardening (no behavior change — `make_key` already keys only prefix/identifier/Cookie/Authorization and `build_request_data` already sets `uri: uri.path()` query-free). routes.rs: `..Default::default()` in build_request_data → explicit field literal (get/post/request params, uploads, content, content_type, jwt_secret empty; docroot None) so a new RequestData field must be classified before it compiles. Unit test `preflight_request_data_is_query_and_body_free` (path-only uri, empty params/content, populated method/host/headers/cookies) in routes.rs `mod tests`. Docs: preflight_cache.rs module doc (query-free + cache-busting-amplifier rationale), routes::iiif_access inline comment, UBIQUITOUS_LANGUAGE.md "Preflight cache" row. sipi_unit_test green; BOTH lint gates green. Round 6. **Phase 5 code-complete: S2-04 (b61f5ddc), S2-09 (76c29aec), S2-16 (58623817), S2-43 (2f9d390e).** |

| P6a-S2-17 | S2-17 | 6a | done | a1cd4767 | X-Forwarded-Host allowlist. New `SIPI_PUBLIC_HOSTS` knob (`config::public_hosts_from_env`, shared `parse_csv_list` with the renamed CORS parser + parse unit test). `AppState.public_hosts: Vec<String>` threaded through `AppState::load` (+ lib.rs call site). `forwarded()` gained `public_hosts: &[String]`: empty = verbatim passthrough (byte-identical to pre-knob); non-empty = allowlisted host passes, else substitute `public_hosts[0]`. All 6 call sites updated (build_request_data threaded via `&state.public_hosts` from both preflight callers 575/654; serve_image, serve_info_json, serve_knora_json, serve_lua_script, redirect). Cache key KEEPS host (now bounded by allowlist). 2 existing forwarded() tests updated to `&[]`, 2 new unit tests + 1 e2e (`public_hosts_allowlist_substitutes_hostile_forwarded_host` in security.rs — asserts redirect Location + info.json `id`). Docs: running.md env table + health-endpoint.md `/health` keeps `version` decision. sipi_unit_test + security e2e (12) green; BOTH lint gates green. Round 6. Notes: info.json field is `id` (IIIF 3.0) not `@id`; sipi.md untouched (SIPI_ALLOWED_ORIGINS isn't there either — env-only knobs). |

| P6a-S2-40 | S2-40 | 6a | done | 1e08282e | Uniform 404 for image-path traversal + docroot dotfile skip. routes.rs: image-path R1 string-level traversal (`contains_traversal` on identifier/prefix, ~281) changed BAD_REQUEST→NOT_FOUND (indistinguishable from a miss); NUL check (~271) untouched (distinct malformed guard, stays 400); post-realpath `Resolved::Traversal` 400 in engine dispatch left as-is (separate defense point, out of scope — worker judgment). serve_docroot: after suffix normalization, before infile construction, any path component `starts_with('.')` → 404 (hidden files never served; `.lua`/`.elua` unaffected). e2e: 3 input_validation.rs assertions renamed/updated 400→404; new `dotfile_is_404_even_when_present` in docroot_scripts.rs (real `.env` fixture via DocrootScript helper, asserts 404 + no content leak). sipi_unit_test + docroot_scripts + input_validation + security e2e green; BOTH lint gates green. Round 6. **Phase 6a code-complete: S2-17 (a1cd4767), S2-40 (1e08282e).** |

| P6b-S2-18 | S2-18 | 6b | done | f51c2fe7 | Request-handler wall-clock timeout. MODULE.bazel + BUILD.bazel add `tower-http` 0.6 features=["timeout"] (tower stays ["util"]; no checked-in crate lock, repin automatic — MODULE.bazel.lock updated). config.rs `request_timeout_from_env()` + `DEFAULT_REQUEST_TIMEOUT_SECS=60` reading `SIPI_REQUEST_TIMEOUT` (seconds; unset/invalid/0→60) + 4 unit tests. lib.rs `app()` wraps router in `TimeoutLayer::with_status_code(408, …)` (used `with_status_code` not `new` — `new` deprecated since tower-http 0.6.7, would trip `-Dwarnings`; same 408 semantics) with the S2-18 comment (bounds handler future to response head, not the spawn_blocking decode / permit occupancy); `serve()` comment records header-timeout/conn-cap ruling = (b) Traefik edge, option (a) hyper-util a Linear follow-up. Docs: running.md env table + admission-control.md pool knobs (sipi.md skipped — env-only, matching CORS/host knobs). e2e `handler_exceeding_request_timeout_answers_408` (resource_limits.rs): reuses `/hardening/loop` Lua route with `SIPI_LUA_TIMEOUT_MS=10000` vs `SIPI_REQUEST_TIMEOUT=1` so axum's timeout fires before the Lua deadline — deterministic across 3 attempts (worker verified with diff stashed). Verified: BOTH lint gates green + sipi_unit_test green (orchestrator re-ran). Round 7. NOTE: two pre-existing flaky tests in resource_limits (sustained_load_memory_growth, transform_pipeline_memory) fail locally regardless of this change (already `--flaky_test_attempts` in .bazelrc). |

| P6b-S2-19 | S2-19 | 6b | done | 08baf5bb | Lua-route body bounds before admission. config.rs: `DEFAULT_MAX_POST_SIZE=256MiB`, `DEFAULT_BODY_READ_TIMEOUT_SECS=10` + `body_read_timeout_from_env()` (mirrors request_timeout, 4 unit tests). routes.rs: `AppState::load` max_post_size now `.filter(|&n|n>0).unwrap_or(DEFAULT_MAX_POST_SIZE)` (both ready+not-ready branches finite); three `==0` unlimited branches deleted (lua_route_method_router, to_bytes path, docroot_method_router → always `DefaultBodyLimit::max`); `RequestBodyTimeoutLayer::new(body_read_timeout_from_env())` on BOTH Lua-route + docroot routers (size+time bound before `admission.acquire`, which stays AFTER the read); multipart parts capped at 64 → 400. Docs: running.md env table + admission-control.md. e2e (resource_limits.rs, existing target): `multipart_part_count_over_limit_answers_400` (65 parts→400) + `trickling_body_is_cut_off_by_body_read_timeout` (raw TcpStream, SIPI_BODY_READ_TIMEOUT=1, 8s socket failsafe, completes ~2.7s — NO hang). Verified by orchestrator: BOTH lint gates + sipi_unit_test + both new e2e green. Round 8. NOTE per worker: body-read timeout maps to the existing 413/generic body-error path, not a distinct 408 (not remapped — out of scope). Checkbox 437 split: 65-part half done here, SipiImage.new-bomb half is S2-20. |

| P6b-S2-20 | S2-20 | 6b | done | e337543a | `sipi_image_new` (image_handle.cpp) full-lane memory-budget charge, mirroring serve_image.cpp's estimate_peak_memory/try_acquire block. On `read_shape` success: `compute_decode_dims` + `estimate_peak_memory(…, angle=0, needs_icc=false)`; when `eng.memory_budget != nullptr && estimated >= large_decode_threshold_bytes` → `try_acquire`; `!result.allowed` → `emit_str("image decode exceeds the memory budget")` + return nullptr (covers advanced permanently-unservable + transient); `result.over_budget` (basic) → log_warn + admit; `std::optional<Sipi::MemoryBudgetGuard> budget_guard.emplace(*mb, estimated, result.allowed)` (3-arg, no metrics callback) held across the decode, RAII-released on return. read_shape failure → guard empty, falls through to existing read (proper error there). Includes added: iiifparser/SipiDecodeDims.h, throttling/SipiMemoryBudget.h + SipiPeakMemory.h, ffi/engine_context.h, <optional>. No BUILD dep change (sipi_ffi already depends on //src/image transitively). Tests (seam_probe_test.cpp, existing target): tiny advanced-budget → refused; null budget → succeeds. Verified by orchestrator: seam_probe_test green. Round 8. |

| P6b-S2-21 | S2-21 | 6b | done | 5cb1a613 | Docroot Tile permit + libmagic load-once. routes.rs serve_docroot: static-file `spawn_blocking` (~1546) now acquires `AdmissionKind::Tile` (Shed/TimedOut → busy_response), permit moved into the closure (`let _permit = permit;`) — mirrors the Lua-path Full permit; `.lua`/`.elua` branch untouched (already gets Full downstream). Parsing.cpp getFileMimetype: `thread_local magic_t handle = nullptr`, lazily loaded once per worker thread (magic_open + magic_load_buffers from embedded magic_mgc), reused, never closed per-call; failed load not cached (retries). Eliminates per-request open/load/close. Verified by orchestrator: clippy + util_test + docroot_scripts e2e green (worker also ran rustfmt + input_validation e2e + `//src/cli/rust:sipi` build). Round 8. Note: actual target names are `//src/util:util_test` and `//src/cli/rust:sipi` (not parsing_test/sipi_server). |

| P6b-S2-33 | S2-33 | 6b | done | 0c7f2114 | **BREAKING** admission_mode default flip → advanced. SipiConf.h:65 `admission_mode_str{"advanced"}` (effective default: clap arg is None→no override, `parse_admission_mode(conf.getAdmissionMode()).value_or(BASIC)` resolves the unset default from SipiConf; value_or(BASIC) unparseable-fallback UNCHANGED, out of scope). Shipped-defaults regressions: C++ `SeamProbe.ShippedAdmissionModeDefaultIsAdvanced` + Rust `config::tests::default_max_post_size_is_finite`. Docs rewritten (advanced=default, DEFAULT-vs-unrecognized distinction, observe-via-explicit-basic workflow): admission-control.md, memory-budget.md, running.md. ADR-0022 amended (dated amendment note + Mode/Consequences corrected; fairness substance left for the 6c amendment). `feat(throttling)!:` + BREAKING CHANGE footer. Verified by orchestrator: seam_probe_test + sipi_unit_test + clippy green; **upload e2e green under the new advanced default (no shed on normal decodes — flip is safe)**. Round 8. **Phase 6b CODE-COMPLETE: S2-18 (f51c2fe7, r7), S2-19 (08baf5bb), S2-20 (e337543a), S2-21 (5cb1a613), S2-33 (0c7f2114).** |

| P6c-S2-44 | S2-44 | 6c | done | 77b29cee | Dropped `client_ip` from the offset-locked `SipiServeRequest` seam (never read engine-side; spoofable XFF). ffi.rs field + offset_of! block re-baselined (size 168→160: params 32→24, restricted_size 104→96, watermark 112→104, forwarded_proto 120→112, forwarded_host 128→120, request_uri 136→128, is_head 144→136, report_error 152→144, report_ctx 160→152); sipi_ffi.h field + static_assert block same; routes.rs serve_image dropped c_client_ip CString + field; serve_image_test.cpp:74 line removed. KEPT Lua-facing RequestData.client_ip / server.client_ip + the client_ip() helper + its test (routes.rs). Verified by orchestrator: sipi_unit_test + serve_image_test green (the ffi.rs offset_of! test is the gate) + BOTH lint gates green. Round 9. `refactor(ffi)` (internal seam removal, no end-user impact). |

| P6c-S2-24 | S2-24 | 6c | done | 072c46aa | Cookie hardening. bindings/mod.rs ResponseCookie: `http_only` default false→true, new `same_site: String` field default "Lax" rendered `; SameSite=Lax`, struct doc updated. bindings/server.rs send_cookie: `http_only` option now honors both true/false (default flipped, so override both ways; secure stays on-only, comment scoped); new `same_site` string option (empty clears). bindings_tests.rs:184 pinned expectation → `sid=s3cr3t; Path=/; Secure; HttpOnly; SameSite=Lax`; send_cookie_validates_options unaffected (asserts contains Secure only). sink.rs apply_headers: `map.append` for `axum::http::header::SET_COOKIE`, `map.insert` for all else (comment corrected — last-write-wins holds except Set-Cookie). docs/src/lua/index.md sendCookie options + secure-default note. e2e: new hardening_cookies.lua (two cookies, no opts) + `/hardening/cookies` route in sipi.lua-hardening-config.lua + test `cookies_default_secure_and_all_survive` in lua_hardening.rs (get_all(SET_COOKIE)==2, each HttpOnly+SameSite=Lax; red on main). Verified by orchestrator: bindings_test + lua_hardening e2e PASSED + BOTH lint gates green. Round 9. `fix(scripting)`. |

| P6c-S2-34 | S2-34 | 6c | done | 343d9c5e | Per-client fairness ruling = (b) Traefik `inFlightReq` middleware keyed on client IP, operator-side, no SIPI code. Recorded as an ADDITIVE amendment to docs/adr/0022 (DEV-7140, 2026-09-05) placed right after the existing DEV-7139 default-flip amendment (left byte-for-byte untouched) + one Consequences bullet. Explains admission stays cost-based/per-partition (no XFF reintroduced) and ties to the S2-44 client_ip seam removal. Traefik diff carried ops-deploy, not this repo. Docs-only, no test/lint. Round 9. `docs(adr)`. **Phase 6c CODE-COMPLETE: S2-44 (77b29cee), S2-24 (072c46aa), S2-34 (343d9c5e).** |

| P7-S2-11 | S2-11 | 7 | done | e28b6808 | Fail-closed jwt_secret startup check + drop shipped default. lib.rs: `const SHIPPED_JWT_SECRET`, pure `jwtkey_is_unsafe(key)` (empty / <32 bytes / ==shipped) + `#[cfg(test)]` unit tests, wired into server_main after probe_hooks in the `configured_routes.is_some()` arm (fail-closed tracing::error + flush_telemetry + ExitCode::FAILURE). config/sipi.config.lua: removed literal jwt_secret + admin block (comment → use SIPI_JWTKEY). tls_auth.rs: `--jwtkey` help corrected ("exactly 42 characters"→≥32 required, default rejected). WIDE blast radius: replaced the public literal `UP 4888…` with dev secret `dev-only-insecure-jwt-secret-change-me` (38B) across config/{loadtest,localdev,test}, test/_test_data/config/{lua-lane,e2e-test,lua-hardening,cache-test,toml-parity .lua+.toml}, test/_test_data/dsp-api/closure config, inline configs in e2e {cache,config,upload,lua_hardening,dsp_api_closure,preflight_cache,tracing}.rs, bindings_tests.rs. security.rs `config_empty_jwt_secret` rewritten → `config_empty_jwt_secret_refuses_startup` (the S2-11 red→green e2e, reuses lua_hardening bad-init refusal pattern). docs running.md (SIPI_JWTKEY required) + sipi.md stale default line. Worker found 3 extra files (toml-parity .lua/.toml had no secret; cache-test still on literal) — fixed. No hardcoded eyJ JWTs. Verified by orchestrator: sipi_unit_test + e2e security/lua_hardening/config PASSED (worker ran 9 e2e + toml_config green) + BOTH lint gates green. Round 9. `fix(server)`. NOTE: plan 466 (multi-finding red-test box) covers the S2-11 refusal e2e (done here) + upload-401 (S2-23) + print→tracing unit (S2-39); tick 466 once those land. |

| P7-S2-39 | S2-39 (3 of 4) | 7 | partial | 39d05523 | Three Lua-binding hygiene fixes landed (plan 473/474/475 ticked): (1) runtime.rs base_vm installs a `print` global joining varargs via tostring → one `tracing::info!(target:"sipi::lua")` line (was stdout) + runtime_tests.rs test (callability/no-error; full tracing-capture not feasible w/o a tracing-subscriber/tracing-test dev-dep, which is prohibited — noted); (2) fs_mkdir masks mode `0o777 & !0o002`; (3) lua_to_json threads a depth counter capped at 64, existing `(false,msg)` shape on cyclic/deep. Worker also renamed a pre-existing `mlua::String`→`mlua::LuaString` deprecation in its new code (needed for clippy -Dwarnings). Verified by orchestrator: scripting_test + runtime_test + bindings_test PASSED + BOTH lint gates green. Round 9. `fix(scripting)` "Part of S2-39". **Sub-fix 4 (plan 476, constant-time Basic compare) NOT done → BLOCKER, see below.** |

| P7-S2-46 | S2-46 | 7 | done | 11a433cd | Docroot-overlap RCE guard. lib.rs: pure `docroot_overlap`/`paths_overlap`/`normalize_path` helpers (lexical normalize, no existence required; equal or either-direction nesting = overlap) + fail-closed startup check between config resolution and Lua-env build, refusing when configured `docroot` overlaps imgroot/tmpdir/scriptdir/cache_dir (names the offending root; only when docroot Some+non-empty). Uses same default fallbacks as lua_config_values (imgroot `.`, tmpdir `/tmp`, scriptdir `./scripts`, cache_dir `./cache`) so it matches runtime-resolved paths. Unit tests on the helper (equal/nested-both-ways/disjoint). e2e `docroot_overlapping_imgroot_refuses_startup` (docroot=imgroot=./images, valid dev jwt_secret so overlap is the sole refusal reason) asserts non-zero exit — satisfies plan-590 acceptance. docs/src/lua/index.md sandbox bullet (bindings not path-confined; overlap invariant; deliberate no-confine() decision). Verified by orchestrator: sipi_unit_test + lua_hardening e2e PASSED + BOTH lint gates green. Round 9. `fix(server)`. |

| P7-S2-22 | S2-22 | 7 | done | 33756eb0 | Deleted scripts/token.lua (reflected XSS + token exfil via unescaped messageId/origin into a `<script>`/postMessage relay) per the pre-made DELETE ruling. Removed the `/api/token`→token.lua route entry from config/sipi.config.lua (routes table left comma/brace-valid) and the doc example in docs/src/guide/sipi.md. grep for `token.lua`/`api/token` across config/scripts/src/test/docs/src = empty (docs/archive historical mentions left). No Rust/C++ changed → no lint gates. Round 9. `fix(scripting)`. |

| P7-S2-23 | S2-23 | 7 | done | 1d91c690 | Upload-example hardening + fork unification. `scripts/upload.lua`: `authorize_api("https://sipi.example.org","Sipi","uploader")` gate at top (no/invalid Bearer → 401 via server.requireAuth/decode_jwt); stored basename now `uuid62 .. '.jp2'` (origname still passed to `write{origname=...}` so knora.json keeps originalFilename); dead non-image `else` branch (referenced undefined `index` global — genuinely broken) replaced with explicit `send_error(415)`. Fork `test/_test_data/scripts/upload.lua` DELETED. e2e now loads the ONE repo script: kept `scriptdir='./scripts'` (other routed test scripts live only there) and instead pointed the `/api/upload` route's `script` field at `../../../scripts/upload.lua` in `sipi.e2e-test-config.lua` + the inline small-post config in upload.rs. `authorize_api` copied into `test/_test_data/config/sipi.init-knora.lua` (init-knora lacked it). All 12 upload.rs tests now send a valid Bearer (create_jwt, secret dev-only-insecure…, claims iss/aud(scalar)/user/exp); new `upload_without_token_returns_401`. Storage kept under `imgroot/unit/` per orchestrator ruling (reference has no prod consumer — uploads go via dsp-ingest — and this keeps prefix_as_path=true + shared fixtures resolving). Verified by orchestrator: `//test/e2e:upload` 13/13 PASSED (bounded --test_timeout=120) + BOTH lint gates green. Round 10. `fix(scripting)`. RULING-NOTE: reference now writes to a `/unit/` subdir (test-ism baked into the example) and non-image uploads return 415 (documented endpoint behavior change) — both accepted; example script, no prod path. Plan 466 (multi-finding red box) now fully satisfied → ticked. |

| P7-S2-42 | S2-42 | 7 | done | abfef3b0 | **BREAKING** removal of the dead adminuser/adminpasswd surface end to end. Offset-locked `SipiServerConfig` shrank 240→224 (two 8-byte pointers gone); EVERY offset after jwtkey re-baselined -16 in lock-step on BOTH sides (config.rs offset_of! block + sipi_ffi.h static_asserts). Removed: clap flags `--adminuser`/`--adminpasswd` + `SIPI_ADMINUSER`/`SIPI_ADMINPASSWD` (tls_auth.rs) + server/mod.rs destructure/assign; ServerOverrides fields + Debug + from_config_file + merge + C-ABI struct/destructure/intern (config.rs); config_file.rs TOML fields + build + test; entry.rs LuaConfigValues admin_user/admin_password + `admin` section parse; config_parse_tests.rs asserts; stale bindings comments; SipiConf.h adminuser+password members + getAdminUser/setAdminUser (worker found `password`/`setPasswort`/`getPassword` had ZERO callers outside the admin wiring → removed the whole member per the "exclusively admin surface" rule); init.cpp:133-134 wiring; dead `authorize_page(...,config.adminuser,config.password)` gate in server/{cache,do-upload,upload}.elua; re-baselined the CLI `--help` e2e snapshot (cli__sipi-server-help.snap) which encoded the removed flags. KEPT bindings_tests.rs:143 (`config.adminuser == nil` — still true/desirable). `refactor(cli)!:` + BREAKING footer. Verified by orchestrator: `bazel build //src/cli:sipi //src/cli/rust:sipi //src/server/rust:sipi_unit_test` (both static_assert seams compile) + sipi_unit_test PASSED (offset gate) + BOTH lint gates green; worker also ran scripting config_parse/bindings/entry tests green. Round 10. RESIDUAL (cosmetic, accepted): cache.elua still has `var token = '<lua>server.print(token)</lua>'` where `token` is now a nil global — inert print in a `/server` example page slated for operator-side removal (Phase 11); not worth a worker cycle. **Phase 7 CODE-COMPLETE except the S2-39/plan-476 constant-time-compare blocker (awaiting maintainer decision — SKIP per this round's blocker_resolutions).** |

| P8-S2-25-26 | S2-25, S2-26 | 8 | done | 137e93af | Cache-index integrity overhaul (two intertwined findings = one atomic on-disk-format change; grouped by surface area per Ivan's practice). C++-only. `FileCacheRecord`/`CacheRecord` gained `source_size` (source file size at add-time, DISTINCT from `fsize`=cache-file size); `check()` now misses on mtime-newer OR source-size-mismatch (same-size+same-mtime still a hit — documented residual, no body digest by design). Cache key is now `SHA256(canonical_url)` hex (64 chars) in-memory AND on-disk (via `@openssl//:crypto`, added to src/cache/BUILD.bazel), computed in check()/add()/remove(); loaded records reuse the stored digest — kills the 255-byte-truncation collision → content-substitution primitive. Fixed `CacheIndexHeader` (magic+version+sizeof(FileCacheRecord)) validated on load: mismatch/short/bad-record (unterminated field, empty canonical, cachepath containing `/`) → discard index (or drop record) + log once; `fsize` recomputed from `lstat()` (rejects symlinks) not trusted. `serve_image.cpp::full_file_body()` now `open()`s O_NOFOLLOW + fstat/S_ISREG. ADR-0025 (new) records header layout + version-mismatch-empty + same-size residual, linked from SipiCache.h banner. 6 new tests. Verified by orchestrator: `//src/cache:cache_test` PASSED + `//src/cli:sipi` builds + serve_image/serve_response tests green (BUILD.bazel dep change covered by the passing build). Round 10. `fix(cache)`. REVIEW-FLAG (accepted, orchestrator ruling): O_NOFOLLOW applies to `full_file_body` which serves BOTH the cache-hit path AND direct source passthrough — the finding described only the cache-dir symlink vector, so this also fail-closes on symlinked SOURCE images under imgroot. Accepted because the plan named the shared `full_file_body` fn and it "only tightens" (symlinked source images are anomalous for a repository server); flag for maintainer if a deployment legitimately symlinks imgroot entries. |

| P8-S2-45 | S2-45 | 8 | done | 922a8b83 | `cache_used_bytes` underflow guard. C++-only. New `subtractClamped(atomic<ull>&, ull)` helper (anon ns, SipiCache.cpp) floors every decrement at 0 + `log_warn` once on would-underflow; applied at all THREE `cache_used_bytes -=` sites (purge() eviction, add() duplicate-canonical replace, remove()). Regression test drives a real double-subtraction through the public API (add() duplicate-replace racing purge()'s LRU rediscovery of a not-yet-erased stale entry) and asserts getCacheUsedBytes() lands at the true small value, not wrapped. Verified by orchestrator: `//src/cache:cache_test` PASSED (23/23). Round 10. `fix(cache)`. **Phase 8 CODE-COMPLETE: S2-25+S2-26 (137e93af), S2-45 (922a8b83).** |

| P10-S2-28 | S2-28 | 10 | done | 9af69ea8 | Sentry event PII/path redaction. Extracted pure `build_image_error_event(ImageErrorEventData)` from the `extern "C"` `report_image_error` (FFI pointer parsing stays in the extern fn; event-shape logic now plain Rust + unit-testable). `input_file` → basename via `Path::file_name()` (falls back to original if None) before insertion into the `Image` context. Deleted the `tags.insert("sipi.request_uri", …)` line — request_uri now lands ONLY in the non-indexed `Image` context, not an indexed/searchable tag. New `image_error_event_pii` test pins both. ADR-0018 gets a dated amendment: prod DSN = Sentry SaaS (US), decision (b) = disable minidump upload in prod until self-hosted Sentry (keep panic/handled-error reporting); actual env/DSN toggle is a Phase-11 operator item (plan 522, left unticked — out of scope here). Verified by orchestrator: sipi_unit_test PASSED + BOTH lint gates green. Round 10. `fix(ffi)`. **Phase 10 CODE-COMPLETE (plan 522 is the operator/Phase-11 follow-through, not this repo's code).** |

| P7-S2-39-4 | S2-39 (4 of 4) | 7 | done | 13eddc54 | Constant-time credential compare (maintainer decision (a)). New `server.secure_equals(a,b)` host binding in bindings/server.rs (constant-time XOR-accumulate over max-length byte slices, final `diff==0 && len==len`; no crate dep, no `subtle`; registered next to the uuid bindings). config/sipi.init.lua authorize_page BOTH branches (openssl + legacy no-openssl) now evaluate `ok_user`/`ok_pass` locals via secure_equals before combining (both compares always run — no short-circuit of the compare itself). Unit test `secure_equals_compares_constant_time` (equal/differing-content/differing-length/empty) in bindings_tests.rs; docs/src/lua/index.md documents the binding. JWT-claim compares (jwt.iss/aud/user) left as-is (signature-verified non-secrets, out of S2-39 scope). Verified by orchestrator: bindings_test + scripting_test PASSED + BOTH lint gates green. Round 11. `feat(scripting)`. **Phase 7 now FULLY code-complete (plan-476 blocker RESOLVED).** SIDE-NOTE: the legacy no-openssl branch passes a possibly-nil `auth.username` to secure_equals (LuaString typing would raise instead of the old silent 401) — inert in practice (`server.has_openssl` is hardcoded `true` in server.rs:201, so that branch is unreachable); not worth a follow-up. |

| P9-S2-41 | S2-41 | 9 | done | 19cc2805 | CI supply-chain hardening (cheap, no native rebuild). SHA-pinned 13 distinct third-party actions across all 6 workflows (version kept as trailing comment; annotated tags dereferenced to commit SHA via `gh api`); local `./.github/actions/*` composites left unpinned (in-repo). ci.yml self-ref `setup-python@main` → local `./.github/actions/setup-python`. claude.yml: dropped `contents: write` from `claude-general` (claude-code-review never had it) + inline rationale comment + fixed a stale job-header comment. New repo-root SECURITY.md (security@dasch.swiss, latest-release support, few-business-days SLA). Verified by orchestrator: all 6 workflows parse (ruby YAML); no third-party `@main`/`@master` remains. Round 11. `chore(ci)`. Ticked plan 510-513. |

| P9-S2-30 | S2-30 | 9 | done | 0a1e6d09 | Dependency-advisory pipeline (cheap-ish; MODULE crate-metadata change only, no native rebuild). `crate.from_specs(cargo_lockfile="//:Cargo.Bazel.lock")` + repin materialized a 4676-line Cargo-format lock (MODULE.bazel.lock also updated by repin). New `just audit` (cargo-audit + osv-scanner). New ci.yml `dependency-audit` job (SHA-pinned actions, checksum-verified osv-scanner binary). publish.yml gained a Critical-only Docker Scout `cves` gate (`exit-code:true`, `only-severities:critical`; image-layers-only limitation documented in a comment). dependabot.yml comment documents the prod-crate + http_archive coverage gap. ci.md gained a "Quarterly native-pin review" checklist with upstream release URLs. Verified by orchestrator: non-repin `bazel build //src/server/rust:sipi_unit_test` green (checked-in lock accepted, no drift), all workflow + dependabot YAML parse, `just audit` present. Round 11. `chore(ci)`. Ticked plan 507-509. SIDE-NOTE: cargo-audit/osv-scanner NOT in the nix dev shell → `just audit` is CI-only (config verified by parse, not executed locally); neither tool has a native severity-threshold flag so both fail on ANY advisory (conservative superset of Critical/High) — Docker Scout does honor true severity filtering. |

| P9-libtiff | S2-12 | 9 | done | a8969bf6 | libtiff 4.7.1 → 4.7.2 (native http_archive, build_file is `bazel/tiff.BUILD.bazel` NOT libtiff.BUILD.bazel as the plan text says). sha256 dd030ae5d6483033bd88d840fc97e36adc3022b844d9d896a3b5a50f2d660d6f, libsdl-org tag v4.7.2 verified to exist. Full 4.7.1-vs-4.7.2 libtiff/ dir diff: NO source add/remove/rename; only version-metadata defines updated (LIBTIFF_VERSION/RELEASE_DATE 20260627/MICRO 2) + cosmetic cmake-template defines. `just bazel-build` + formats_test green. 5 TIFF-output goldens (JpegToTiffDownscaled, CmykTiffDownscaled, CielabTiffDownscaled, JpegRotatedDownscaledTiff, J2kRegionToTiff) re-emitted (same byte-count); `sipi compare` = "Files identical!" (max/avg |Δ|=0, pixel-perfect — libtiff pixel path unchanged, only compressed-stream emission) for all 5; regenerated via direct approval binary + renamed .received→.approved (LFS confirmed) + CHANGELOG row (2026-09-05, sign-off PENDING PR review). Two worker cycles (bump + goldens). Verified by orchestrator: approval + formats_test green. Round 11. `build(deps)`. Ticked plan 499. NOTE: MODULE.bazel.lock not repinned (build used cached lock; consistent — expect a repin diff on next CI run). |

| P9-curl | S2-29 | 9 | done | f5ac3c18 | curl 8.12.0.bcr.1 → 8.21.0.bcr.1 (closes DEV-6564). CONFLICT resolved informationally (worker first returned blocked): curl 8.21.x's BCR module declares `openssl@4.0.1.bcr.0` transitively, which MVS would force our openssl 3.5.5→4.x (plan wants 3.5.x; openssl 3→4 removed-API risk for @openssl//:crypto = src/util digests + S2-26 src/cache SHA-256). Held openssl at 3.5.x via `single_version_override(openssl, 3.5.5.bcr.4)` (confirmed newest published 3.5.x, not yanked). curl 8.21.x compiles + links cleanly against openssl 3.5.x. `bazel mod graph` shows openssl@3.5.5.bcr.4 only (no 4.x). Verified: just bazel-build green (228 actions); util_test + cache_test (crypto consumers) PASS against openssl 3.5.x; approval + formats_test byte-identical; `.bazelrc --@curl//:ssl_lib=openssl` unchanged/valid. MODULE.bazel.lock repinned + committed. Round 11. `build(deps)`. Ticked plan 500. **RECONCILES plan 503's openssl half**: openssl is now explicitly held at newest 3.5.x (3.5.5.bcr.4) by this commit's override — the later libpng+openssl commit (plan 503) is now libpng-only. |

| P9-lcms2 | freshness | 9 | done | c2e89893 | lcms2 2.16 → 2.19.1 (native http_archive, tag `lcms2.19.1`, sha256 bfc54f7bab59fbc921012014a8032e4cba4abd46db47d46b76416a8c0b2815c8). bazel/lcms2.BUILD.bazel unchanged (identical src/include layout, glob srcs; visibility narrowing preserved). just bazel-build + formats_test + tiff_codecs_test + icc_normalize_test + icc_parse_test + essentials_test + exif_rational_test (6/6) green. ONE golden shifted: TiffRegionRoundTrip (embedded ICC re-serialized, shrank 4516→4456 bytes). sipi compare = "Files identical!" (pixel-perfect); cmp -l + TIFF IFD parse confirm all 3374 differing bytes fall inside the ICC Profile tag byte-count field + profile data (offset 65694→EOF), never scanline/strip data. Regenerated via direct approval binary + CHANGELOG row (2026-09-05, sign-off PENDING PR review). Verified by orchestrator: approval green. Round 11. `build(deps)`. Ticked plan 501. NOTE: prod output now embeds the more-compact lcms2-2.19 ICC serialization (valid equivalent profile, same decoded colours) — accepted benign drift the plan anticipates. |

| P9-exiv2 | freshness | 9 | done | e51c89b8 | exiv2 0.28.5 → 0.28.9 (newest 0.28.x). sha256 700b76b97695b2fab4ef8c79619c68ae57d09e0c130724791cafbd39e0eb4aef. Diffed 0.28.5-vs-0.28.9 src/ + xmpsdk/{src,include} + include/exiv2: ZERO file add/remove/rename; cmake/config.h.cmake structurally unchanged. bazel/exiv2.BUILD.bazel: only version strings updated (EXV_PACKAGE_STRING/PROJECT_VERSION/PATCH=9). just bazel-build + 7 metadata/format tests green. NO approval golden shifts. libexpat dep edge left untouched (separate chunk). Verified by orchestrator: //src/cli:sipi builds. Round 11. `build(deps)`. Ticked plan 502. |

| P9-libpng | freshness | 9 | done | c5067b54 | libpng 1.6.54 → 1.6.58 (BCR bazel_dep; 1.6.58 confirmed published in BCR). just bazel-build + formats_test + full approval green; NO golden shifts. MODULE.bazel.lock repinned + committed. Round 11. `build(deps)`. Ticked plan 503 (libpng half; openssl newest-3.5.x half already satisfied by the curl override commit f5ac3c18). |

| P9-libexpat | DEV-7075 | 9 | done | 3b5fa783 | Vendored libexpat 2.8.4 as a native cc_library (ADR-0015), replacing the BCR bazel_dep. New bazel/libexpat.BUILD.bazel: cc_library `@libexpat//:expat` (sha256 b8ece2437692dad44d851c4532723390a5a330990007706be9c8d2b90d294f36, release R_2_8_4/expat-2.8.4.tar.gz); xmltok_impl.c/xmltok_ns.c kept as textual/#included not compiled; local_defines XML_GE=1/XML_DTD/XML_NS/XML_CONTEXT_BYTES=1024 + platform-select entropy (HAVE_ARC4RANDOM_BUF macOS, HAVE_GETRANDOM Linux — hermetic glibc ~2.28 too old for arc4random). Removed bazel_dep(libexpat,2.7.1.bcr.1). Repointed all 4 consumers (src/BUILD.bazel:211, src/format_handlers/BUILD.bazel:79, src/image/BUILD.bazel:73, bazel/exiv2.BUILD.bazel:129 + its line-24 doc). Visibility narrowed to ONLY `@@//src:__pkg__` + `@@//src/format_handlers:__pkg__` + `@@//src/image:__pkg__` + `@@+http_archive+exiv2//:__subpackages__` (canonical name via `bazel mod dump_repo_mapping`). Worker PROVED the visibility bites: a probe dep from //src/image_processing failed with "not visible", then restored (diff-clean). Verified by orchestrator: just bazel-build green, approval + formats_test byte-identical, `bazel mod graph` shows NO expat. Round 11. `build(deps)`. Ticked plan 504, 505, 506. **PHASE 9 CODE-COMPLETE → all in-scope phases (1-10 + S2-39/476) DONE.** |
| T1-docnit | testing-strategy doc | review | done | db3419de | `docs(testing)`: testing-strategy.md Lua-config table `jwt_secret (42 chars)` → `(≥32 bytes)` — 42 was the removed shipped default's length; the S2-11 startup check rejects <32 bytes, so ≥32 bytes is the durable requirement. Independent of T1; landed standalone. Round 12. |
| T1-race | iiif_compliance regression | review | done | d66a15eb | **RESOLVED — racy TEST cleanup, not a production bug.** Session confirmed root cause with raw-socket + curl evidence: `tiff_jpeg_compression_input` did `.send()` → `remove_file(&dst)` (deleted the SOURCE image mid-request) → `resp.bytes()`. Deleting the source mid-stream makes the serve path's later source access fail (S2-25 cache-record source stat / streaming), so S2-27 correctly aborts the chunked stream without the `0\r\n\r\n` terminator → reqwest `UnexpectedEof`. With the source left in place the response is wire-valid and complete (JPEG EOI + terminator; verified via curl exit 0 and raw sockets, close + keep-alive, at fastbuild and -c opt). NO server/decode/encode/concurrency/memory defect. Fix: reorder the test to read+decode the full body BEFORE deleting the fixture (fixture removal now the last statement, with enduring rationale comment). Also removed the non-bug WIP stress test (`tiff_jpeg_concurrency_test.cpp` + its `formats_test` BUILD entry) — it tested a concurrency race proven not to exist (decode is thread-safe; the fastbuild "hang" was `-O0` destructor churn, passes 320x at -c opt) and it wedged the fastbuild `just bazel-test`. Verified by orchestrator: targeted `//test/e2e:iiif_compliance --test_arg=tiff_jpeg_compression_input --test_arg=--exact` PASSED; `just bazel-test` 77/77 green (formats_test no longer wedges); `just bazel-rustfmt-check` clean. Round 13. `test(e2e)`. |

## Blockers (need session/maintainer decision)

- **RESOLVED (Round 13, 2026-09-05):** the session established with raw-socket + curl evidence that the real root cause was a racy TEST cleanup (`tiff_jpeg_compression_input` deleted the source image mid-request, before reading the body), not any production defect. Fixed by reordering the test (commit `d66a15eb`); the non-bug WIP stress test was removed. See the T1-race row above. The Round-12 diagnosis below is kept for provenance.
- **T1 (Round 12, 2026-09-05) — CORRECTED DIAGNOSIS from primary evidence: the JPEG-in-TIFF decode path is NOT racing. The reproducer's "hang" is an `-O0` (fastbuild) execution-cost artifact, not a deadlock.** A fresh orchestrator built the WIP stress test (`tiff_jpeg_concurrency_test.cpp`, 32 threads × 10 rounds of `SipiImage::read` on the 7197×5441 fixture) and ran it under BOTH build configs:
  - **fastbuild (`-O0`, the `just bazel-test` default):** wedges — `timeout 45 ./formats_test` → exit 124. macOS `sample` of the hung process: worker threads are CPU-bound inside `SipiIOTiff::read` → `std::vector<unsigned char>::~vector` → `clear()` → per-element `__base_destruct_at_end` (the unoptimized O(n) trivial-destructor loop over ~470 MB buffers, ×32 concurrent, plus allocator contention). ONLY the main thread sits in `__ulock_wait` (a plain `join()`). No worker is blocked on a lock.
  - **`-c opt` (what production and the CI release image ship):** **PASSES, 5/5 stable, ~4.9 s each** — 320 concurrent full decodes, every one error-free and pixel-identical to the single-threaded reference.
  - **Clincher:** a real mutex/codec deadlock cannot be optimized away — it would wedge at `-c opt` too. It does not. Therefore there is **no decode-path concurrency bug** to fix. The brief's Step-2 remedy (per-instance state / `call_once` / a JPEG-in-TIFF mutex) would harden a non-bug and add a throughput cost for nothing. `SipiIOTiff` in fact carries **no mutable member state** (all methods `static` or local-only); `SipiImage::io["tif"]` is a shared *stateless* handler.
  - **Consequences for the brief's acceptance criteria:** the test as written can NEVER satisfy "`just bazel-test` 77/77" — that suite runs fastbuild, where this test wedges. It is not a red→green regression test either (there is no bug to catch; it passes at opt). The WIP (`tiff_jpeg_concurrency_test.cpp` + its `formats_test` entry in `BUILD.bazel`) is therefore LEFT UNCOMMITTED — landing it would break `just bazel-test`.
  - **The real e2e symptom is unexplained by a decode race and lies elsewhere.** `iiif_compliance::tiff_jpeg_compression_input` issues a SINGLE `full/max/0/default.jpg` request (decode 7197×5441 + encode a full-size JPEG). Its `hyper UnexpectedEof` under S2-27's (correct) BodyAbort points at the **encode path or memory pressure under the full server** (8 engine threads × a ~470 MB decode + full-size JPEG encode ≈ multi-GB peak → memory-budget shed / OOM / abort), NOT the decode path this reproducer exercises. A decode-only unit test cannot capture it.
  - **DECISION NEEDED (tier-1 / maintainer):** (a) reframe T1 as a memory-pressure/encode investigation under the real server (right next probe: run the e2e with `SIPI_NTHREADS=1` and with a raised memory budget to see if the abort disappears — if so it is throttling/OOM, not a codec defect), and drop the decode-race framing; (b) if a cheap concurrency *guard* is still wanted, keep only a `std::call_once` hardening of `registerCustomTIFFTags`' `static bool done` (benign data race, not the cause) — cosmetic, not a fix; (c) decide the fate of the WIP reproducer — delete it, or keep it as an opt-only (`size = "enormous"`/manual-tagged) concurrency smoke that is excluded from the fastbuild suite. No codec/source fix was shipped this round (correctly — there is no decode bug). The iiif_compliance e2e stays red until the encode/memory path is investigated.
- **RESOLVED (round 11): S2-39 sub-fix 4 (plan 476)** — maintainer chose option (a). Landed as `13eddc54` (`server.secure_equals` binding + sipi.init.lua rewrite). Original blocker text kept below for provenance.
- **S2-39 sub-fix 4 (plan 476) — constant-time Basic-auth compare has no host-side call site.** The plan directs "`server.rs:783-823` `require_auth`: constant-time compare for Basic credentials host-side". But `require_auth` (now ~server.rs:790-830) only base64-decodes the `Authorization: Basic` header and RETURNS `{username, password}` to the calling Lua script — it performs NO host-side `==`. The actual credential comparison lives in the shipped `config/sipi.init.lua` (`authorize_page` / `authorize_api`, `auth.username == username and auth.password == password`), which is Lua, not host code. So the checkbox as written cannot be satisfied (there is no host-side compare in require_auth to make constant-time). DECISION NEEDED (do not guess — expands scope / edits shipped Lua): (a) add a host-side `server.secure_equals(a,b)` constant-time binding and update sipi.init.lua's authorize_page/authorize_api to use it; (b) accept the timing channel (Basic auth over a Traefik-terminated TLS edge, credentials returned to operator-supplied Lua) and mark 476 wontfix with rationale; (c) retarget the finding. Recommendation: (a) is the faithful fix but crosses into shipped-Lua + a new binding (scope-discipline "ask first"). Plan 476 left UNTICKED.

## Deferrals

- **Phase 6b oha smoke (plan line 449)**: LEFT UNTICKED. "Record a before/after
  `oha`-style 60 s smoke against the local server in the PR description." This is a
  PR-time record item, not code — the request-body timeout / body-limit layers add
  one timer + one size check per request (no hot-path work). Run an `oha` 60 s smoke
  at PR-assembly time and paste the before/after into the PR body. Not blocking.

- **Phase 5 exhaustive e2e matrix (plan line 405)**: LEFT UNTICKED. The
  security-critical properties of that matrix — anonymous never receives a
  Cookie/Bearer-populated decision, and Cookie vs Bearer are distinct key
  material — are covered by the existing `preflight_cache.rs` e2e
  (`cache_does_not_leak_across_credentials`) plus the S2-43 classification unit
  test. The full 7-permission × 3-credential-channel × 3-endpoint (image /
  info.json / knora.json) agreement sweep is disproportionate to hand-craft
  (each of login/clickthrough/kiosk/external needs a distinct hook branch + a
  distinct e2e). Focused per-finding e2e coverage landed instead (S2-04 region
  scaling, S2-09 403 cases, S2-16 info/knora clamp + 404). Maintainer/late round:
  build the exhaustive matrix if wanted, or accept the focused coverage.
- **Phase 5 dsp-api coordination (plan line 417)**: cross-repo, OUT OF SCOPE
  here (see Cross-repo/maintainer notes). `sipi.init.lua:129-141` bare
  `{type='restrict'}` now yields 403 after S2-09; dsp-api must default a size
  (e.g. `!128,128`) before the release rolls. Maintainer's item; left unticked.

- **Phase 4 S2-27 e2e fixture (plan line 389)**: NOT crafted. The behavior is
  covered by two deterministic sink unit tests (the required gate). Crafting a JP2
  that decodes far enough to commit the JPEG head and then read-errors
  deterministically mid-encode (the SIPI-1Q `phase=write` shape) needs iterative
  probing of the exact byte offset that survives shape-read + partial pull_stripe
  but fails later in the Kakadu codestream — a build/test feedback loop the worker
  judged beyond "a couple of attempts". Maintainer/next round: craft the fixture
  (truncate a real JP2 at a codestream boundary) and add an e2e in
  http_contracts.rs/connection.rs asserting an incomplete/aborted body, or accept
  the unit-test coverage and tick plan 389 + the S2-27 half of plan 591.
- **Phase 3 operator knob `SIPI_DECODE_TIMEOUT_MS`**: the watchdog deadline is
  `EngineContext::decode_timeout_ms` with a fixed 120s default; NOT wired to a clap/env
  knob (would need clap field → SipiServerConfig C-ABI struct + layout asserts → init.cpp →
  EngineContext, a large surface for a safe default). Recorded in the design note as a
  follow-up. Maintainer: file the Linear enhancement if operator tuning is wanted.
- **Phase 3 bench (`just bench decode` before/after)**: not run. A `std::thread` spawn per
  decode is O(µs) against an O(ms–s) decode, so no measurable regression is expected; a
  proper before/after needs a `-c opt` baseline at 5007032c. Deferred with the Phase 1 bench.
- **Phase 3 fuzz.yml `workflow_dispatch` green-on-j2k-leg**: the `continue-on-error` code
  removal landed (0a399f2a); the actual green-on-a-dispatch-run is a CI action only the
  maintainer/session can trigger. Corpus-replay proxy stays green.


- **Phase 1 bench (plan checkbox "just bench decode before/after")**: not run. All Phase-1
  codec changes are O(1) comparisons or vector-sizing (no per-pixel work added), so no
  hot-path regression is expected. A proper before/after needs a `-c opt` baseline at
  `5007032c`; run next round or at PR close. Not blocking.
- **Phase 1 phase-wide checkboxes** "Fixtures (LFS, malformed/)" and "Unit tests in
  *_test.cpp ...": RESOLVED round 2 — ticked. 22 fixtures present under
  test/_test_data/images/malformed/; S2-38 items are defensive (positive/regression tests,
  no new malformed fixture needed). Full `just bazel-test` green locally (73/73). The
  pre-fix asan-ubsan CI-red confirmation remains the maintainer's CI concern (local ASan
  broken) — not a code gap.
- **Phase 1 "just bazel-test green on all three platforms"**: ticked on the strength of a
  local macOS `just bazel-test` (73/73 green). linux-x86_64 / linux-aarch64 are CI-verified
  (build-completeness invariant); a green CI run on the branch confirms them.
- **Phase 2 exec/s before/after (plan line 363, J2K throughput)**: max_len 32768→8192 landed
  (afd… commit, Round 3), but the exec/s before/after is a nightly-loop measurement that
  cannot be produced locally. Record it in the PR from a `workflow_dispatch` fuzz run (or a
  local `just fuzz j2k -max_total_time=60` before/after if a maintainer wants a quick number).
- **Phase 2 item (plan line 365): Linear follow-up to sunset the plain-`read` decode call /
  targets** once their corpora migrate into the header-prefixed path, referenced from the
  harness comment — NOT filed (orchestrator has no Linear access). Maintainer: file it and
  add the issue ref to the `run_decode` comment in codec_fuzz_harness.h. Also record the
  nightly runtime cost of the 4 added round-trip legs in the PR.
- **Phase 2 final checkbox (plan line 368): `fuzz.yml workflow_dispatch green on all legs`**
  — the corpus-replay-green half is satisfied (`bazel test //src/format_handlers/fuzz/...`
  = 8/8, and `//src/...` sweep green). The `workflow_dispatch` green-on-all-legs half is a
  CI action only the maintainer/session can run; the j2k leg is `continue-on-error` until
  Phase 3 removes it. Left UNTICKED as a CI gate.

## Round 1 result (this run)

Landed all Phase-1 memory-corruption Criticals + Highs + the two codec Mediums, 7 commits
(afb1eaa8, 1f11444a, daaf582f, 3d17f01c, 45c8b606, a0f770f5, 81765f81). Group-verified:
`bazel test //test/approval //src/...` = 43/43 green (approval byte-identical except the
recorded S2-14 IPTC +4B; fuzz corpus-replay green). Stopped for context health, not a blocker.

## Next round — start here (priority order 1→10)

**ROUND 6 UPDATE — Phase 5 AND Phase 6a are code-complete (6 commits this round).**
Phase 5: b61f5ddc S2-04, 76c29aec S2-09, 58623817 S2-16, 2f9d390e S2-43.
Phase 6a: a1cd4767 S2-17, 1e08282e S2-40.
**ROUND 8 UPDATE — Phase 6b is CODE-COMPLETE (5 commits total; 4 landed this round).**
6b: S2-18 (f51c2fe7, r7), S2-19 (08baf5bb), S2-20 (e337543a), S2-21 (5cb1a613), S2-33 (0c7f2114).
**START ROUND 9 AT → Phase 6c** (Cookies, client identity, fairness ruling — S2-24/S2-34/S2-44,
DEV-7140, plan ~451-460), then 7, 8, 9, 10 in strict priority order.

READY-FACTS for round 9 / Phase 6c (reuse the brief's "Pre-made design ruling · Phase 6c" verbatim; do NOT re-open):
- **Fairness ruling = (b) Traefik** → ADR-0022 amendment stating "no SIPI code; enforced in Traefik". NOTE: ADR-0022 already got a *default-flip* amendment this round (2026-09-05, top of file); the 6c fairness amendment is ADDITIVE — append a second amendment note, do not overwrite the first. Two-lane/fairness design substance in ADR-0022 was deliberately left untouched for you.
- **DROP `client_ip` from `SipiServeRequest`** (offset-locked seam — re-baseline EVERY following offset): `ffi.rs` field (~308) + `offset_of!` block (~1421); `sipi_ffi.h` field (~210) + `static_assert` block (~500); `routes.rs` `c_client_ip` (~760) + assignment (~776); fixture `serve_image_test.cpp:72`. Engine never reads it (no consumer in src/ffi/cpp). **KEEP** the Lua-facing `RequestData.client_ip` / `server.client_ip` (bindings/mod.rs:50, server.rs:202, routes.rs:695,1000) — separate path. After the drop, run `//src/ffi:serve_image_test` + `//src/server/rust:sipi_unit_test` (the ffi.rs offset_of! test is the gate) + BOTH lint gates.
- **Cookies (S2-24):** bindings/mod.rs ~79-104 `ResponseCookie` `http_only` default → true (~99); ADD a `same_site` field (none today) rendered `SameSite=Lax` by default in `render()` (~104), with a `sendCookie` override in server.rs; update struct doc (~77-78) + pinned expectation in `bindings_tests.rs:184` (`"sid=s3cr3t; Path=/; Secure; HttpOnly"` → add `; SameSite=Lax`). sink.rs `apply_headers` (~316-330): `map.append` ONLY for `header::SET_COOKIE`, keep `map.insert` for everything else (each cookie already arrives as its own pair). Update docs/src/lua/index.md.
- e2e (red): a script setting two cookies emits two `Set-Cookie` headers with `HttpOnly; SameSite=Lax` (red on main: http_only=false, no SameSite, second Set-Cookie replaces first).
- Shared file: Phase 6c edits docs/src/lua/index.md (Phase 5 already touched it; Phase 7 rebases after). No known hang-risk e2e in 6c.

**START HERE (round 8): Phase 6b remaining — S2-19, S2-20, S2-21, S2-33** (historical, now done):

Phase-5/6a items left unticked BY DESIGN (not code gaps): plan 405 (exhaustive 7×3 e2e matrix —
credential-separation covered by preflight_cache.rs + S2-43 test; full sweep disproportionate, see
Deferrals) and plan 417 (dsp-api sipi.init.lua bare-restrict default — cross-repo maintainer item).

READY-FACTS for round 7 (reuse, don't re-derive):
- `SipiStatus::Forbidden=403` EXISTS (added S2-09, serve_response.h). Seam returns `static_cast<int>`;
  `sink::map_status` renders any int via `from_u16`.
- Env-knob pattern: mirror `config::allowed_origins_from_env` / the new `config::public_hosts_from_env`
  (`parse_csv_list`); thread a `Vec<String>`/value field through `AppState` (routes.rs ~86) + `AppState::load`
  + the `lib.rs` call site (~441-455). `state.has_preflight` = "a preflight hook is configured".
- Phase 6b: `MODULE.bazel` `tower` crate.spec ~547 (add `tower-http` `["timeout"]`); axum::serve at lib.rs ~489;
  admission.acquire(Full) AFTER body read (routes.rs ~1103); `AppState::load` single source of `max_post_size`
  (routes.rs ~181, `ffi::max_post_size().unwrap_or(0)`); 3 `==0` unlimited branches (Lua-route ~974-978
  DefaultBodyLimit::disable, ~1082-1086 usize::MAX, docroot ~1346/`docroot_method_router` ~1411-1415 disable);
  docroot handler `docroot_method_router`/`serve_docroot` (routes.rs ~1388/1426); C++ `sipi_image_new`
  (image_handle.cpp ~94-151) charge `eng.memory_budget` like serve_image.cpp ~655; admission_mode default flip
  = `feat(throttling)!:` + BREAKING footer. MODULE.bazel edit ⇒ relock + 3-platform `just bazel-build` gate (SLOW).
  Header-timeout/conn-cap ruling = (b) Traefik (Phase 11 maintainer) + record option (a) as a Linear follow-up.
- Phase 6c: `SipiServeRequest.client_ip` STILL on the seam (ffi.rs field ~308 + offset_of! ~1421; sipi_ffi.h
  field ~210 + static_assert ~500; routes.rs c_client_ip ~760 + assignment; serve_image_test.cpp fixture ~72)
  — 6c DROPS it (offset/static_assert re-baseline). KEEP the Lua-facing RequestData.client_ip/server.client_ip.
  cookies: bindings/mod.rs ResponseCookie (~79) http_only default → true + add same_site (SameSite=Lax);
  bindings_tests.rs expectation ~184; sink.rs apply_headers ~316 append only for Set-Cookie.
- forwarded() now takes `public_hosts: &[String]` (6 call sites already threaded); do not regress that.

(historical, priority order 1→10:)

**Phase 1 is code-complete** (rounds 1+2). **Phase 2 is code-complete** (round 3). All Phase-1
and Phase-2 CODE checkboxes ticked. Do NOT re-do any of them. Remaining un-ticked non-code boxes:
Phase-1 `just bench decode` (plan 344, deferral); Phase-2 Linear-follow-up (plan 365, no orchestrator
Linear access — maintainer files it) and `fuzz.yml workflow_dispatch green on all legs` (plan 368,
CI action; corpus-replay half satisfied). See Deferrals.

**Phase 3 is code-complete (round 4).** Two commits: `e57131e9` (design note, docs(specs)) +
`0a399f2a` (fix(ffi): watchdog mechanism + wedged_threads gauge end-to-end + deadline unit tests +
fuzz.yml continue-on-error removal — S2-13/DEV-7080). All Phase-3 checkboxes ticked (the "green on
a dispatch run" half of item 379 is a maintainer CI action; see Deferrals). SipiMetricsSnapshot is
now size 176 (offset 168 = wedged_threads); future fields append after it.

**Phase 4 is code-complete (round 5).** Three findings, three commits: `035b030e`
(fix(iiifparser): rotation/region/size-pct bounds + PERCENTS derived-output clamp, S2-05),
`8438d987` (fix(ffi): BodyAbort on post-commit encode failure, S2-27/SIPI-1Q), `c4c50548`
(fix(ffi): Range header length cap, S2-37). All Phase-4 checkboxes ticked EXCEPT plan 389
(S2-27 e2e mid-encode fixture — deferred, unit-tests cover it; see Deferrals) and the S2-27
half of plan 591 (same deferral; the Range→400 half is done+tested). Note: `SipiSize::get_size`
PERCENTS now clamps derived w/h to limitdim(32000); `apply()` in serve_response.cpp now returns
`int` (body-delivery code) and both `sipi_serve_*` map non-zero→InternalError; `serve_streaming`
(sink.rs) emits `Err(BodyAbort)` on `head_sent && code != 0`.

**START HERE (round 6): Phase 5 — Authorization semantics (S2-04, S2-09, S2-16, S2-43), DEV-7137,
opus/high, plan lines ~396-441.** Large, interlocked phase (restrict-as-scale, new
`SipiStatus::Forbidden=403` in serve_response.h, info.json/knora.json `Vary: Cookie,Authorization`
+ `Cache-Control: private,no-store` when a hook is configured, preflight cache key material,
explicit RequestData literal, `pub fn parse_size` exposed from iiifparser lib.rs). All Phase-5
design rulings are pre-made in the brief's "Pre-made design rulings" block — do NOT re-open them;
implement and record. Phase 5 lands first of the three phases that edit docs/src/lua/index.md
(then 6c, 7). Test-first: the two e2e red checkboxes (plan 404-405) before the fixes. One commit
per finding. Then 6a, 6b, 6c, 7, 8, 9, 10. Parser/shell/Rust changes → BOTH lint gates per commit.

**(historical Phase-3 sequencing, kept for provenance):**
1. **Design note** `docs/specs/2026-09-04-sipi-security-hardening-wave-2/02-jp2-decode-watchdog-design.md`:
   the maintainer's ruling + plan recommendation is mechanism **(b)** — a wall-clock deadline at the
   FFI seam that fails the request when `read_shape`/`read` exceeds it. A wedged Kakadu thread cannot
   be joined, so the note MUST state how the leaked thread is bounded (by `nthreads`) and surfaced
   (`wedged_threads` gauge → `/health` restart rule at `nthreads-1`). This IS decided; write it, do
   not re-open. This is a spec-folder doc (author via a worker or orchestrator judgment).
2. **Deadline unit test** using the committed hang fixtures at `test/_test_data/images/hang/`
   (`dev7080_read_shape_hang.jp2`, `nightly_j2k_read_shape_hang.jp2`) — deadline-bounded, red on
   `main` by timeout (the hang). Keep these OUT of any corpus.
3. **Implement (b)** at the FFI seam (`src/ffi/cpp/serve_image.cpp`), NOT a box pre-pass (that is
   option (a); one layer only).
4. **`wedged_threads` gauge END TO END** per CONVENTIONS.md § Metrics and plan line 378 (this is
   the big multi-file part, touches Rust → BOTH `just bazel-rustfmt-check` && `just bazel-clippy-check`
   gates, and the OFFSET-LOCKED FFI struct `SipiMetricsSnapshot` — every offset re-baselines):
   `src/observability/cpp/metrics.h` (member + bump), `src/ffi/cpp/metrics_snapshot.h` (field +
   size/offset static_asserts), `src/ffi/cpp/sipi_ffi.cpp` (populate), `src/server/rust/src/ffi.rs`
   (mirror + layout test), `src/server/rust/src/metrics.rs` (GAUGES row), `metrics_registry_test`
   (`every_snapshot_field_is_accounted_for` count). Exported over OTLP + reported by `/health`
   (`docs/src/operation/health-endpoint.md` — Phase 6a also edits this file; whoever is second rebases).
5. **Remove the Phase-2 j2k `continue-on-error`** from `.github/workflows/fuzz.yml` (the "Fuzz
   (libFuzzer, 600s)" step) — j2k leg should go green once the watchdog lands.
   NOTE: metrics_snapshot is scalar-only across the seam (see MEMORY: label-fanned families don't cross).

Then priority order: **Phase 4, 5, 6a, 6b, 6c, 7, 8, 9, 10.** Rust-touching phases need BOTH
`just bazel-rustfmt-check` && `just bazel-clippy-check` before each commit (hard gate, NOT covered by
bazel-test). Two breaking commits: S2-33 `feat(throttling)!` (6b), S2-42 `refactor(cli)!` (7).
MODULE.bazel edits (6b tower-http `["timeout"]`, 9 dep bumps, 7 field removals) trigger the 3-platform
`just bazel-build` gate + relock. Design rulings already made (do not re-open; see plan's binding
"Design decisions" + brief): 6b header-timeout = (b) Traefik + in-code tower_http request/body-read
timeouts + finite max_post_size default; 6c fairness = (b) Traefik, DROP client_ip from
SipiServeRequest; Phase 8 adds a new cache-index-header ADR; Phase 7 implements config literal
removal + startup jwt_secret check regardless of Phase 0.

Operational reuse (confirmed round 3): build/test = `GH_TOKEN=$(gh auth token) nix develop --command
bazel <...>`; cache warm. No local PyYAML — validate workflow YAML with `ruby -ryaml -e`. Fuzz
corpus-replay = `bazel test //src/format_handlers/fuzz/...` (8 targets now). Full `--config=fuzz`
fuzz build is slow and CI-only; local `bazel test //src/format_handlers/fuzz/...` (default config)
is the cheap correctness proxy.

## Round 2 result (this run)

Finished Phase 1: landed S2-38 as 3 commits (a9570f6e metadata, f8103649 format_handlers,
d250258e image_processing). Verified each targeted test green + full `just bazel-test` 73/73
green locally on macOS (approval byte-identical). Ticked all Phase-1 code checkboxes + the
two phase-wide (fixtures, unit tests) + the approval/bazel-test box. Only the `just bench
decode` box left (deferral). Stopped at the Phase-1 boundary for a clean handoff; Phase 2 is
a coherent opus-flavored unit best started fresh (and its reproducer download is time-sensitive).
No blockers.

## Round 3 result (this run)

Group-verified Phase-1 head first: `bazel test //test/approval/... //src/...` = 43/43 green
(the plain `//test/approval` label the earlier journal note used is invalid — needs `/...`).
Then completed **Phase 2** code-work in 6 commits (priority order):
- `6685a5b5` test(fuzz): DEV-7080 hang reproducers → test/_test_data/images/hang/ (LFS, no corpus).
- `0ecbf725` test(fuzz): S2-31 recovery — counting libtiff warning handler (LLVMFuzzerInitialize
  in tiff_decode_fuzz.cc), 50M log cap on 3 tee'd files, j2k Fuzz-step continue-on-error.
- `3954d313` test(fuzz): DEV-7081 — parse_request leg ASAN_OPTIONS=detect_container_overflow=0.
- `aecbbb1d` test(fuzz): S2-32 decode coverage — header-prefix region/size-aware 2nd pass in
  codec_fuzz_harness.h (kept plain read; templated fuzz_temp_path fixing a latent static bug).
- `232410ca` test(fuzz): S2-32 encode coverage — run_roundtrip + 4 *_roundtrip_fuzz targets +
  BUILD + fuzz.yml matrix (5→9 legs) + fuzzing.md + fuzz_seeds seeding + justfile wiring.
- `6b50f8c4` test(fuzz): S2-31 j2k throughput — decode max_len 32768→8192 (fuzz.yml + justfile).
Verified: `bazel test //src/format_handlers/fuzz/...` = 8/8 corpus-replay green (after aecbbb1d
and after 232410ca); YAML validated via ruby; justfile parses + new target labels resolve.
All Phase-2 CODE checkboxes ticked; plan 365 (Linear) + 368 (workflow_dispatch) deferred (see
Deferrals). No blockers. Stopped cleanly at the Phase-2/Phase-3 boundary for context health —
Phase 3 (offset-locked FFI struct + wedged_threads metric end-to-end + Rust lint gates) is the
plan's most intricate chunk and is best started with a fresh orchestrator's full context, not
half-done. Nothing uncommitted except plan/journal state files (working-tree writes, per model).

## Round 4 result (this run)

Completed **Phase 3** (JP2 decode watchdog, S2-13/DEV-7080) in 2 commits:
- `e57131e9` docs(specs): the mechanism design note (b, seam deadline; committed standalone first).
- `0a399f2a` fix(ffi): the whole mechanism as one finding commit — two workers accumulated into
  the working tree (P3-metric: wedged_threads gauge end-to-end; P3-watchdog: `run_with_deadline`
  helper wrapping both seam decode calls + deadline unit tests over the hang fixtures + fuzz.yml
  continue-on-error removal), then one commit.
Verified before commit: `bazel test //src/ffi:serve_image_test //src/ffi:metrics_snapshot_test
//src/observability:metrics_registry_test //src/server/rust:sipi_unit_test` = 4/4 green (serve_image
15/15, both hang fixtures trip at ~2.0s under a 2s deadline), plus BOTH `just bazel-rustfmt-check`
and `just bazel-clippy-check` green. All Phase-3 checkboxes ticked. Deferrals recorded
(SIPI_DECODE_TIMEOUT_MS knob follow-up, Phase-3 bench, workflow_dispatch green half). No blockers.
Stopped cleanly at the Phase-3/Phase-4 boundary for context health.

Design/scope rulings made this round (recorded so they are not re-litigated):
- Deadline is `EngineContext::decode_timeout_ms` (default 120000ms), NOT a clap knob — a fixed safe
  default; operator tuning is a deferred follow-up. This keeps init.cpp / the SipiServerConfig
  C-ABI struct untouched.
- Both `read_shape` and the full `read` seam decode calls are wrapped by the ONE deadline mechanism
  (one layer covering two entry points — not defense-in-depth). The hang fixtures trip at read_shape.
- Timeout → 500 InternalError (deterministic per-file hang is not retryable), reported via the
  existing Sentry side channel with phase "read".
- `wedged_threads` is a Gauge (gained a `Gauge::Increment()`), appended LAST in the snapshot gauges
  so all prior offsets stayed put (size 168→176).

## Round 5 result (this run)

Completed **Phase 4** (IIIF input bounds and engine DoS) in 3 commits, one per finding:
- `035b030e` fix(iiifparser): S2-05 — parse.rs rejects non-finite / out-of-range rotation
  (0..=360), region Coords (0..=2147483647) / Percents (0..=100), size pct (non-finite);
  SipiSize.cpp get_size PERCENTS clamps derived w/h to limitdim(32000). Parser unit tests +
  e2e + proptest (39-45 digit sweep) + 2 corpus seeds.
- `8438d987` fix(ffi): S2-27 (Sentry SIPI-1Q) — apply() returns body-delivery code, sipi_serve_*
  map non-zero → InternalError, serve_streaming emits Err(BodyAbort) on post-commit failure.
  Two deterministic sink unit tests are the gate.
- `c4c50548` fix(ffi): S2-37 — parse_range rejects Range headers >128B before the regex.
Verified per commit: targeted tests green + BOTH rust lint gates green (S2-05, S2-27);
serve_response_test green (S2-37); e2e targets compile. Two follow-up workers used: one rustfmt
diff fix (proptest), one FFI comment correction (sipi_ffi.cpp — the "!head_sent unreachable"
claim was inaccurate; both failure branches are live). Stopped cleanly at the Phase-4/Phase-5
boundary for context health — Phase 5 is the large opus-flavored authorization phase, best started
fresh. Deferral: S2-27 e2e mid-encode fixture (plan 389 + S2-27 half of 591) — behavior is
unit-test-covered; crafting a deterministic mid-encode-failing JP2 needs a build/test probing loop.

## Operational facts confirmed this round (reuse, don't re-derive)

- Rust unit-test target is `//src/server/rust:sipi_unit_test` (carries the ffi.rs layout tests +
  metrics.rs registry tests). C++ metrics-seam tests: `//src/observability:metrics_registry_test`
  (inventory) + `//src/ffi:metrics_snapshot_test` (struct layout). Seam serve tests:
  `//src/ffi:serve_image_test` (has `data=//test/_test_data:images`, which globs images/hang/**).
- `EngineContext` is an engine-internal C++ struct (NOT a layout-locked seam struct); adding a field
  with a default costs nothing at the C ABI (designated-init in init.cpp leaves it defaulted).
- `health-endpoint.md` has no metrics table — new metrics get a prose subsection. Phase 6a also
  edits this file; it must rebase onto the new "Wedged Decode Threads" section.

- Build/test: `GH_TOKEN=$(gh auth token) nix develop --command bazel <...>`. Cache is warm.
- Fixture tooling: a venv with tifffile+imagecodecs+numpy at
  `/private/tmp/claude-502/-Users-subotic--github-com-dasch-swiss-sipi--claude-worktrees-security-analysis/6d1e5699-54bf-4fba-9747-2a0262e21721/scratchpad/vfix/bin/python3`
  (session-scoped; recreate with `python3 -m venv` + `pip install tifffile numpy imagecodecs` if gone).
- Kakadu headers (license-gated, materialized) readable at
  `/Users/subotic/Library/Caches/bazel/_bazel_subotic/ebf1373ef392e57fd1da76cd28a1ee14/external/+kakadu_extension+kakadu/`.
- Most format-handler unit tests live in `//src/format_handlers:formats_test`; color/channel tests
  in `//src/image_processing:image_processing_test`. Add new *_test.cpp to the existing cc_test srcs.
- Approval re-approval: `bazel test` sandbox deletes `.received.*`; run the approval binary directly
  with `SOURCE_DATE_EPOCH=946684800` + a scratch TEST_TMPDIR to retrieve them (see S2-14).

## Round 6 result (this run)

Completed **Phase 5** (authorization semantics) and **Phase 6a** (host trust + docroot) — 6 commits,
one per finding, all verified per commit (targeted tests + BOTH `just bazel-rustfmt-check` &&
`just bazel-clippy-check` green):
- `b61f5ddc` fix(ffi): S2-04 restrict-as-scale (sampling-factor cap across regions).
- `76c29aec` fix(ffi): S2-09 refuse non-restricting restrict decisions (SipiStatus::Forbidden=403;
  seam check keyed off restricted_size!=null; Rust edge 403 for bare restrict + /file restrict).
- `58623817` fix(server): S2-16 info.json/knora.json restricted-dims info-leak (iiifparser::clamp_dims_to_size,
  KnoraJson gated → 404, clamped dims + no tile grid for non-Allow, Vary+Cache-Control when hook configured).
- `2f9d390e` fix(server): S2-43 query-free preflight cache key (explicit RequestData literal + classification
  test + docs; no behavior change — hardening only).
- `a1cd4767` fix(server): S2-17 SIPI_PUBLIC_HOSTS forwarded-host allowlist (forwarded() validates in one place).
- `1e08282e` fix(server): S2-40 uniform 404 for image-path traversal + docroot dotfile skip.

Design rulings made this round (recorded so they are not re-litigated):
- S2-16 clamp: exposed `iiifparser::clamp_dims_to_size` (NOT the plan's literal `parse_size->SizeKind`, which
  can't compute dims and would be dead API) — honors the checkbox's stated goal "server never re-implements a
  security-relevant clamp". KnoraJson non-Allow/Restrict → 404 (hide existence on the DSP-internal metadata
  surface) — reconciles the plan tension between checkbox 404 (404) and plan 411 ("like the image route"/401).
- S2-09 seam check keys off `req.restricted_size != nullptr`, not `SipiSize::undefined()` (size=max parses to
  the same undefined shape); bare restrict caught on the Rust edge (seam can't see null/null vs allow).

Stopped cleanly at the Phase 6a / Phase 6b boundary for context health (six worker cycles + heavy file reads
this round). No blockers. Phase 6b is a coherent heavy unit (a BREAKING admission-mode flip + MODULE.bazel
relock + 3-platform build gate + 5 findings) best started with a fresh orchestrator's full context. Nothing
uncommitted except the plan/journal state files (working-tree writes, per model).

## Round 8 result (this run)

Finished **Phase 6b** (HTTP shell resource exhaustion + shipped defaults) — 4 commits this round,
one per finding, each verified by the orchestrator (targeted tests + BOTH lint gates where Rust
was touched; e2e bounded per the CRITICAL hang rules):
- `08baf5bb` fix(server): S2-19 — finite `DEFAULT_MAX_POST_SIZE` (256 MiB) + `SIPI_BODY_READ_TIMEOUT`
  (10 s) `RequestBodyTimeoutLayer` on Lua-route + docroot routers + 64-part multipart cap; three
  `==0` unlimited branches deleted; admission.acquire stays AFTER the (now bounded) body read.
  e2e trickle test uses a raw socket with an 8 s failsafe — completes ~2.7 s, NO hang (the prior
  round's killer is resolved).
- `e337543a` fix(ffi): S2-20 — `sipi_image_new` charges the full-lane memory budget (read_shape +
  estimate + try_acquire, RAII guard) so Lua `SipiImage.new` bomb decodes are refused. Deterministic
  tiny-budget unit tests (no bomb fixture).
- `5cb1a613` fix(server): S2-21 — docroot static serving takes a Tile permit; libmagic DB cached in a
  per-thread `thread_local` (load-once, was reloaded per request).
- `0c7f2114` feat(throttling)!: S2-33 — **BREAKING** admission_mode default flip basic→advanced +
  shipped-defaults regression tests + docs + ADR-0022 amendment. Verified upload e2e stays green under
  the new advanced default (no shed on normal decodes).

All Phase-6b code checkboxes ticked except plan 449 (oha PR-smoke — PR-time record item, deferred).
Stopped CLEANLY at the Phase 6b / Phase 6c boundary for context health: 6c is the intricate
offset-locked-seam phase (dropping `client_ip` from `SipiServeRequest` re-baselines every following
offset in ffi.rs + sipi_ffi.h + the fixture) and is best started with a fresh orchestrator's full
context. No blockers. Nothing uncommitted except the plan/journal state files (working-tree writes).

## Round 9 result (this run)

Completed **Phase 6c** (3 commits) and landed 4 of 6 **Phase 7** findings (+ 3-of-4 of a 5th) — 7 code commits total this round, one per finding, each verified by the orchestrator (targeted tests + BOTH lint gates where Rust was touched):
- `77b29cee` refactor(ffi): S2-44 — drop client_ip from the offset-locked SipiServeRequest seam (size 168→160, all following offsets re-baselined in ffi.rs + sipi_ffi.h; Lua-facing client_ip kept).
- `072c46aa` fix(scripting): S2-24 — cookies default HttpOnly + SameSite=Lax; apply_headers appends Set-Cookie so multiple cookies survive; two-cookie e2e.
- `343d9c5e` docs(adr): S2-34 — additive ADR-0022 fairness amendment (b Traefik, no SIPI code).
- `e28b6808` fix(server): S2-11 — fail-closed jwt_secret startup check + drop shipped default; wide config/e2e blast-radius migrated to a dev secret; empty-secret e2e rewritten to a refusal test.
- `39d05523` fix(scripting): S2-39 (3 of 4) — print→tracing shim, fs_mkdir mode mask, lua_to_json depth cap. **Sub-fix 4 (plan 476) BLOCKED — see Blockers.**
- `11a433cd` fix(server): S2-46 — refuse startup when docroot overlaps imgroot/tmpdir/scriptdir/cache_dir (RCE guard) + sandbox docs (no confine helper).
- `33756eb0` fix(scripting): S2-22 — delete token.lua + /api/token route (XSS/token-exfil relay).

Stopped CLEANLY for context health after 7 worker cycles, BEFORE starting S2-23 (upload.lua) and S2-42 (BREAKING). No overflow risk taken. Nothing uncommitted except the plan/journal state files.

## Round 10 result (this run)

Landed the Phase 7 remainder, ALL of Phase 8, and ALL of Phase 10 — 5 commits, one per finding (S2-25+S2-26 grouped as one atomic on-disk-format change), each verified by the orchestrator (targeted tests + BOTH lint gates where Rust was touched; e2e bounded per the hang rule):
- `1d91c690` fix(scripting): S2-23 — upload.lua token gate + uuid62 basename + dead-branch removal; fork deleted, e2e drives the one script (13/13, incl. new 401 test).
- `abfef3b0` refactor(cli)!: S2-42 — BREAKING removal of the dead adminuser/adminpasswd surface (SipiServerConfig 240→224, both sides re-baselined).
- `137e93af` fix(cache): S2-25+S2-26 — cache-index integrity (source_size freshness, SHA-256 key, fixed header, strnlen/fsize-from-lstat hardening, O_NOFOLLOW reads) + ADR-0025.
- `922a8b83` fix(cache): S2-45 — cache_used_bytes underflow clamp at all 3 decrement sites.
- `9af69ea8` fix(ffi): S2-28 — Sentry input_file→basename + request_uri out of tags + ADR-0018 amendment.

Phases 7 (except the plan-476 maintainer blocker), 8, and 10 are CODE-COMPLETE. Stopped CLEANLY at the Phase 8/10 → Phase 9 boundary for context health BEFORE starting any Phase 9 native dep bump (they rebuild from source; the brief warns to stop rather than get stuck mid-bump). No overflow risk taken. Nothing uncommitted except the plan/journal state files.

## Round 11 result (this run) — PHASE 9 COMPLETE; ALL IN-SCOPE CODE DONE

Landed the S2-39/plan-476 maintainer decision + ALL of Phase 9 in 11 commits, one per
finding / per dep bump, each verified by the orchestrator (targeted tests + BOTH lint gates
where Rust was touched; `just bazel-build` + approval byte-check per native bump):
- `13eddc54` feat(scripting): S2-39/476 — `server.secure_equals` constant-time binding + sipi.init.lua rewrite (maintainer option (a)). **Phase 7 now fully complete; blocker resolved.**
- `19cc2805` chore(ci): S2-41 — SHA-pin third-party actions, fix setup-python self-ref, drop claude contents:write, add SECURITY.md.
- `0a1e6d09` chore(ci): S2-30 — cargo_lockfile → checked-in Cargo.Bazel.lock, `just audit` + CI dependency-audit job, Docker Scout Critical gate, dependabot/ci.md coverage docs.
- `a8969bf6` build(deps): libtiff 4.7.1 → 4.7.2 (5 TIFF goldens re-approved, sipi compare pixel-identical).
- `f5ac3c18` build(deps): curl 8.12.0.bcr.1 → 8.21.0.bcr.1 + single_version_override holding openssl at 3.5.5.bcr.4 (resolved the curl↔openssl-4.x MVS conflict informationally; also satisfies plan 503's openssl half).
- `c2e89893` build(deps): lcms2 2.16 → 2.19.1 (1 ICC golden re-approved, diff confined to ICC bytes).
- `e51c89b8` build(deps): exiv2 0.28.5 → 0.28.9 (no golden shift).
- `c5067b54` build(deps): libpng 1.6.54 → 1.6.58 (no golden shift).
- `3b5fa783` build(deps): vendor libexpat 2.8.4 as a native cc_library, visibility narrowed to the 4 consumers (visibility proven to bite).

ALL Phase 9 code checkboxes ticked (499-513, 504-506). Remaining unticked `[ ]` in phases 1-10 are
the previously-recorded DEFERRALS/cross-repo/CI-action items (bench before/after, fuzz.yml
workflow_dispatch green, S2-27 mid-encode e2e fixture, Phase-5 exhaustive 7×3 e2e matrix, dsp-api
bare-restrict coordination, Phase-6b oha smoke, Phase-0 jwt_secret verification, Sentry operator
follow-through) — none is orchestrator code work; see Deferrals + Cross-repo notes. There are NO
more code chunks to dispatch. Next: full-suite run + adversarial review + ship (session/tier-1).

## (historical) Next round — START HERE: Phase 9 only (deps and supply chain, DEV-7143)

Phase 9 is the ONLY remaining in-scope phase. It is the build-heavy one — PACE IT: one native bump per worker, verify (`just bazel-build` on macOS at minimum + approval byte-check; coverage never worked, MEMORY says stretch-not-gate), commit `build(deps):`, move on; STOP and return the report before context runs low rather than mid-bump. Use the brief's "Pre-made design ruling · Phase 9" verbatim. Order (plan ~499-513):
1. Native dep bumps, each its OWN `build(deps):` commit + 3-platform build + approval byte-identical (record any golden shift in test/approval/CHANGELOG.approval.md): libtiff 4.7.2 (re-verify bazel/libtiff.BUILD.bazel codec flags), curl 8.21.0.bcr.1 (revisit `--@curl//:ssl_lib=openssl`), lcms2 2.19.1, exiv2 newest 0.28.x, then a SEPARATE commit for libpng 1.6.58 + openssl newest 3.5.x.
2. Vendor libexpat 2.8.4 as bazel/libexpat.BUILD.bazel (native cc_library, ADR-0015; visibility narrowed to exiv2 + src/image/BUILD.bazel:73 + src/format_handlers/BUILD.bazel:79 + src/BUILD.bazel:211) + repoint the XMP SDK + those 3 consumers.
3. CI/supply-chain (CHEAP, no native rebuild — could be done first if context is tight): set `cargo_lockfile` on crate.from_specs → checked-in Cargo.Bazel.lock + add `just audit` (cargo audit -f + OSV-Scanner) CI job failing on Critical/High; SHA-pin third-party actions; fix ci.yml:401 self-ref setup-python → local `./` form; Docker Scout fail on Critical in publish.yml (verify scout-action@v1 input name); claude.yml drops contents:write; add SECURITY.md at repo root; dependabot.yml coverage note + quarterly native-pin checklist in ci.md.

Note: `@openssl//:crypto` was ADDED to src/cache/BUILD.bazel this round (S2-26 SHA-256) — the openssl bump in step 1 must keep that consumer building.

### (historical) Phase 7 remaining — DONE round 10 except the blocker
S2-23 (1d91c690), S2-42 (abfef3b0) landed round 10. Plan 466 (multi-finding red box) fully satisfied and ticked. The S2-39/plan-476 constant-time-compare item stays BLOCKED (maintainer decision — see Blockers). Phases 8 (137e93af, 922a8b83) and 10 (9af69ea8) also code-complete round 10. Only Phase 9 remains.

## Side findings

- S2-06 required an adjacent fix (folded into daaf582f): the tiled tile-count consistency check
  didn't account for PLANARCONFIG_SEPARATE storing nc on-disk tile sets, rejecting every valid
  separate-planar multi-sample tiled TIFF. Corrected — the finding's fix was otherwise unreachable.
- S2-15: `kdu_codestream_comment::get_text()` returns NULL only when `!exists()` (already gated by
  the loop), not for binary comments (those return ""; `get_data()` is the binary accessor). The
  NULL guard is defensive and currently unreachable/untested; no binary-COM fixture was crafted.
- S2-45 (round 10): the underflow root cause the worker found — `add()`'s duplicate-canonical path
  decrements the counters but does NOT erase the stale `cachetable` entry before calling `purge()`,
  so `purge()` can rediscover and re-evict it, double-decrementing. The S2-45 guard treats the
  symptom on `cache_used_bytes` (the plan's chosen approach). The SAME window also double-decrements
  `nfiles` (unsigned atomic), left UNGUARDED (out of S2-45 scope). Maintainer follow-up: either guard
  `nfiles` the same way, or fix the root cause (erase the stale entry before purge in add()'s
  duplicate path) which fixes both counters at once. Not filed in Linear (no orchestrator access).

## Review fixes (post-review round, fresh spawn)

Fresh orchestrator round after the full-suite run + 5-reviewer adversarial pass on the
code-complete branch. 7 fixes in priority order: RUST-001 (info/knora bare-restrict),
DUNE-001/CPP-W1 (shared decode guard), T1 (libtiff TIFF-JPEG decode), T2 (SipiImage:write
nil), T3 (resource_limits over-rejection), T4 (cli help snapshot), doc/consistency.

| chunk | fix | status | commit(s) | summary |
|---|---|---|---|---|
| RUST-001 | info/knora bare-restrict | done | 444b6d1e | serve_info_json/serve_knora_json: guard in the IMAGE_MIMES branch before dims lookup — `permission==Restrict && access_kv_str("size").is_none() && access_kv_str("watermark").is_none()` → info.json 403, knora.json 404 (mirrors serve_image ~848 + /file ~713). Size-carrying restrict clamp path untouched. e2e `bare_restrict_info_json_is_forbidden` + `bare_restrict_knora_json_is_not_found` in security.rs (reuse existing `test_restrict_bare` prefix, no lua/BUILD change). //test/e2e:security PASSED + BOTH lint gates green (orchestrator re-verified security e2e). |
| DUNE-001 | shared decode-guard helper | done | 49b56138 | New src/ffi/cpp/decode_guard.{h,cpp}: moved `run_with_deadline` out of serve_image.cpp anon ns + added `acquire_full_lane_budget` (estimate→try_acquire→construct guard→FullLaneAcquisition) and `run_guarded_decode<T>` (moves guard into worker-thread closure; re-arms guard back to caller on success so serve path keeps charging through encode; on timeout guard stays with detached worker = NO refund). Both serve_image.cpp and image_handle.cpp sipi_image_new now call these with eng.decode_timeout_ms → Lua path gains DEV-7080 deadline + wedged_threads. Wired into //src/ffi:sipi_ffi (throttling reaches via //src/image). ARCH-MAP.md throttling + UBIQUITOUS_LANGUAGE.md "Decode memory budget" name the shared helper as the single acquire site. serve_image_test + seam_probe_test + //src/cli:sipi green (orchestrator re-verified). C++/docs only, no lint gates. Bench deferred (no per-pixel change). SIDE: no Lua-path deadline test added to seam_probe_test (hang fixtures live only in serve_image_test — out of scope). |
| T4 | cli help snapshot + admission help | done | 4eb082de | limits.rs `--admission-mode` help corrected basic→advanced (matches admission-control.md wording); folded in the T3 side finding. Regenerated cli__sipi-server-help.snap. ACTUAL snapshot drift was NOT adminuser/adminpasswd (already absent — S2-42 predates) but the S2-11 `--jwtkey` help text ("exactly 42 characters" → "≥32 bytes; shipped default rejected") + column reflow, plus the admission-mode wording. //test/e2e:cli 8/8 green uncached (orchestrator re-verified) + BOTH lint gates green. |
| T3 | resource_limits over-rejection | done | 4fd6dcd9 | NOT admission_mode/parse_size — a test-fixture isolation bug. Four `test/_test_data/config/*.lua` hardcode `cache_dir='./cache'` (relative to the shared sandbox cwd). `handler_exceeding_request_timeout_answers_408` + `trickling_body_is_cut_off_by_body_read_timeout` each start a SECOND concurrent sipi process against that same on-disk `./cache` while the shared `server()` uses it; the second process's SipiCache load treats the first's live files as orphans and DELETES them → shared server's next cache-hit 500s → tanks sustained_load_memory_growth + transform_pipeline_memory. Fix: both `start_env` calls get an isolated `tempfile::tempdir()` cache via `SIPI_CACHE_DIR` (mirrors admission_control.rs `start()`). Test-only; production admission/budget/parse_size verified correct + unrelated. resource_limits e2e green uncached (orchestrator re-verified). BOTH lint gates green. SIDE: `--admission-mode` help text in src/cli/rust/.../limits.rs still says "basic ... default" (stale post-S2-33) → folded into the CLI help fix. Latent landmine: all 4 configs share literal `./cache`. |
| DOCS | doc/consistency fixes | done | 9b0cf64d | (1) running.md: removed the `[tls_auth] admin_user`/`admin_password`→`admin.user`/`admin.password` TOML-mapping rows (S2-42 deleted the keys; TlsAuthSection deny_unknown_fields → they were a startup-parse trap). (2) testing-strategy.md: dropped admin.user/admin.password from the Lua-config-key inventory. (3) UBIQUITOUS_LANGUAGE.md Lua-route-handler examples: removed deleted `token.lua` (S2-22). (4) UBIQUITOUS_LANGUAGE.md Admission mode row: "default basic"→"default advanced" (S2-33). Grep-confirmed clean. Markdown only, no gates. SIDE: testing-strategy.md still says jwt_secret "42 chars" (S2-11 changed to ≥32 bytes) — not flagged by review, left out of scope. |
| T2 | scripting image_test SipiImage:write nil | done | 8a7be7b9 | Bindings were NOT broken. Root cause: S2-20 (e337543a) made image_handle.cpp `sipi_image_new` call `engine_context()` unconditionally (for budget + decode_timeout_ms), which THROWS unless `sipi_init` installed the context; the Rust `image_tests.rs` `test_vm()` harness never installs one (only C++ seam_probe_test.cpp was updated in e337543a), so `SipiImage.new` always returned `(false,msg)` and the string result had no `write` method. Fix: new non-throwing `engine_context_or_default()` (default EngineContext — no budget/cache, default deadline — when uninstalled) in engine_context.{h,cpp}; `sipi_image_new` uses it; IIIF serve path still hard-fails via `engine_context()` so production budget enforcement unchanged. Production-path fix, image_tests.rs untouched. image_test 7/7 + seam_probe_test + serve_image_test green (orchestrator re-verified). C++ only, no lint gates. SIDE: S2-20 verification originally missed //src/scripting/rust:image_test. |
| T1 | iiif_compliance tiff_jpeg_compression_input | BLOCKED | none | Brief premise disproven. Worker rebuilt server pinned to libtiff 4.7.1 (tiff.BUILD.bazel/codec wiring UNCHANGED 4.7.1→4.7.2 per `git show a8969bf6` — version-metadata-only diff) and the e2e fails IDENTICALLY on 4.7.1. NOT a libtiff-bump regression. Root cause = concurrency race in the JPEG-in-TIFF decode path: a single decode of the 7197×5441 TIFF-JPEG succeeds reliably (CLI/curl/single reqwest all clean); two concurrent decodes in the pool (SIPI_NTHREADS=8) → one aborts mid-stream with `hyper UnexpectedEof during chunk size line` — the exact test signature (the e2e harness runs tests in parallel against a shared server, so sibling decodes race it). S2-27 BodyAbort correctly SURFACES a pre-existing race (non-thread-safe global/static in libtiff/libjpeg JPEG-in-TIFF codec, or in SIPI dispatch). No source changed (diagnostic MODULE.bazel pin reverted). NEEDS MAINTAINER DECISION on fix approach — see Blockers. |

### Review-fix round result

6 of 7 review fixes landed, 1 blocked. Commits this round (in landing order):
- `444b6d1e` fix(server): RUST-001 — bare-restrict info.json→403 / knora.json→404.
- `49b56138` refactor(ffi): DUNE-001 — shared decode_guard helper; Lua path gains the deadline; no refund on timeout.
- `8a7be7b9` fix(ffi): T2 — engine_context_or_default() so Lua SipiImage.new decodes without an installed context.
- `4fd6dcd9` test(e2e): T3 — isolate concurrent resource_limits servers' cache dirs.
- `4eb082de` docs(cli): T4 — correct admission-mode help default + refresh help snapshot.
- `9b0cf64d` docs: DOCS — purge admin_*/token.lua/stale-admission-mode from docs.

BLOCKED: **T1** (`//test/e2e:iiif_compliance` `tiff_jpeg_compression_input`) — pre-existing
concurrency race in the JPEG-in-TIFF decode path (NOT a libtiff-4.7.2 regression; disproven by
rebuilding on 4.7.1). Needs a maintainer decision on the fix approach. See Blockers.

Final verification: BOTH lint gates green; `just bazel-test` = 76/77 (only iiif_compliance fails =
the T1 blocker); security / resource_limits / cli / image_test e2e all re-verified green individually.

## Cross-repo / maintainer notes (recorded, not acted on — out of scope here)

- Phase 5 dsp-api coordination: `sipi.init.lua:129-141` must default a size (e.g. `!128,128`) for the bare `{type="restrict"}` branch before Phase 13 rolls, else restricted-view images without settings return 403 after this PR. Maintainer's item.
- Phase 7 secret: config literal removal + startup jwt_secret check are implemented here; deployment-side Phase 0 secret rotation is the maintainer's and must be applied before the release rolls.
