# Execution journal — codec fuzzing + value-error migration

Plan: `01-feat-codec-fuzzing-and-value-errors-plan.md`
Branch: `feature/dev-7056-codec-fuzzing-and-value-errors`
Base: `63cca36b` (= origin/main, post-DEV-6418-wave-1)

State keys for recovery: this file + `git log 63cca36b..HEAD` in the target repo.
Plan-repo writes are working-tree only; the session commits them at ship time.

## Chunk table

> **History rewrite, round 3.** Chunk `3a`'s fix was folded into its introducing
> commit `6e0c8966` by `git commit --fixup` + `git rebase --autosquash`, which
> rewrote the two youngest commits: `6e0c8966` → **`8826293d`** and
> `0a1b4b1a` → **`ab1992e4`**. Nothing below `6e0c8966` moved. The rebase applied
> without conflicts and the resulting HEAD tree is byte-identical to the tree the
> chunk-`3a` worker verified green (`git diff 0a1b4b1a HEAD` = exactly the 12
> renamed-reference files). The branch needs a force-push; the session owns it.

> **History rewrite, round 5.** Two maintainer-authorized fold-ins landed in one
> `git commit --fixup` + `git rebase -i --autosquash` pass over
> `ae0a68f4..HEAD` (10 commits replayed, no conflicts). Chunk `5A` folded into
> `7b7d5480` and chunk `5B` into `fad85de0`; both commits were also **reworded**
> in the same pass, because each fold made its old subject inaccurate
> (`propose` → `adopt` now that the ADR ships accepted; "the post-commit
> script-error test" → "the post-commit abort tests" now that two tests are
> hardened). Every commit from `fad85de0` up therefore has a new SHA:
> `fad85de0`→**`6509ccc4`**, `856cc991`→**`6b03433c`**, `1c27fab7`→**`e1b0ee75`**,
> `913ef070`→**`6a3586b8`**, `e137daf1`→**`51669a96`**, `c7f35790`→**`db120a30`**,
> `00625ba1`→**`9bdc269d`**, `7b7d5480`→**`8afd88da`**. Nothing at or below
> `ae0a68f4` moved. Verified: `git diff <pre-rebase-head> HEAD` is **empty** —
> the replayed tree is byte-identical to the tree the two workers verified green.
> `just commit-lint` clean over the whole range; `just bazel-test` 72/72.
> The branch needs a force-push; the session owns it. Note this rewrite lands on
> commits already pushed to PR #797, so the force-push must use a lease pinned to
> the last-pushed SHA, not a bare `--force-with-lease`.

> **History rewrite, round 7.** The maintainer's reversal of ADR-0024
> Decision 6 was folded into the ADR's introducing commit `8afd88da` by
> `git commit --fixup` + `git rebase --autostash -i --autosquash`, so the ADR
> ships on `main` stating the settled PNG decision and never states the other
> one. Ten commits replayed, no conflicts. Every commit from `8afd88da` up has
> a new SHA: `8afd88da`→**`9de5d8c6`**, `c520e8e9`→**`66ebb80c`**,
> `f3fd0a42`→**`2a6dbae3`**, `98e64b52`→**`bc6cd3c2`**,
> `9ff74121`→**`0dc9cfd5`**, `09dfa8cf`→**`15545ff2`**,
> `950aa81c`→**`326467e3`**, `42917204`→**`30bf8a06`**,
> `ef423c93`→**`26bfff9c`**. Nothing at or below `9bdc269d` moved. Verified:
> `git diff ef423c93 HEAD` touches **only**
> `docs/adr/0024-value-based-image-errors.md` — the code tree is byte-identical
> to the tree round 6 verified green, so no re-verification of the replayed
> commits was needed. `--autostash` was used because the plan file lives in
> this same repo and is deliberately uncommitted; it was restored intact.
> The branch needs a force-push and the session owns it; the lease must be
> pinned to the last-pushed SHA (`ef423c93`), not a bare `--force-with-lease`.

> **History rewrite, round 8.** The maintainer's ruling on ADR-0024's TIFF
> rationale was folded into the ADR's introducing commit (`9de5d8c6` →
> **`8a512105`**) by `git commit --fixup` + `git rebase --autostash -i
> --autosquash`, so the ADR ships on `main` stating only the true root cause and
> never the folklore. Fourteen commits replayed, no conflicts. Every commit from
> `9de5d8c6` up has a new SHA: `9de5d8c6`→**`8a512105`**,
> `66ebb80c`→**`73380da1`**, `2a6dbae3`→**`7816a74a`**,
> `bc6cd3c2`→**`bc2f4f33`**, `0dc9cfd5`→**`ff6bf392`**,
> `15545ff2`→**`e53387eb`**, `326467e3`→**`e2928f2e`**,
> `30bf8a06`→**`924d769f`**, `26bfff9c`→**`09f8da54`**,
> `dde844cf`→**`3e1e5d25`**, `1adfdd8a`→**`17095e81`**,
> `4aae7a4e`→**`c6865465`**, `69900f99`→**`c43f70c6`**,
> `e2dc3143`→**`c6e10878`**. Nothing at or below `9bdc269d` moved. Verified:
> `git diff e2dc3143 HEAD` touches **only**
> `docs/adr/0024-value-based-image-errors.md` — the code tree is byte-identical
> to the tree round 7 verified green, so the replayed commits needed no
> re-verification and the on-disk benchmark "before" pair captured at `e2dc3143`
> stays valid. `--autostash` was used because the plan file lives in this same
> repo and is deliberately uncommitted; it was restored intact. The branch needs
> a force-push and the session owns it; the lease must be pinned to the
> last-pushed SHA (`e2dc3143`), not a bare `--force-with-lease`.

| id | status | commit(s) | summary |
|----|--------|-----------|---------|
| 14A | done | `acdeffcd` | **The fuzz harness earned its keep: the first real memory-safety bug it found on its own (DEV-7078), in code neither this branch nor the codecs own.** `Icc::parse` classified an embedded profile by reading its description with the two-call `cmsGetProfileInfoASCII` idiom. When the profile carries **no description tag** — the crash input's tag count is literally `0` — the first call returns `0`, `std::make_unique<char[]>(0)` hands back a zero-length buffer that nothing NUL-terminates, and the `strcmp` on the next line walks off it. ASan caught it as a `heap-buffer-overflow` READ of size 1 at `icc.cpp:74`, reached through `SipiIOJpeg::read`. **Every codec is a caller** (`SipiIOJpeg` ×2, `SipiIOJ2k` ×2, `SipiIOPng`, `SipiIOTiff`), so this was one crafted profile away from any format. The fix is the guard, not a bigger buffer: an absent description is `icc_unknown`, which is exactly what the existing fall-through arm already produced for every unrecognised description — so no classification outcome moved and the approval goldens are byte-identical. Standalone `fix(metadata):` because the bug is on `main`. The co-located `icc_parse_test` drives both arms; the worker corrected my brief on the second one — lcms2's built-in sRGB description is `"sRGB built-in"`, **not** the `"sRGB IEC61966-2.1"` literal `Icc::parse` matches against, so an untouched `cmsCreate_sRGBProfile()` classifies as `icc_unknown` even *before* the fix and would have proved nothing. The test writes the expected description explicitly to reach the `len > 0` branch. |
| 14F | done (code) / **blocked** (the test it asked for) | `62511be1` | **The question was answered by reading two functions, and the test that was supposed to confirm it found something much worse.** The geometry half is a non-event, and the evidence settles it: `convertToIcc` ends in `img.set_pixels(std::move(outbuf), nx, ny, nnc, new_bps)` with `nnc = cmsChannelsOf(sRGB) = 3`, and `set_pixels` is the single size-checked path that moves buffer and geometry together — so the `set_geometry(nx, ny, 3, 8)` that followed it in `SipiIOPng::write` was re-stating values already set. Redundant, not a desync; removed, with the reason stated in its place. Round 9 was right to preserve it verbatim rather than guess, and right that someone had to check. **The deliverable that did not land is the read-back assertion**, and that is the finding: reading a PNG SIPI just wrote fails outright when the source carried EXIF, because the writer stuffs a raw binary blob into a libpng *text* chunk and libpng cuts it at the first NUL. Three bytes of the EXIF survive — `II*` — and zero of the IPTC. Blocker written up in full under Side findings; the test cannot be added until the encoding is decided, because the fixture it would read is itself corrupt. |
| 14A3 | done | `e962e15a` | **The fuzzer found the bug; grep found the four worse copies of it.** After `14A` landed I read the rest of `icc.cpp` and `operator<<(std::ostream &, Icc &)` had the identical two-call idiom four times — description, manufacturer, model, copyright — each streaming `buf.get()` as a C string. **This one needs no crafted file at all**: manufacturer, model and copyright are *optional* ICC tags that ordinary profiles routinely omit, so the overrun is one absent tag away on a perfectly valid image. And it is reachable from two production surfaces, not just the codecs — `sipi query`'s `std::cout << img` (`cli_app.cpp:215`) and `sipi_image_tostring`, which is Lua's `tostring(img)` (`image_handle.cpp:642`), both via `SipiImage`'s own stream operator. The fuzz harness could never have found it: it drives `read_shape`/`read`, and nothing there streams an image. Four five-line blocks became four one-line calls through a file-local helper that treats an absent tag as no text; labels, alignment and `std::endl` unchanged, so output for a profile carrying all four tags is character-identical. The two `cmsSaveProfileToMem` sizing idioms in the copy constructor and `createFromProfile` were checked and cleared — they hand the buffer to `cmsOpenProfileFromMem` with an explicit length and never read it as a string. |
| 14E | done | `7471e781` | **The round-11 deferral closed the way the maintainer chose: implement, not signal.** The 16bps arm was an empty block with a comment saying support "was never really finished", so a watermark on a 16-bit image was skipped and the request returned success — a provenance control silently dropped. It generalized exactly as the ruling assumed, and the fact that makes it a five-line change rather than a design problem is that **the watermark file is always 8bps**: `read_watermark` rejects any other `TIFFTAG_BITSPERSAMPLE`, so both arms sample it with `bilinn`'s `byte` overload and normalize by 255, and only the image side moves to a `word*` view normalized by 65535. The loop is duplicated rather than templated, matching how every other 8/16 split in `image_processing` is written. **The test is the part worth reading**: `TEST(SipiImage, Watermark)` already applied a watermark to `CIELab16.tif` and asserted `has_value()` — which the no-op satisfied perfectly. That is how a silent no-op survives a test that names it. It now reads a second, unwatermarked copy and asserts the buffers differ, that the maximum per-sample difference is bounded well below full scale, and that the geometry is unchanged. |
| 14D | done | `d0c633d6` | **Q1 delivered, and the interesting part is the two things the ruling did not decide.** All four opens go through `TIFFOpenExt`/`TIFFClientOpenExt` with a per-call `TiffDiagnosticSink`; one `with_tiff_diagnostic` helper folds the accumulated text into a failure while preserving the error's code, errno and source location, so the fold is one call at each of ~15 return sites rather than fifteen hand-written concatenations. Concatenation in call order, per the ruling. **(a) Warnings are captured with errors, deliberately.** libtiff reports most malformed-directory conditions — unsorted tags, bad counts, unknown fields — at *warning* level, while the `TIFFGetField` those conditions poison simply returns 0. Capturing errors only would have produced an empty sink on exactly the read failures this feature exists to explain. The cost is that an unrelated benign warning can ride along on a later failure's message; that is the trade accepted. **(b) The `nullptr`-open path is left alone at three of the four sites**, and this is a contract, not an omission: `SipiIOTiff::read` returns `false` and `read_shape` returns a `SipiImgInfo` with `success == FAILURE` when `TIFFOpen` fails, which is how `SipiImage::read`'s fallback loop learns "not a TIFF, try the next handler". Turning that into an error would break reading a PNG that is named `.tif` — a case `ReadFallsBackWhenExtensionDoesNotMatchContent` pins. So "not a TIFF file, bad magic number" still reaches the log and not the `Result` on the read paths; only `write`'s two open failures are true errors, and those do carry it. Verified live rather than asserted: `sipi convert lena512.tif nonexistent_dir/out.tif` now reports `TIFFopen of "…" failed!: TIFFOpen: …: No such file or directory`. |
| 14B3 | done | `e3a78db1` | **The triage walkthrough taught that every nightly failure is a codec bug, and this round produced four failures of which one was.** `fuzzing.md` gained the decode-size budget (which its "Expected-reject vs finding" section otherwise contradicted the moment `b55e704c` landed) and a classification step ahead of "fix as its own commit": sanitizer-report-in-SIPI, sanitizer-report-entirely-inside-libFuzzer, out-of-memory, timeout. The one non-judgment rule in it is the corpus rule — **a hanging input never joins the seed corpus**, because the corpus is replayed by `bazel test` on every PR, so pinning one converts a nightly failure into a permanently hanging test suite. That is the trap the J2K finding below would have walked straight into under the old step 3, which said to commit every reproducer. |
| 14C | done | `935ebae8` | **The metadata-fatality ruling reaches the one site round 13 deliberately did not.** `Icc::createRGB` synthesizes a profile from a TIFF's white point and primaries rather than parsing an embedded blob, which is why the fatality sweep skipped it and why `error-model.md` had an explicit sentence exempting it. The maintainer closed that gap: the ground for the rule is *the file is corrupt*, and colour tags that cannot form a profile fail that test exactly as a malformed blob does. Both TIFF call sites propagate; the J2K one already did, which is the tell that the divergence was accident rather than design. The doc sentence that said "unaffected by this contract" is now the opposite, and the paragraph after it — the one that claims there is **no** reader-level exception — is finally true of every metadata path in every handler. Goldens byte-identical, as expected: the failure arm needs lcms2 to refuse a white-point/primaries triple, which no fixture produces. |
| 14B | done | `cfb1d7d8`, `b55e704c` | **Fuzz-config triage: four findings, four different answers, and only one of them was a bug (14A).** (a) **Kakadu UBSan** — `jp2.cpp:5058` reports `signed integer overflow` while assembling a palette entry from a `pclr` box. The brief expected this to be the documented multithreaded-decode noise; it is not — the existing `-fno-sanitize=function,shift,pointer-overflow` list does **not** contain `signed-integer-overflow`, and this site is reachable from `read_shape` on any malformed JP2, not from `kdu_thread_env`. Added as a fourth exclusion with the comment corrected on both counts (the wrap yields a wrong palette *sample value*, not an address). Also worth recording: it did **not** abort the leg — UBSan is recoverable in the fuzz config, so the j2k job failed on the timeout below, not on this. (b) **TIFF OOM** — `used: 2196Mb; limit: 2048Mb` from a **275-byte** file whose header claims 19789 × 65281 × 3. Not memory corruption and not a missing guard in the security sense: `validate_decode_dims` bounds each dimension (`1 << 17`) and the channel count (32) but **never their product**, so the claim is legal, and production bounds the resulting allocation at the FFI seam with the full-lane `MemoryBudgetGuard`. The harness has no budget, so the fix is the harness's: it now reads the geometry off the probe it already ran and skips the full decode above 512 MiB. Raising `-rss_limit_mb` was rejected — a crafted header can claim `131071²`, so no limit exists that this argument does not defeat. (c) **`parse_request` ASan** — not ours, and not even in SIPI code: the stack is `RereadOutputCorpus` → `Sha1ToString` → `basic_stringbuf::str()` → libc++'s `__init_internal_buffer[abi:nqe220107]`, a WRITE inside libFuzzer's own SHA1 formatting on a stack buffer whose shadow is partially-addressable. The saved reproducer is the **empty input** (`da39a3ee…` = sha1 of zero bytes), which is what a finding outside the target looks like. Surfaced for its own issue; untouched here. (d) **J2K timeout** — the one that is not config; see the blocker below. |
| 14A2 | done | `5aa6f54d` | **The reproducer is pinned as an input *class*, not as the fuzzer's artifact.** The crash file was 8323 bytes of mutated JPEG carrying a 6356-byte EXIF blob it never needed; what actually matters is 536 bytes of it — an ICC profile whose tag table is empty. So the fixture is regenerated by the committed generator (`_build_icc_profile_no_description` builds the 128-byte header field by field, appends a tag count of `0`, pads to the declared size) and wrapped in the **real** ICC-in-JPEG APP2 layout: `ICC_PROFILE\0` + sequence number + count. That last detail is what makes it a different fixture from `jpeg_icc_short_app2.jpg`, which pins the N1 length guard and never reaches `Icc::parse` at all — this one has to clear every upstream guard to be worth anything, and the test proves it does by asserting the decoded profile's *type*, which only the parse path can set. 1199 bytes, LFS, in `fuzz_seeds_jpeg` so every `just bazel-test` replays it. |
| 13E | done | `7d0c7258` | **J2K's fatal-metadata coverage, and the one of the three fixtures that had to be made by hand.** No generator emits a JP2 with a metadata box, so `j2k_exif_truncated.jp2` is `images/unit/ycbcr16.jpx` (730 bytes, the same base the oversized-dimensions fixture used) with a single top-level `uuid` box appended: 4-byte length, `uuid`, the 16-byte ASCII EXIF UUID `JpgTiffExif->JP2`, then the same 3-byte `II*` payload. **Appending after the codestream works** — Kakadu's box walk reaches it, so the before-`jp2c` fallback the brief allowed was not needed. Verified through `sipi query` before the test was written: exiv2 rejects it with "This does not look like a TIFF image" while the unmodified base still reads clean, which is what makes the appended box the only variable. The test lives in a new `j2k_metadata_regression_test.cpp` beside its dimension and palette siblings, wired into `formats_test`. Provenance is documented in the README's Regenerating section in prose, matching how the other two hand-made JP2 fixtures are recorded — there is no script to commit. |
| 13D | done | `df40e27c` | **TIFF's fatal-metadata coverage; ICC rather than IPTC, on evidence.** `tiff_icc_garbage.tif` is emitted by the existing `generate_malformed_images` `cc_binary`: a well-formed 8x8 grayscale TIFF whose `TIFFTAG_ICCPROFILE` holds 128 zero bytes with only the big-endian length field set, so the buffer is exactly the fixed ICC header size and the length lcms2 reads matches what it was given. **`SipiIOTiff::read()` applies no length or bounds check to that tag**, which is precisely why it is the right choice: the bytes go straight to `cmsOpenProfileFromMem`, which rejects them on the missing `acsp` signature at offset 36. Confirmed by CLI query before the test was written (`ICC-CMS error: not an ICC profile, invalid signature`). Failure surfaces from `read()`. **Worth knowing for the next fixture round:** `bazel run //test/unit/fixtures:generate_malformed_images -- <relative path>` writes into the runfiles sandbox, not the workspace — the generator has to be invoked at `./bazel-bin/...` with an absolute output path, and the README's documented command is the relative form. |
| 13C | done | `75456d94` | **The first of the three metadata-fatality fixtures, and the one that set the pattern the other two follow.** `jpeg_exif_truncated.jpg` is emitted by the existing `generate_jpeg_fixtures.py` generator: the standard small RGB base JPEG with an APP1 segment whose payload is `Exif\0\0` + `II*`. The 3-byte TIFF prefix is long enough to clear every length and bounds check upstream and short enough that `Exiv2::ExifParser::decode()` throws — which is the whole design constraint, since **every pre-existing `malformed/` fixture stops at a bounds guard and none of them had ever reached a parser.** Verified through `sipi query` before the test was written. The failure surfaces from `read()`, not `read_shape()` (EXIF extraction only happens on the full read). Each fixture also joins its format's fuzz seed corpus, where an input that a parser genuinely rejects is a better mutation start than another early-exit. |
| 13B | done | `1fe886e6` | **The held arm from 12M, released — and the fixture edit was byte-output-neutral exactly as predicted.** The maintainer authorised editing the LFS fixture, so `sample_with_icc.png`'s incidental `Raw profile type exif` zTXt record was removed with a byte-level chunk walker: 296786 → 296740 bytes, exactly the 46-byte length+type+data+CRC record, every surviving chunk byte-identical in the same order with its original CRC, **no re-encode of the IDAT stream**. `ImageEncodeBaseline.PngRoundTrip` passed untouched before *and* after the code change, which is the evidence that the chunk was carrying nothing the encoder ever read; the CHANGELOG row records it as an input-fixture edit rather than a re-approval, since no golden changed. PNG's EXIF arm then became fatal like its IPTC neighbour, and `error-model.md`'s "one stated exception" paragraph is gone — malformed ICC, IPTC and EXIF are now fatal in all four handlers with **no reader-level divergence left**. Side finding, pre-existing and untouched: the fixture carries a **duplicate `IEND` chunk**, which was there before the edit and is not this chunk's business. |
| 13A | done | `8e9609c1` | **The HTTP-400 ruling from 12L, delivered — and the request/file line drawn in the code, not just in the status table.** `ErrorCode::kInvalidRequestParameter` and `HttpStatusClass::kClientError` are the first non-`kInternalError` pair in the model; `status_for` maps the class to `SipiStatus::BadRequest`, which already existed. The policy is `kSkip` for Sentry: a client asking for a size we cannot serve is not a server fault and must not page anyone. **The interesting part is the guard split.** Each of the three resamplers held one condition testing both target and source dimensions; that single condition was the reason a 400 could not be delivered honestly, because half of it fires on the *file*. It is now two: a degenerate **target** is the client's parameter (400, no report), a degenerate **source** stays `kMalformedInput` (500), because a one-sample-tall image in the repository is not the caller's doing. No other code was reclassified — every corrupt-file rejection still renders 500. The e2e assertion (`/full/1,1/` → 400) is in `iiif_compliance.rs` next to the other size-status rows; nothing pinned the old 200. |
| 12M | done (with one arm held; **arm released in `1fe886e6`, coverage gap closed by `75456d94` / `df40e27c` / `7d0c7258`**) | `ac331707`, `5a8914e1` | **The maintainer's second ruling — "we are a repository, no corruption is allowed" — and the fixture that proved it bites.** Malformed EXIF/IPTC/ICC was logged and stepped over in JPEG, J2K and TIFF: the image decoded, the metadata vanished, and the caller was told it worked. Now a parse failure fails the read with `kMetadataParseFailed`, and the four handlers agree. `SipiIOJpeg::parse_photoshop` had to change signature (`void` → `Result<bool>`) so a bad APP13 resource block reaches the caller instead of `return`-ing out of the scan and leaving the image with whatever parsed first. Each new error path releases what its neighbours release — libjpeg's decompress struct and ICC buffer explicitly, the TIFF handle and the Kakadu resources through their existing RAII guards. **The finding: applying the ruling turned one of our own approval fixtures into an unreadable file.** `test/_test_data/images/unit/sample_with_icc.png` carries an incidental "Raw profile type exif" chunk that decompresses to a 3-byte `II*` — a TIFF magic prefix with no IFD behind it, which `Exif::parse` genuinely rejects (not a bounds-guard short-circuit; verified empirically). It is the input to the `ImageEncodeBaseline.PngRoundTrip` golden, so the PNG EXIF arm turned an approval test red. **The PNG EXIF arm was therefore held back and the rest landed green**, because editing or re-approving a Git-LFS fixture is the maintainer's call under `test/approval/CHANGELOG.approval.md`, and a red commit is not an option. `error-model.md` states the contract *and* names PNG's tolerated EXIF chunk as that reader's limitation, so the inconsistency is written down rather than lurking. **Coverage is thinner than the ruling asked for**: JPEG, J2K and TIFF have no fixture that drives a metadata parser to actually fail — every `malformed/` fixture pins a *bounds guard*, which stops parsing early without producing a parse error — and fabricating binary fixtures was out of scope. `Icc::createRGB` (profile *synthesis* from colour tags, not a parse of an embedded blob) was left log-and-continue, deliberately and reported. `just bazel-test` **73/73**; `just bazel-test-e2e` 30/30. |
| 12L | done (**status mapping delivered in `8e9609c1`**) | `da0914f7` | **The round-11 blocker, resolved by the maintainer as option (1): reject.** A IIIF request for a one-pixel size (`1,1`, `1,` or `,1`) was answered with **the image at its original size and HTTP 200** — all three resamplers treated a `<= 1` target or source axis as "nothing to do", logged a warning, and returned success with the image untouched. They now return `kMalformedInput` naming the source and requested dimensions. The pre-existing test `ScaleToOnePixelDoesNotCrash` pinned the old silent success; it is now `ScaleToOnePixelIsRejectedWithoutCrashing` and pins **both** properties — the `bilinn` segfault it was written for (`ba84f8b2`) still cannot happen, because the interpolator is still never entered with a degenerate target, and the answer is now honest. Two sibling tests cover `1,` and `,1`. The `log_warn` went with the change: an error value carrying the same facts to the seam plus a log line stating them again is the duplication this migration removes. **Two behavior-change surfaces wider than the literal request, both worth the maintainer's eye:** (1) `SipiSize`'s proportional math (`PIXELS_X`/`PIXELS_Y`/`PERCENTS`/`MAXDIM`) can compute a `1` on the *unpinned* axis for an extreme-aspect-ratio source — e.g. `2,` on a very wide, thin image — so such requests are now rejected too; no fixture exercises it. (2) The guard also tests the **source** dimensions, so a legitimately 1-pixel-tall source image can no longer be scaled at all. **One thing this chunk deliberately did not do:** deliver the HTTP **400** the ruling names. Today `status_for` maps every `ErrorCode` to `kInternalError` — `HttpStatusClass` has exactly one variant — so a 400 requires either a new status class plus a new code for this case, or remapping `kMalformedInput` globally, and the latter would silently reclassify every corrupt-*file* decode rejection (oversized dimensions, undersized palette, bad TIFF tags) from 500 to 400, which reads as blaming the client for the repository's own content. The rejection is in; the status mapping is a one-row policy decision left to the maintainer. |
| 12K | done | `21cf6913`, `917eea63` | **The documentation sweep found the migration's last two lies, and one of them was written *by* the sweep.** Code first: `Xmp`'s constructor stores its bytes verbatim and never parses them, so the `try { … } catch (SipiError &)` the J2K and TIFF readers wrapped around `std::make_shared<Xmp>(…)` could never fire and its `log_err` could never print — deleted in both (JPEG and PNG never had it). Docs: `error-model.md` called itself "the living catalog kept current as the migration lands" while naming a deleted class (`SipiImageClientAbortError`), saying `SipiImageError` would be narrowed "**once** no fallible call site throws it" (it has been), listing `InfoError` as pending replacement (deleted), and citing a `MetricHint::kMemoryAllocFailure` **that does not exist in the enum**. `fuzzing.md` still said a codec rejects malformed input by *throwing*. `CONVENTIONS.md`'s one fallible-operation row now splits by layer, because the image layer and `iiifparser` genuinely answer differently. ADR-0006 (`proposed`, but actively cited) proposed per-operation error types and the *removal* of `SipiImgInfo::success`; ADR-0024 settled a single shared `SipiValueError` and the field survives — corrected in place under the status-dependent ruling, since a proposed-but-live ADR contradicting shipped code misleads exactly the reader who goes looking for the rationale. ADR-0024 itself deliberately untouched. **The lie the sweep wrote:** its first draft claimed "`kdu_exception` never reaches a seam — `SipiIOJ2k.cpp` converts it to a `Result` internally". False, and it contradicted the fuzz-harness comment committed an hour earlier: `SipiIOJ2k::read` guards the source open, `access_codestream` and `codestream.create`, but **not** `access_siz`, `apply_input_restrictions`, `get_dims` or `access_colour`, so a `kdu_exception` from those escapes to the FFI/CLI catch-all wall. Caught by cross-checking two documents against each other rather than each against the code — **which is the cheap check: when two docs disagree, at least one is wrong, and the disagreement is free to find.** |
| 12J | done | `aec2c8a0`, `9e3c8f69` | **Two "narrow the contract" items, and both turned out to be audits rather than deletions — one deletion of two, which is the point.** (1) `serve_image.cpp`'s `if (info.success == SipiImgInfo::FAILURE)` guard, flagged as unreachable in `11K` and deliberately left alone then, was proved unreachable path-by-path through `read_shape` and deleted; the unwrap site now states the invariant instead of testing it. `SipiImgInfo::FAILURE` itself stays — the handlers still use it to mean "this file is not mine". (2) The codec fuzz harness's catch blocks were the plan's "narrow to the post-migration contract" item, and **nothing could be narrowed**: both arms still fire. What was wrong was the *comment* justifying them, which named the `Iptc`/`Exif`/`Xmp` metadata constructors as throwers — EXIF, IPTC and ICC became `Result` factories in round 11, and `Xmp`'s constructor stores its bytes verbatim without ever parsing them, so none of the four can throw — while under-stating what actually can: `kdu_exception` from the Kakadu accessors the J2K decode path does not individually guard (hence the bare `catch (...)`, since it is not a `std::exception`), plus the allocation guards. **Deleting a reachable catch arm in a fuzz harness turns a real crash into a silent pass**, which is the one failure mode a fuzzer must not have, so "narrow" was correctly read as "verify each arm, keep what fires, fix the justification". Also replaced the harness's two `static_cast<void>(...)` discards with `if (const auto r = …; !r)`: the harness genuinely does not care about the value, but a `(void)`-cast is exactly what `CONVENTIONS.md` says defeats `[[nodiscard]]`, and saying "a rejection is a valid outcome" in code costs nothing. `just bazel-test` **73/73**. |
| 12I | done | `450c312c` | **Two stale contracts in `src/image/cpp/`, and a ruling that goes the opposite way to the round brief's guess.** (1) `getPixel`/`setPixel` threw **bare `int`s** (`throw((int)1)` … `throw((int)6)`) — round 11's side finding. The brief suggested converting them to `Result` on the grounds that pixel-coordinate misuse is fallible input. The evidence says otherwise, and it is the same evidence that settled `11I`: **neither accessor has a production caller.** Every call is in a `*_test.cpp`, with coordinates the test author writes as loop indices — no decoded file, no request. That is the invariant class, so they keep throwing; the defect was never the mechanism, it was throwing a type **nothing can catch deliberately** (`catch (const SipiImageError &)` misses an `int`; only a catch-all sees it, and reports nothing). Now `SipiImageError` naming argument, value and bound, with the contract stated in both doc comments. **The rule, restated because it has now decided four cases: "does production call this at all?" is the cheapest test of input-class vs invariant-class, and it beats intuition about what the arguments *look* like.** (2) `SipiImageError`'s own docstring still taught a **subclassing pattern** — derive a subclass, place its `catch` before the base's, and the seam's type-ordered chain gives it a different policy. Every part of that is now false: policy is data (`policy_for(ErrorCode)`), the one subclass was deleted in `12E`, and fallible operations do not throw at all. A docstring instructing the reader to extend a mechanism that no longer exists is worse than none, since it is the first thing anyone adding an error will read. Rewritten to state the invariant-only role and point at `error-model.md` + ADR-0024. Formatting note: the new messages pushed nine lines past the 120-column limit in a file that had **zero** before, caught by `awk 'length > 120'` and re-wrapped — worth adding to the per-chunk checks, since there is no CI gate for C++ formatting. `just bazel-test` **73/73**. |
| 12H | done | `2b5500a3` | **The last thrower in `//src/image_processing`, and the one conversion that required changing an API's shape.** `operator-=` rewrote its left operand into the rescaled signed difference of two images — a mutating, fallible operation wearing compound-assignment syntax. An operator cannot return a `Result`, so the conversion *is* the rename: `Sipi::processing::subtract(SipiImage &lhs, const SipiImage &rhs) -> Result<void>`, sitting next to `compare` and `maxPixelDelta` where it belongs. Its doc comment now says why it is not an operator (in-place mutation is not the value semantics `-=` implies, and the operation is fallible), so the question does not reopen. Codes: incompatible channel count / bit depth / photometric interpretation → `kMalformedInput`; unsupported bit depth → `kUnsupportedFormat`. **The third throw was the interesting one:** `throw SipiImageError(scaled.error().raw_message(), scaled.error().errnum(), scaled.error().location())` — a converted `scale`'s error being unpacked field by field and re-thrown, the single clearest instance of the round-trip this migration deletes. It is now `return std::unexpected(scaled.error())`: same code, same message, same source location, no exception. The `compare` verb checks it exactly as it checks its reads and writes. **`grep -n throw src/image_processing/cpp/compose.cpp` now shows only `checked_buf_size_or_throw`** — the allocation-overflow invariant — so no operation in the package throws. One preserved wart, deliberately: the verb logs failures as `"sipi: unhandled exception: %s"`, which is now literally untrue, but it is the CLI's existing user-visible text and this round changes mechanism, not output. `just bazel-test` **73/73**; `just commit-lint` clean. |
| 12G | done | `4bcc5d68` | **Three of `compose.cpp`'s throwing functions were converted by deleting them.** `operator-`, `operator+=` and `operator+` over `SipiImage` have **zero callers** — verified across `src/`, `test/`, the Lua/FFI surface, the benchmarks and the docs, remembering that operator uses appear as `a + b`, never as the operator's name. 144 lines of pixel arithmetic and four throwing rejection paths that no input could reach. This is the fourth time this migration has met an abstraction with no caller (`Xmp`, `Icc(PredefinedProfiles)`, the dimensioned `SipiImage` constructor, now these) and the answer has been the same each time; the difference here is that the migration would otherwise have *paid to convert them*. **The reusable order-of-operations:** before converting a throwing function, check whether anything calls it — deletion is cheaper than conversion and strictly better than both. Only `operator-=` survives, with exactly one caller (the `compare` verb). `ARCH-MAP.md`'s `image_processing` entity list updated in the same commit, since a map naming deleted symbols is worse than no map. `just bazel-test` **73/73**. |
| 12F | done | `07be35d8` | **The second thing the round-12 grep found, and the one that shows why "the package is throw-free" claims need a grep, not a memory.** `read_watermark` — declared in `//src/image_processing`'s `processing.h`, defined in `//src/format_handlers`'s `SipiIOTiff.cpp` via the deliberate link-time asymmetry — threw all six of its rejections, and its only caller `processing::add_watermark` **already returned `Result<void>`**. So a malformed watermark file threw straight *through* a value-returning function: the worst of both mechanisms, and invisible to anyone reading either function alone. Now `Result<std::vector<unsigned char>>`, codes following the TIFF handler's own precedent (`kMalformedInput` for a tag that will not read off a file that opened, `kUnsupportedFormat` for bps ≠ 8 and non-contiguous planar config, `kDecodeFailed` for a failed scanline), messages byte-identical. The `TIFF*` and buffer were already RAII (`unique_ptr` + `std::vector`), so no cleanup moved. **Two deliberate non-changes:** the `wmbuf.resize` allocation failure keeps throwing (`std::bad_alloc` class, ADR-0024 Decision 7, consistent with `memTiffOpen` and `checked_buf_size_or_throw` in the same file); and `add_watermark`'s `if (wm->empty())` check stays, because the open-failure path still returns **success with an empty buffer** — a genuinely reachable "opened nothing, failed nothing" case, so the check is not the stale defensive line it looks like. `just bazel-test` **73/73**; `just commit-lint` clean. |
| 12E | done | `374a68b1` | **The facade's last throwing member, and the deletion that shows what the whole migration was for.** `SipiImage::write` (both overloads) → `[[nodiscard]] Result<void>`, ~50 call sites, and with it die **both** of the constructs that existed only to serve the throwing contract: `throw_from_value_error` (its last caller gone) and **`SipiImageClientAbortError`** — a whole exception *class* whose only job was to carry one error code across a `throw`. One thrower, one catcher, and a subclass in the header so a `catch` could dispatch on the type. With the code already on the value, the HTTP seam reads `err.code() == ErrorCode::kClientAbort` and the class has no reason to exist. That is the clearest single illustration in this migration of type-as-discriminant being replaced by data-as-discriminant, which is ADR-0024 Decision 2 in one deletion. Behavior held exactly: an abort still unlinks the partial cache file, logs at info, increments `client_disconnected_total`, and is never captured to Sentry; every other write failure still captures with the same `ImageContext` and logs the same line. The seam used the `Result<void> r;`-above-the-switch shape from `11G` for the four-arm format switch — the fourth time that shape has absorbed a switch unchanged. `UBIQUITOUS_LANGUAGE.md`'s **Client abort** entry moved from naming the class to naming `ErrorCode::kClientAbort` and its `policy_for` row; ADR-0024 was deliberately **not** touched, because its "before" table records the decision as taken and is legitimately historical. `io.at(ftype)`'s `std::out_of_range` stays: callers pass validated format strings, so that one is a programming error. `just bazel-test` **73/73**; `just commit-lint` clean. |
| 12B+12D | done | `6424da18` | **The facade's read contract, its ~150 call sites, and the correction that mattered more than the conversion.** `SipiImage::read` + both `readSource` overloads → `[[nodiscard]] Result<void>`, one commit, every caller touched once: 7 production files and ~29 test/benchmark files. The dependency order round 11 flagged held — `readSource` **is** `read` plus the Essentials tripwire, so they were never separable. Seam conventions applied unchanged from round 11 (`report_value_error`+`status_for` at HTTP, `client_message()` at Lua, `diagnostic_message()` at the CLI, `has_value()` in tests), which is the payoff for settling them on small functions first. **The correction: the first pass deleted three `catch` arms that are still reachable.** `read` stopped throwing *decode* failures, but it did not stop throwing — `checked_buf_size_or_throw`, `memTiffOpen`'s raw `malloc` and Kakadu's `kdu_exception` remain exception-based by ADR-0024's design, so `serve_image.cpp`'s `SipiImageError` arm (Sentry + `ImageContext`) and the `std::exception` arms in `verify.cpp` and `convert_service_file.cpp` still fire. Deleting them would have downgraded a buffer-overflow rejection from a reported 500 with image context to a bare guard-caught 500, and stripped `verify`'s `failed to decode <path>` line. All three restored verbatim, each now carrying a comment naming what it covers. **The rule this yields:** "the callee no longer throws *this*" is not "the callee no longer throws" — before deleting a catch arm, enumerate what still throws *under* the call, not just what the callee's own body does. `just bazel-test` **73/73**; `just commit-lint` clean. |
| 12C | done | `a333c496` | **A prerequisite the plan did not list, found by grepping instead of trusting the journal.** Before converting the facade, `grep -rn "throw SipiImageError" src` was run — and `validate_decode_dims` (`src/image/cpp/SipiIO.h`) came back. It validates width, height, channel count and bits-per-sample **read from a file header**, which is decoded-file input by the round-11 rule ("input-class if any operand traces to a decoded file"), yet it threw. Left alone, it would have been a fallible path throwing *through* a `Result`-returning facade — the exact shape the migration exists to delete, and the round-11 note that "`//src/image_processing` is throw-free" had quietly created the impression the decode path was too. It is not the only one: the same grep also found `SipiIOTiff.cpp`'s `read_watermark` and the whole of `image_processing/cpp/compose.cpp` still throwing (both recorded below as remaining Phase 8 work). Conversion: `Result<void>`, `kMalformedInput` for the dimension and channel caps, `kUnsupportedFormat` for a bit depth the codecs do not implement, all three message strings byte-identical. Four handler call sites, each propagating with its neighbours' cleanup — JPEG needed `jpeg_destroy_decompress`, J2K and TIFF are covered by their RAII teardowns (`kdu_teardown`, `tif_guard`), PNG matched its siblings. **Commit ordering, deliberately:** this landed *before* the facade commit, and at that point `img.read` still threw for an oversized header (the facade translated the handler's error), so the existing `EXPECT_THROW` regression test kept passing untouched — the test moved to asserting the value in the facade commit, where that became true. Isolated verification of this commit was reasoned rather than executed: `git stash` is blocked in this environment and a fresh `git worktree` means a cold build of the whole native dep graph, which the round-11 notes rule out for two numbers. `just bazel-test` **73/73** on the combined tree. |
| 12A | done | `5a7097f3` | **The round-11 side finding, taken under the standing main-owned-bug pattern, and it was worse than the finding said.** `SipiIOPng::write` freed only the *data* attached to `png_ptr`/`info_ptr` on its error returns (`png_free_data(..., PNG_FREE_ALL, -1)`) and leaked the two structs themselves; only the setjmp landing block and the success tail destroyed them. One file-local `PngWriteStructGuard` — holding **references** to the two locals, not copies, so it sees the `info_ptr` assigned after it is armed — now calls `png_destroy_write_struct` exactly once on every exit. It is constructed the instant `png_create_write_struct` succeeds and **before** the `setjmp`, which is what makes the landing block correct: the block returns normally out of the function, so the destructor runs there too and its explicit destroy call could go. Every manual `png_free_data`/`png_destroy_write_struct` in the function is now gone, including the success path's — `png_destroy_write_struct` frees the info struct and its attached data, so those calls were redundant once the guard existed. **The finding under-counted the arms:** the journal listed four leaking returns, the worker's grep found a fifth (the `icc_LAB` `convertToIcc` failure inside the ICC block). That is the argument for the guard rather than five hand-patched arms — "make every arm correct by construction" does not need the enumeration to be complete, and the enumeration was not. Error strings, `ErrorCode`s and return values byte-identical; approval goldens unchanged. `just bazel-test` **73/73**; `just commit-lint` clean. |
| 11K | done | `8c2d230a` | **Phase 8's head item turns out to decompose the same way Phase 6 did — by member — and this is the proof.** `SipiImage::read_shape` → `[[nodiscard]] Result<SipiImgInfo>` with all three call sites. It was chosen precisely because it is tiny: **3 call sites against ~144 for `read`/`readSource`/`write` combined**, all three on the FFI seam, no test touches it. So it settles the facade's shape at almost no risk, exactly as `crop` settled the `image_processing` shape in round 10. **What the conversion actually buys is visible here for the first time:** `read_shape`'s dispatch lambda and fallback loop each did `if (!result) { throw_from_value_error(result.error()); }` — flattening a handler's `Result` into an exception so the facade could keep its throwing promise. Both are now plain `return std::unexpected(result.error());`. That is the round-trip this whole migration exists to delete, and it disappears once per facade member. Codes: `kUnsupportedFormat` for an unrecognised mimetype, `kShapeProbeFailed` for the terminal no-handler case (which keeps the "tried: …" handler list an earlier chunk added). Fallback semantics preserved byte-for-byte — `FAILURE` continues the search, an error value stops it as the authoritative cause. **The deletion worth noting: `Sipi::InfoError`.** It was `enum InfoError { INFO_ERROR };` in `SipiImage.h`, **thrown nowhere in the tree**, caught in exactly one place — so `sipi_image_file_dims`'s first catch arm and its `"Couldn't get dimensions"` string were dead. Enum, arm and message all went. A type nobody throws and nobody else names is worse than no type; the grep that proves it costs seconds and every facade chunk should run it. Also found and **left alone**: `serve_image.cpp`'s `if (info.success == SipiImgInfo::FAILURE)` guard is now unreachable, because the terminal check converts that case to an error before returning — reported, not deleted, since a stale defensive line is cheaper than a wrong deletion mid-migration. `sipi_image_dims` has no error callback in its C ABI, so it follows the `sipi_image_topleft` precedent from `11H`. `just bazel-test` **73/73**; `just commit-lint` clean. |
| 11J | done | `8a3cefde` | **Two Phase 8 items, and the interesting one is that a documentation file was asserting something false about the code.** The `CONVENTIONS.md` rule went into the existing "Error Handling Pattern" section rather than a new one, and is written to be true independently of how far the migration has got — it constrains *new* fallible operations, so it does not rot when the facade converts. The clang-tidy citation is load-bearing rather than decorative: `bugprone-unused-return-value` watches `std::expected` by default **and flags the `(void)`-cast that silences `[[nodiscard]]`**, which is the exact hole that let `(void)Sipi::processing::crop(...)` serve a full-size image in place of a requested crop for however long it had been there. **The correction:** `error-model.md` said the CLI exit code "is derived from `HttpStatusClass` (client-class failures and internal failures map to distinct non-zero codes)". All five verbs — `cli_app.cpp`, `convert_access_file.cpp`, `convert_service_file.cpp`, `verify.cpp`, `health.cpp` — return only `EXIT_SUCCESS`/`EXIT_FAILURE`, checked one by one, and each command header documents exactly those two. The doc was describing an intention as fact, which is worse than describing nothing, because chunk `11D` had to work out the real convention from the code while a doc sat there confidently contradicting it. Now marked unimplemented. **The glossary was a deliberate no-op**: `Result`/`SipiValueError`/`ErrorCode`/`client_message`/`diagnostic_message` are mechanism vocabulary and ADR-0024 already names `error-model.md` as their catalogue; padding `UBIQUITOUS_LANGUAGE.md` with them would duplicate a living document into a stale one. `just bazel-test` **73/73** (all cached, no source touched); `just commit-lint` clean. |
| 11I | done | `a9c25a1d` | **Phase 6 closes the way Phase 7 opened: by refusing a conversion, with evidence.** The plan item said "`Image` construction → `Result`-returning factories per the ADR"; the answer is that ADR-0024 Decision 7 **excludes** it, and one fact settles it — **the dimensioned constructor `SipiImage(nx, ny, nc, bps, photo)` has zero production callers.** Every construction in the tree is in a `*_test.cpp` (12 files), and several pin its rejections with `EXPECT_THROW`. Its operands are geometry the caller states outright, never a file's decoded shape, so the "trace the operands" rule that convicted `removeChannel`'s guards in `11C` acquits these: no input path can reach them, they are programming errors, they stay throws. The copy constructor is the `Icc`-copy-constructor case verbatim — it re-derives a buffer size for an image already validated at its own construction, so it fires only on corrupt internal state (`std::bad_alloc`'s class), and a constructor cannot return a value regardless. Converting would have produced a factory whose error arm **no production caller can produce** — the abstraction-without-a-caller `CLAUDE.md` forbids and this migration has now rejected three times (`Xmp`, `Icc(PredefinedProfiles)`, this) — and would have **weakened** the `EXPECT_THROW` tests, which is forbidden outright. Landed as `docs(image)`: the two constructors' header comments now state the contract, so the question does not reopen. **The reusable rule, now proven on both sides:** a throw is input-class if any operand traces to a decoded file or a request; it is invariant-class if only a caller passing wrong arguments reaches it — and "does production call this at all?" is the cheapest way to find out. One thing noticed and deliberately not acted on: `getPixel`/`setPixel` throw **raw `int`s**, which is neither a `SipiImageError` nor a value; out of this chunk's scope, recorded in Side findings. `just bazel-test` **73/73**; `just commit-lint` clean; file mode on the `100755` header preserved. |
| 11H | done | `c281379a` | **The 37-site `rotate` — the chunk four rounds of notes treated as the reason the finale had to be atomic — landed in one worker pass, and `//src/image_processing` is now throw-free.** `rotate` + `set_topleft` → `[[nodiscard]] Result<void>` with roughly fifty call sites, paired because `set_topleft` *is* seven `rotate` calls in a switch. `rotate` carried the **same dangling `else { return false; // clean up and throw exception }`** as the crop overloads, and it went the same way under the maintainer's standing ruling. **The 37 sites turn out to be three copies of one thing:** `cli_app.cpp`, `convert_service_file.cpp` and `convert_access_file.cpp` each carry their own seven-arm EXIF-orientation switch — 21 of the 37 — all of which discarded the result. The `Result<void> r;`-above-the-switch shape from `11G` absorbed all three unchanged, which is exactly why settling the seam conventions on the small functions first was worth the rounds it took. The two access-file helpers (`apply_orientation_topleft`, `apply_rotate_mirror`) moved to `Result` returns rather than swallowing — a wrapper keeping a `void` signature over a converted operator **is** the forbidden adapter, so a helper's signature travels with its operator. **One asymmetry worth knowing:** `sipi_image_rotate` has an error callback in its C ABI and had been ignoring it (`(void)err; (void)err_ctx;`); `sipi_image_topleft` has none, so its failure reports as the generic non-zero status an escaping exception used to produce, with a comment stating that contract. Changing an `extern "C"` signature was out of scope and remains so. Finally this drained `geometry.cpp`'s `checked_buf_size_or_throw`: `crop`'s four remaining calls moved to the checked form (its body only — no call site touched twice), the helper and the `SipiImageError` include went, and **`grep -c throw src/image_processing/cpp/geometry.cpp` is now 0**. With `color.cpp` cleared in `11G`, no operator in the package throws. `just bazel-test` **73/73**; approval goldens byte-identical; `just commit-lint` clean. |
| 11G | done | `872f9f39` | **Seventeen sites, and the first translation unit in the package to become throw-free.** `convertToIcc` and `toBitonal` → `[[nodiscard]] Result<void>`, converted together for **two** independent reasons — `toBitonal` reaches grayscale by calling `convertToIcc`, *and* the HTTP seam dispatches both from one `switch` on the requested quality. Either alone would have forced the pair. Codes: the unsupported channel count and the unsupported `new_bps` are decoded-file state → `kMalformedInput`, consistent with the rest of the file; a null from `cmsCreateTransform` → **`kMetadataParseFailed`**, which is not an obvious choice until you notice `src/metadata/cpp/icc.cpp` already types every lcms2 refusal (`cmsOpenProfileFromMem`, `cmsCreateRGBProfileTHR`) that way — the failure is the library declining two already-validated profiles, not bad pixels. `Icc::iccFormatter` stays throwing, per the earlier ruling that profile *emission* is exempt under the ICC-determinism invariant, and the seams' `try`/`catch` blocks stay in place for it and for `std::bad_alloc`. **The `Result<void> r;`-above-the-switch shape** used at the HTTP seam is the reusable bit: a three-arm switch cannot `return` per arm without triplicating the report block, so each arm assigns and one block after handles the failure. `apply_icc` in the access-file verb became `[[nodiscard]] Result<void>` rather than swallowing — a helper's signature moving with the operator it wraps is in scope for the unit, since leaving it `void` *is* the adapter. Cleanup: these were the **last two users** of `color.cpp`'s `checked_buf_size_or_throw`, so the helper, its now-empty anonymous namespace, and the `image/SipiImageError.h` include all went — **no operator in `color.cpp` throws any more**, verified by grep. New error returns released what their siblings release: `jpeg_destroy_compress` at the JPEG CIELAB arm, `png_free_data` at both PNG arms. `just bazel-test` **73/73**; approval goldens byte-identical; `just commit-lint` clean. |
| 11F | done | `ebac0ef9` | **The HTTP seam gets its convention, and it needed no new machinery at all.** `add_watermark` → `[[nodiscard]] Result<void>` with all seven call sites: `serve_image.cpp`, the Lua handle, two CLI verbs, three test assertions. The function has exactly one failure (an unreadable watermark file), typed `kDecodeFailed` rather than `kMalformedInput` — the watermark is a TIFF that failed to *decode*, not a bad request parameter; the policy is identical today, so this is a semantic-fit choice that pays off only when `policy_for` grows arms. **The HTTP-seam shape, to copy verbatim:** `serve_image.cpp` already had both halves (added when value errors first crossed the seam) and neither was being used from `build_image_response` yet — `status_for(err)` maps through `policy_for(err.code()).http_status_class`, and `report_value_error(...)` honours the code's `SentryPolicy`, skipping the callback for a `kSkip` code. So the block is: build the same `ImageContext` the catch arm built (`input_file`, `file_size_bytes`, `populate_from_image`), `report_value_error(req.report_error, req.report_ctx, r.error(), "convert", sentry_ctx)`, `return std::unexpected(status_for(r.error()));`. **Never switch on a raw `ErrorCode` here** — `serve_image.h` says so explicitly, and `status_for` exists to make that unnecessary. The `"convert"` phase label and the `PhaseTimer` are unchanged, so the Sentry report a failure produces is identical. The surrounding `try`/`catch` stays for `std::bad_alloc`. **The Lua seam's `try`/`catch` went entirely** — the call was the only statement inside it, so the block had nothing left to catch. The three `EXPECT_NO_THROW`s became `EXPECT_TRUE(... .has_value())`: after this change the old form would have passed on an image that was never watermarked, which is the exact failure mode `EXPECT_NO_THROW` stops detecting once a function returns its errors. One thing deliberately *not* touched, now a side finding: `add_watermark`'s `bps == 16` arm is an empty documented no-op, so watermarking a 16-bit image silently succeeds without watermarking. `just bazel-test` **73/73**; `just commit-lint` clean. |
| 11E | done | `de65e5af` | **The largest unit so far (15 sites), and it found a live IIIF defect that it then deliberately did not fix.** `scale`, `scaleMedium` and `scaleFast` → `[[nodiscard]] Result<void>`. **They had to convert together**: every format handler dispatches all three from a *single* `switch` on the scaling quality, so one-at-a-time would have rewritten the same statement three times — the first case where "smallest first" would have actively caused the double-touch the ruling forbids. Unsupported bits/sample and pixel-buffer overflow became `kMalformedInput`. Two new seam precedents: **`operator-=` / `operator+=` in `compose.cpp` return `SipiImage &` and cannot carry a result**, so they convert the value back into a `SipiImageError` inline (`raw_message`, `errnum`, `location` — the same construction `SipiImage.cpp`'s anonymous-namespace `throw_from_value_error` uses; it is not reachable from `compose.cpp` and a third copy of a two-line helper is not worth an abstraction). That also stops those operators silently differencing a mis-sized image, which they did whenever the operands' dimensions disagreed. **The finding, and the reason this chunk needed a second pass:** the degenerate-dimensions guard (`nnx <= 1 || nny <= 1 || …`) returns `false` that *every* caller discards, so `.../full/1,1/0/default.jpg` is served at the image's **original size with HTTP 200** — reachable, because `SipiSize` rejects only a parsed `0` and `get_size` passes a `1` through unclamped. The first pass converted it to `kMalformedInput` and **failed `SipiImage.ScaleToOnePixelDoesNotCrash`**, a pre-existing test from `ba84f8b2` (the `bilinn` segfault fix) that pins exactly this. So unlike `crop` — where the function reported failure and the caller ignored it — here the author wrote `"skipping"` and a test to match: the intent was to skip, not to fail. Reverted that one arm to `log_warn` + success, converted only the unambiguous failures, **edited no test**, and raised the real question as a BLOCKER (see above). The general rule this establishes: **a discarded `false` is only the `crop` defect if nothing pins the discard**; if a test or an explicit "skipping" says the skip was intended, it is a product decision and a chunk may not take it. `just bazel-test` **73/73**; `just commit-lint` clean. |
| 11D | done | `7acee418` | **The CLI seam gets its convention, and the convention is smaller than `error-model.md` implies.** `to8bps` → `[[nodiscard]] Result<void>` with all six call sites: PNG and TIFF read paths, the benchmark, the transform-matrix test, and — first time in this migration — **two CLI verbs**. The function itself is trivial (one `checked_buf_size_or_throw`, plus the `bps != 16` early exit which is a genuine "nothing to do" success and stays one). The value is in the seam. **The shipped CLI is binary `EXIT_SUCCESS`/`EXIT_FAILURE` everywhere** — `cli_app.cpp`, `convert_access_file.cpp`, `convert_service_file.cpp`, `verify.cpp`, `health.cpp`, each header documenting exactly those two — while `error-model.md` says the exit code "derives from `HttpStatusClass`" with "distinct non-zero codes". The doc describes an intention, not the code. Since the plan's acceptance criterion is that **CLI exit codes are verified unchanged**, the convention set here is: `diagnostic_message()` (not the Lua seam's redacted `client_message()`) into the verb's **own existing** reporting idiom, under the **same phase label** (`"read"`), then `EXIT_FAILURE`. The two verbs' idioms were deliberately *not* unified into a helper — there is no third caller, and `CLAUDE.md` § Scope discipline forbids the abstraction. Copy these verbatim in later chunks: `cli_app.cpp` → `populate_from_image` + `log_err("Error reading image: %s", r.error().diagnostic_message().c_str())` + optional `emit_json_report(..., std::string{"read"})` + `EXIT_FAILURE`; `convert_access_file.cpp` → `populate_from_image` + `report_error(sentry_ctx, "read", r.error().diagnostic_message(), args.json_output)` + `EXIT_FAILURE`. The surrounding `try`/`catch` blocks stay: `readSource` and the `convertToIcc` on the very next line still throw, and narrowing those catches is Phase 8's tail, not this chunk's. Neither handler site needed a release — PNG has already torn down its read struct by then, TIFF's handle is under an RAII `unique_ptr` guard. `just bazel-test` **73/73**; file modes preserved; `just commit-lint` clean. |
| 11C | done | `efb4010d` | **A worker classified three guards as caller-misuse invariants, I sent it back, and the evidence is the reusable part.** `removeChannel` and `removeExtraSamples` → `[[nodiscard]] Result<void>` with all four call sites, converted as **one** commit because `removeExtraSamples` calls `removeChannel` in a loop — splitting them would have left the loop swallowing a `[[nodiscard]]` result, i.e. the adapter the ruling forbids. That is the first instance of the "tightly-coupled pair is one unit" rule; `rotate`/`set_topleft` is the next. The first pass converted the `bps` and buffer arms but left the three `"Cannot remove component"` guards throwing, on the ADR-0024 Decision 7 reading that an out-of-range channel index for the image's actual shape is a programming error. **Plausible, and wrong on the facts.** Both quantities those guards test are file-derived: `SipiIOTiff.cpp:1063` copies `TIFFTAG_EXTRASAMPLES` into the image with `eslen` straight from the file and **no cross-check against `SamplesPerPixel`**, so a TIFF declaring `SamplesPerPixel=3` and `EXTRASAMPLES=[1]` yields `nc == 3` with a registered extra sample; `removeExtraSamples` then loops from `content_channels`=3 and calls `removeChannel(img, 3, …)` on a 3-channel image, tripping `channel >= nc`. Reached from `SipiIOJpeg::write` and `SipiIOPng::write` — both `Result`-returning — so the throw escaped a function whose contract says failures are values. Malformed input, and exactly the class the codec fuzz harnesses this plan added are meant to reach. The worker re-verified the evidence itself before acting and agreed. **The general lesson for the remaining chunks:** "caller misuse vs malformed input" is not decided by how the guard *reads*, it is decided by tracing where its operands come from — if any operand is populated from a decoded file, it is input. The two `// TODO: figure out when this can happen` comments stayed: which real files reach guards (2) and (3) is still unknown, and that is information. Both handler sites re-verified for cleanup (PNG mirrors its sibling's `png_free_data`; JPEG has no `cinfo` live yet — it is declared *after* the call). One side finding raised, not taken: `SipiIOPng::write`'s error arms disagree about destroying the write struct. `just bazel-test` **73/73**; `just commit-lint` clean. |
| 11B | done | `fe23fbde` | **The first clean instance of the unit, with nothing to argue about — which is the point of recording it.** `convertYCC2RGB` → `[[nodiscard]] Result<void>` with both of its call sites in the same commit. Three throws became values: the terminal unsupported-`bps` arm and, newly, **both pixel-buffer allocations**, which stop going through the file-local `checked_buf_size_or_throw` and call `Sipi::checked_buf_size` directly with a `kMalformedInput` on `nullopt`. That last part is the pattern the remaining chunks inherit (see the second correction in the round-11 section): the throwing helper is not deleted, it is *drained* one function at a time and dies with the last chunk that empties it. Message wording carried over verbatim, so no log line anywhere reads differently. The J2K site propagates with the established `if (auto r = ...; !r) { return std::unexpected(r.error()); }`; the regression test's `ASSERT_NO_THROW` became `ASSERT_TRUE(result.has_value())`, which is strictly the stronger claim — `ASSERT_NO_THROW` would have passed on a function that returned an error and did nothing at all, and after this migration that is exactly the failure mode a stale `ASSERT_NO_THROW` stops catching. **Every remaining chunk must make that substitution, not just delete the macro.** Grep confirmed exactly two sites. `just bazel-test` **73/73**; `just commit-lint` clean; changed-lines-only formatting held. |
| 11A | done | `2a56f72a` | **The maintainer's standing ruling on the empty `else`, executed — and the reachability answer is the interesting half.** Both `crop` overloads ended their `bps == 8` / `bps == 16` chain with `else { // clean up and throw exception }` followed by `return {}`, so an unsupported bit depth reported **success on an untouched image**. That is `86dd36fd`'s defect one level down: `86dd36fd` stopped callers *discarding* crop's answer, and this stops crop *lying* in that answer. Landed as a dedicated `fix:` rather than a fold-in, per the ruling's own wording, and because `86dd36fd` is already pushed to PR #797 with CI in flight — a rewrite would have invalidated the run for no gain. `kMalformedInput`, naming the offending `bps`, matching the neighbouring crop rejections in the same file. **The worker was asked to prove reachability before editing and did**: every registered read path normalizes bits/sample to 8 or 16 before pixels reach `SipiImage` — TIFF promotes sub-8-bit via `one2eight`/`read_standard_data` (tiled TIFF pre-rejects the rest), PNG expands sub-8-bit gray and palette through libpng transforms, JPEG emits 8-bit only, and J2K's decode switch covers `{8,12,16}`, coercing 12→16 and rejecting everything else with `kUnsupportedFormat` before `set_pixels`. So the hole is **latent, not live**, and the commit body says so rather than claiming a user-visible fix. It is still the right change: a failure that reports success is the one outcome this error model exists to prevent, and the ruling is explicit. The new test has to build the unsupported image through `set_pixels`, because `SipiImage`'s constructor accepts only 8 and 16 — worth knowing for every later chunk that wants to pin an unsupported-bps arm. `just bazel-test` **73/73**; `just commit-lint` clean; changed-lines-only formatting held (no whole-file reflow of `processing.h` this time). |
| 10F | done | `86dd36fd` | **The merged Phase 6/8 finale turns out to be decomposable, and this chunk is the proof.** The maintainer's ruling said Phase 6's `image_processing` conversion must execute *together* with Phase 8's caller work — no intermediate throw-adapters, no touching ~80 call sites twice. That was read in round 9 as "one enormous atomic change". It is not: the unit that satisfies the ruling is **one function plus all of its call sites in one commit**, and the call-site counts make that tractable — `crop` 6, `set_topleft` 2, `convertYCC2RGB` 2, `removeChannel` 2, `removeExtraSamples` 2, `toBitonal` 2, `scaleFast`/`scaleMedium` 5, `to8bps` 6, `add_watermark` 7, `scale` 13, `convertToIcc` 15, `rotate` 37. Each such commit is boundary-complete, needs no adapter, and touches each site exactly once. **`crop` went first deliberately** — it is small *and* it is the ruling's own headline example. The defect it closes is real and user-visible: `(void)Sipi::processing::crop(*img, region);` in the JPEG and PNG read paths meant a rejected crop left the image untouched and the handler carried on, **serving a full-size image in place of the region the client asked for**, silently. Ruling (1) mapped cleanly onto the geometry: a region entirely outside the image, or one that shrinks to nothing once a negative offset is clamped, is `kMalformedInput` and now carries the offending values; `return true;// we do not have to crop!!` is genuinely "nothing to do" and stays success. **The seam decision the next twelve chunks will copy** is in `image_handle.cpp`: `emit_str(err, err_ctx, cropped.error().client_message())` then return non-zero, mirroring the file's existing `catch (SipiError &)` convention exactly — `client_message()` (redacted, no source location) is the correct accessor for a Lua-visible string, `diagnostic_message()` would leak paths. The two overflow regression tests got **stronger**, not weaker: they asserted a bare `false` and now assert both failure and the `ErrorCode`. Process-tier benchmark: `OVERALL_GEOMEAN +0.16 % / +0.18 %`, and `BM_Crop1024` — the only case whose code changed — reads `−0.16 %`. `just bazel-test` **73/73**; approval goldens byte-identical; `just commit-lint` clean. One caution for the next chunks, from this one: the worker ran `clang-format -i` without `--lines` and reformatted the whole of `processing.h`, caught and reverted it itself. That file is about to be edited twelve more times — brief every chunk on changed-lines-only formatting. |
| 10E | done | `14b6d89d` | **Phase 7 closes by deleting four catch blocks, not one.** The plan's checkbox named a single "Phase 5c try/catch"; the analysis found four `catch (const std::exception &)` on the JPEG metadata paths — two around `parse_photoshop` and two around the APP1 XMP extraction, one of each in `read` and `read_shape`. This chunk was briefed as **analysis first, edit second**, with an explicit instruction not to delete on the strength of the `TODO` alone, and the reachability trace is the deliverable: the IPTC/ICC/EXIF cases now go through `Result` factories that log and return early themselves; **`Exif::getValByKey` already wraps its own body in `catch (const Exiv2::Error &) { return false; }`**, so it cannot propagate (this was the one genuine unknown); `Xmp`'s constructor is a string assignment; the setters are assignments and the block walk is pointer arithmetic. **What is left is `std::bad_alloc`, and that is why deletion was the right answer rather than a narrowing.** A `catch (const std::exception &)` here would catch *only* an OOM and downgrade it to `log_warn` and carry on — ADR-0024 Decision 7 keeps `bad_alloc` exception-based precisely so the seams handle it. `read_shape`'s block additionally claimed to prevent unwinding past the live `jpeg_decompress_struct`; an OOM does that at every other allocation in the same function, so the guard never bought that either. Two fold-ins: the two APP1 XMP blocks were scoped out of the brief and had to be pulled back in (leaving them would have had the file handle one situation two opposite ways), and the three `// Mirrors the callers' catch handler` comments inside `parse_photoshop` became **dangling the moment the callers' handlers were deleted** — collapsed into one statement of the rule above the `switch`. `just bazel-test` **73/73**; approval goldens byte-identical; `just commit-lint` clean. |
| 10D | done | `37d4bef5` | **The last and largest metadata type, and the worker pushed back on my table — correctly.** I briefed `Icc(PredefinedProfiles)` for conversion; it was left as a throwing constructor instead, because its only two throws are the `icc_unknown` and `icc_RGB` caller-misuse invariants, so there is no input-parse failure on that path to convert, and converting it anyway would have dragged in `src/ffi/` and `src/cli/` (both prohibited) plus two benchmark/test files for zero gain. Accepted. The copy constructor stays throwing for a different reason worth writing down: `SipiImage`'s copy constructor and copy-assignment call it and **cannot return a `Result`**, and its failure mode is an *already-validated* profile failing to round-trip through lcms2 — systemic, in `std::bad_alloc`'s class, not malformed input. So three factories, not five: `parse`, `createFromProfile`, `createRGB`. **The reviewable finding is that I sent this one back too, for the mirror image of the `Xmp` problem.** `createRGB` came back returning `Result` while being *unable to fail* — it called `cmsCreateRGBProfileTHR` and stored the result unexamined — and three call sites wrote `img->set_icc(*Icc::createRGB(...))`, dereferencing an unchecked `expected`. That is UB the moment the error arm becomes reachable, and it is safe today only by the coupling `Result` exists to remove. Fixed by making the error arm **real**: a null-check on a C library return at an FFI boundary, which `CLAUDE.md` explicitly permits and which is not defense-in-depth — today a null from lcms2 becomes an `Icc` wrapping nothing and misbehaves far from the cause. The tone-curve triple and the lcms2 context are released on both paths. The three sites then took the outcome of their **nearest sibling ICC site in the same function**, so each function stays internally consistent: J2K propagates like its neighbouring `parse` cases in the same `switch`; TIFF logs and carries on like the `TIFFTAG_ICCPROFILE` arm of the same `if/else` chain. PNG and the JPEG APP2 accumulator were fatal and stay fatal, now releasing the `jpeg_decompress_struct` and the ICC buffer rather than unwinding past them. `iccBytes()`/`iccFormatter()` were left throwing deliberately — they are **emission**, not construction, and the ICC determinism invariant means every TIFF/JPEG/PNG/JP2 write still funnels through `iccBytes()`. Approval goldens byte-identical, which is the real gate here since ICC bytes are embedded in every emitted image; `just bazel-test` **73/73**; `just commit-lint` clean; file modes preserved (the worker caught its own editor dropping TIFF's executable bit). |
| 10C | done | `615cf7ee` | **`Exif::parse`, same shape as `Iptc`, and a third brief of mine proved wrong about a call site.** The type work is unremarkable: parse into locals, private constructor takes the parsed state, default constructor stays public and non-fallible because `SipiImage`'s lazy-init path (`SipiImage.cpp:262`) builds an empty `Exif`. `toURational`'s non-finite/negative guards and the `addKeyVal` template's unsupported-type throw stay exceptions — caller misuse, not malformed input, per ADR-0024 Decision 7; the worker was asked to say whether it agreed and did. **The five buffer sites have four different policies and all four were preserved.** JPEG's APP1 site in `read` and the J2K box tolerate and log. PNG's text-chunk site **swallows in silence** — an empty `catch` with a `// TODO: better error handling – now we nothing at all`; preserved silent, because adding a log line there is a change nobody asked for, with the TODO reworded to state the fact rather than deleted. The Photoshop site logs with its callers' wording and returns, reproducing the unwind. **The site my brief got wrong was `read_shape`'s APP1 EXIF (`:938`), which I claimed was try/catch-wrapped. It was not guarded at all** — so a malformed blob threw straight out of the shape probe, past a live `jpeg_decompress_struct`, leaking it. That is precisely what the comment two branches below warns against, and the leak was invisible because nothing guarded the site the comment was written for. It stays fatal (an unguarded throw and an error return are the same outcome class) but now calls `jpeg_destroy_decompress` first and returns the error, matching how every other exit in that function already works. `FdGuard` covers the fd either way. `just bazel-test` **73/73**; approval goldens byte-identical; `just commit-lint` clean; no test assertion edited. |
| 10B | done | `c539a9dd` | **`Iptc::parse` lands, and the reviewable part is a call site I sent back.** The type conversion itself is clean and the worker's shape is better than the brief's: the factory decodes into a **local** `Exiv2::IptcData` and hands it to a private constructor only after the decode succeeds, so no `Iptc` can exist half-parsed. Message text (`"No valid IPTC data!"`) unchanged. **The finding is that the four call sites do not agree with each other, and did not agree before this commit either.** J2K, TIFF and the JPEG Photoshop block all *tolerate* a bad IPTC block — log, carry on, serve the image without IPTC. PNG does not: its site had no guard at all, so a parse failure was fatal to the read. The worker's first pass harmonized PNG onto the tolerant behavior, flagged it, and **that was the one direction this migration must not take unasked** — it would serve an image SIPI currently refuses, i.e. a changed *success* case, the exact move chunk `9H` was forbidden. Sent back; PNG now returns the error through the `Result<bool>` its override already has. **What made the decision non-obvious is worth recording**: PNG's current throw is not a designed failure. It throws `Sipi::SipiError`, and the HTTP seam's read-path `try` (`serve_image.cpp:693-712`) catches only `std::bad_alloc`, `SipiImageError` and `SipiSizeError` — so today that exception escapes uncaught. "Preserve behavior byte-identically" would have meant preserving an unhandled escape. Returning the value keeps the *outcome class* (this input still does not decode) while making the failure mapped and Sentry-reported, which is strictly better and needs no ruling; harmonizing the other way still does. **Whether the four should agree is a real open question for the maintainer** — see Side findings. One text change to declare: these sites' log lines now read `Sipi image error at [...]` instead of `Error at [...]`, because the diagnostic comes from `SipiValueError::diagnostic_message()` rather than `shttps::Error::to_string()`. The message body is unchanged. **No test anywhere covers a malformed IPTC block in any format** — the worker grepped `test/unit` and `test/approval` and found zero hits. `just bazel-test` **73/73**; approval goldens byte-identical; `just commit-lint` clean; no test assertion edited. |
| 10A | done | `55a6118c` | **Phase 7 opens by discovering its first item does not exist. `Xmp` construction cannot fail, and has not been able to fail for years.** The worker was briefed to convert the six `throw SipiError(...)` sites in `xmp.cpp` into a `Result` factory; it read the file first and **refused the brief**, correctly. All three parsing constructors do `__xmpstr = <bytes>;` then hit a literal `return;`, and every throw sits in a `/* ... */` block *below* that return. Two facts settle the disposition rather than leaving it a judgement call: (a) `thisSourceFile`, the first argument of every throw in those blocks, is **defined nowhere in this repository** — the code would not compile if uncommented, which is precisely the chunk-`9G` rule ("dead code that would not compile if uncommented is worse than no block"); (b) the same dead-block shape appears in `operator<<` (iterating a never-populated `xmpData`) and in the destructor. So the blocks went, `xmpData` went with them (no reader or writer left anywhere in the tree), and both exiv2 includes went with *that*. **No `Result` factory was added** — an error arm that can never be produced is an abstraction with no caller, which `CLAUDE.md` § Scope discipline forbids; reactivating real Exiv2 parsing is a functional change with thread-safety implications and is not an error-model migration's business. The Phase 7 `Xmp` checkbox is answered by that reasoning, not by a conversion. **Three fold-ins were needed and one of them caught me out.** I told the worker `operator<<` had zero callers and to delete it; it tried, found `SipiImage.cpp:506` streams `*(rhs.xmp)`, **reverted cleanly and reported the blocker** — my grep had missed the dereferenced-pointer idiom. The operator stays; its comment now states that it emits nothing and why, which is what makes the empty `XMP-Metadata:` section in `sipi`'s image dump legible instead of looking like a bug. `<iosfwd>`/`<ostream>` now supply the `std::ostream` declaration that the deleted exiv2 header had been dragging in by accident. `just bazel-test` **73/73**; approval goldens byte-identical; `just commit-lint` clean over the whole range; no test assertion edited. |
| 9H | done | `c2bbbd13` | **The fallback loops, redesigned — and the redesign is much smaller than four rounds of notes predicted, for a reason worth recording.** The round-5 side finding and every "where the next round starts" section since said the loop "erases the failing handler's cause". **That stopped being true at the Phase 5e vtable flip and nobody noticed**: once handlers returned `Result`, an error value already propagated its real message, errno and source location through `throw_from_value_error`, at both the dispatch site and inside the loop. The worker was briefed to verify that before acting, and did. So what was actually left was two narrow defects, both now fixed. (a) **The extension/mimetype-dispatched handler was tried a second time inside the fallback loop.** Its answer is deterministic, so the repeat bought nothing and cost a second *full decode* attempt on every mis-extensioned file. The dispatched key is remembered and skipped. (b) **The terminal throw said nothing** — `Error reading file <path>` with no indication of what was attempted. It now names the handler keys tried, which is the one place a cause was genuinely lost. **Stop-on-error semantics were deliberately kept exactly**: an error still halts the search rather than continuing to the next handler. Turning it into a `continue` would let a later handler succeed where the read fails today — a *changed success case*, which the brief forbade. The rule is now written above both loops as enduring text. A `SipiImage`-level test covers the fallback (a PNG carrying a `.tif` extension must decode to the same geometry as the same file read under its own name); the pre-existing "not a TIFF / not a PNG" tests only ever drove one handler directly, which is why this path had no coverage. `read_shape`'s fallback is verified by reading only — its dispatch sniffs content via libmagic, so a misnamed fixture cannot force it. Approval goldens byte-identical; `just bazel-test` **73/73**; no test assertion edited. |
| 9G | done | `280a001e` | **The last friendship is gone. `grep -n "friend class" src/image/cpp/SipiImage.h` returns nothing** — only the `operator<<` friend remains, and it is not part of this work. `SipiImage`'s protected state is now protected *in fact* rather than by the convention that only four trusted classes reached past it. ~300 sites, the largest of the four. One member joined the surface: `getSkipMetadata()`, needed because TIFF is the only handler that **reads** the mask (all four set it via the already-public setter) — which is why three chunks got by without it. The thirteen `ensure_exif()` calls became `exif_writable()`, which does the lazy allocation and returns the instance in one call, so the private method has no external caller left. The buffer-ownership trap did not apply: TIFF has no `setjmp`, libtiff reports failure by return code, and the two pixel-reader helpers already *return* a `Result<std::vector<T>>` by value rather than holding one across a failing call — so their existing shape was preserved untouched, as briefed. **One deliberate extra:** two long commented-out blocks (a `TIFFTAG_PAGENAME`/`PAGENUMBER` fragment and an old `convertToIcc` YCbCr/CMYK fragment) were deleted rather than left, because both quote protected members through an access path that no longer exists — dead code that would not compile if uncommented is worse than no block. Approval goldens byte-identical; `just bazel-test` **73/73**; `just commit-lint` clean over the whole range; file mode 755 preserved; no test assertion edited. |
| 9F | done | `5c0a1035` | **The J2K friendship is gone; only TIFF's is left.** ~160 sites, no new member needed — the second handler in a row that the PNG-chunk surface covered without additions. The JPEG leak lesson was carried in the brief and applied: all three decode branches (bps 8 / 12 / 16) allocate the buffer into the image with `set_pixels` **before** `pull_stripe` and write through a `byte *dst` hoisted once. Strictly speaking J2K did not need it — `pull_stripe` fails by raising a `kdu_exception`, which unwinds and would have destroyed a local — but one ownership shape across four handlers beats a per-codec argument about which failure mechanism forgives what, and the file's `catch (...)` sites make the wrong shape easy to reach. Two details worth the reviewer's eye. (a) The **12-bit branch** used to do `pixels = std::move(buf); bps = 16;` as two statements; the promoted sample depth now rides the same `set_pixels` call that takes the buffer. (b) Routing these through `set_pixels` puts its size check on a path that had none — it holds because `nx`/`ny` are set from `dims.size` at `:522` and each branch sizes its buffer from the same `dims`, and `force_bps_8` narrows `bps` before the switch that selects the branch. Palette expansion and the encode stripe loop hoist pointers outside the loop bodies; no accessor call per iteration. Approval goldens byte-identical; `just bazel-test` **73/73**; no test assertion edited. |
| 9E | done | `f286cb1b` | **The JPEG friendship is gone, and the surface needed no additions** — the six members PNG introduced covered every site, which is the first evidence that the pilot got the API right. **This chunk took two worker passes, and the reason is the most important thing in this row.** The first pass replaced `img->pixels.assign(ny * sll, 0)` with a function-local `std::vector<byte> buffer`, filled it in the scanline loop, and handed it over with `set_pixels(std::move(buffer), …)` afterwards. Tests were green and approval goldens were byte-identical — and it was **wrong**: the local is constructed *after* the `setjmp` (line 532) and stays alive across `jpeg_read_scanlines`, which can `longjmp`. `longjmp` skips destructors, so any decode error past the first scanline would have leaked the entire `ny * sll` pixel buffer, on exactly the malformed-input path the codec fuzz harnesses drive. Nothing leaked before, because the buffer belonged to the `SipiImage` and the caller destroyed it normally. The fix restores that ownership: `set_pixels` takes a zeroed buffer **before** the loop, a `byte *dst` is hoisted once, and the loop body and the CMYK inversion write through it — loop shape identical, nothing owning alive in the risk window. **The lesson for the two remaining handlers: `set_pixels(std::move(local), …)` after a fill loop is the natural-looking translation and it is a leak whenever the fill can `longjmp`.** The encode side was checked and is fine — `pixels_writable().data()` is a non-owning raw pointer, trivially destructible. Approval goldens byte-identical; `just bazel-test` **73/73**; no test assertion edited. |
| 9D | done | `b2fa1946` | **The first friendship is gone, and the public surface the other three will reuse is now fixed.** PNG went first deliberately — it is the smallest handler (~60 protected-member sites) and therefore the cheapest place to get the API shape wrong. Six members were added, each with a caller in this same commit and none speculative: `getIptc()`, `set_xmp`/`set_iptc`/`set_exif` (mirroring `set_icc`), `addEs` (one extra-samples entry at a time — chosen over get/modify/set, which reads worse at all three `push_back` sites), and **`set_geometry(nx, ny, nc, bps)`**, which is the load-bearing one: decode parses a container's geometry before any pixel buffer exists, so `set_pixels` cannot serve that phase. Its doc comment states the boundary — `set_pixels` stays the only size-checked path that moves buffer and geometry together, and the decode uses it once libpng has filled the rows. **Two consequences worth the reviewer's eye.** (a) Routing the decode's buffer handoff through `set_pixels` adds a size check where a raw `pixels = std::move(buffer)` had none; it throws on mismatch, which is in-policy (ADR-0024 keeps logic-bug invariants exception-based) and never fires on the corpus. (b) The four-channel PNG **encode** branch converts to sRGB and then reports `nc=3, bps=8` **without resizing the buffer** — geometry deliberately inconsistent with the buffer. Preserved verbatim via `set_geometry`, not fixed; recorded as a side finding. Approval goldens byte-identical; `just bazel-test` **73/73**; no test assertion edited. |
| 9C | done | `6ca4a466` | **Phase 6 opens: `app14_transform` is off `Image`.** Its own commit, never folded into the friend-removal change — the deferral said so and the audit called it a behavior change dressed as a refactor. The audit's caution turned out to be conservative rather than wrong, and it is worth recording *why*: the field's entire lifetime is one `SipiIOJpeg::read` call (written at the Adobe APP14 marker parse, read once by the CMYK/YCCK polarity check ~90 lines later, never again), and the handler already re-inverts libjpeg-turbo's inverted CMYK before returning — so "downstream sees standard CMYK" was **already true** and removing the field changes nothing observable. What actually went was the field plus four copy/move propagations that existed only because it was there. **Volatile-locals determination, recorded because ADR-0024 demands it be made explicitly:** no `volatile`. The `setjmp` landing block reads only `icc_buffer_guard`, `jerr.error_message` and `cinfo`; the new local is written in the marker loop before `jpeg_start_decompress` and read after `jpeg_finish_decompress`, entirely outside anything the landing block observes. Approval goldens byte-identical — the real gate on a decode-path change; `just bazel-test` **73/73**; the two CMYK pins (`JpegCmykPhotoshopApp14Inversion`, `JpegCmykRawNoApp14NotInverted`) pass with assertions unedited. |
| 9B | done | `084b7051` | **JPEG-compressed TIFFs decode**, on the maintainer's round-9 ruling — the standing main-owned-bug pattern, own `fix(format_handlers):`. The mechanism was already fully diagnosed in round 8's side finding and needed no re-derivation: `TIFFTAG_JPEGCOLORMODE` lives only while the JPEG codec is bound to the *current* directory, and every `TIFFSetDirectory` re-binds it and resets the mode to `JPEGCOLORMODE_RAW`. `read` requested RGB conversion exactly once, right after `TIFFOpen`, then called `read_resolutions` (ends with `TIFFSetDirectory(tif, 0)`) and, on a pyramid, `TIFFSetDirectory(tif, level)` — so the setting was **always** gone before the first scanline. A file-static `set_jpeg_colormode_if_jpeg` re-requests it after every directory switch and keeps the round-8 compression guard, so no pseudo-tag is requested on a directory whose codec never registered it. **The fix has a second half, found by the worker and not anticipated by the brief.** `TIFFTAG_PHOTOMETRIC` keeps reporting the file's on-disk YCbCr encoding, but with RGB conversion enabled libtiff hands back scanlines that are already RGB; `photo` drives the ICC/colourspace branches downstream, so a YCbCr-tagged JPEG-compressed directory now sets `photo = RGB`. Without it the decode succeeds and the *JPEG output* is wrong — latent until now because no JPEG-compressed TIFF ever decoded far enough to exercise it. Both halves serve one outcome, so one commit. Two regression pins land with it: the `TiffJpegAutoRgbConvert` unit test is uncommented and its `// BROKEN!` banner deleted, and `tiff_jpeg_compression_input` (e2e) now requires a 200 and a decodable body instead of accepting a 500. Approval goldens byte-identical; `just bazel-test` **73/73**; rustfmt + clippy clean; `just commit-lint` clean over the whole range. |
| 9A | done | `3973df30` | **The dead `separateToContig` member is gone**, on the maintainer's round-9 ruling. Declaration (`SipiIOTiff.h`, with its doc comment) and definition (41 lines) deleted; the two file-static free function templates of the same name and their three call sites in `read_standard_data`/`read_tiled_data` are untouched — the name collision is the whole reason this was worth a deliberate chunk rather than a grep. It carried the last unconverted `throw` in the file ("Bits per sample not supported"), so `SipiIOTiff.cpp`'s live throw count is now exactly the two documented carve-outs (`memTiffOpen`'s `malloc` failures, `checked_buf_size_or_throw`) plus `read_watermark`'s six, which belong to `image_processing`'s own migration. Landed as `refactor(format_handlers):` — a deletion, not a conversion. `just bazel-test` **73/73**. |
| 8J | done | `66d46060` | **Phase 5e — the vtable flip, and the transitional wrap layer is gone.** `SipiIO`'s three virtuals return `Result` (`read` → `Result<bool>`, `read_shape` → `Result<SipiImgInfo>`, `write` → `Result<void>`), along with the four non-virtual convenience `read` overloads. Each handler's `*_impl` was **promoted to be the override itself** rather than left as a one-line forwarder, so no wrapper survives anywhere; all four move in the same commit because a half-flipped vtable does not compile. Executed as two worker passes over one change — core (base + 4 handlers + dispatcher, verified against library targets only, tests knowingly broken) then consumers (harness + tests) — and committed once. Five things worth the reviewer's eye. (a) **`SipiImage`'s three entry points keep their signatures**, so the FFI seam, the Lua bindings and the CLI needed **zero edits** — the blast radius of the highest-risk change in this plan turned out to stop at `//src/image`'s own boundary. (b) **Behavior is preserved exactly, including the part that is tempting to "fix"**: a handler's failure throws *immediately*, before any fallback handler is tried, because that is what a throw out of the old override did. The fallback-loop redesign is Phase 6's and was deliberately not smuggled in here. (c) The `kClientAbort` → `SipiImageClientAbortError` dispatch that all four handlers each carried collapses to **exactly one place**, `SipiImage::write` — the point of the flip, and verified by grep that no handler retains it. (d) **No test assertion was weakened.** The three `ASSERT_NO_THROW(info = io.read_shape(...))` sites kept *both* halves — still must not throw, **and** the outcome is now checked — and the "not a JP2 / not a TIFF / not a PNG / nonexistent / truncated" tests still assert the load-bearing contract that a format mismatch is a **successful** Result carrying a `FAILURE`-marked `SipiImgInfo`. A truncated TIFF was checked empirically and is in that class (libtiff fails to read the directory, so `TIFFOpen` returns null) rather than an error value. (e) The **fuzz-harness catch-scope note** was rewritten, which is a phase gate rather than tidying: a codec rejecting malformed input now reports a value, so the `try`/`catch` is documented for what genuinely still reaches it — `kdu_exception` (an `int`-like type, hence the bare `catch (...)`), `std::bad_alloc`, `checked_buf_size_or_throw`/`memTiffOpen`, `validate_decode_dims`, and the `Iptc`/`Exif`/`Xmp` constructors — with both discards made explicit under the new `[[nodiscard]]`. Approval goldens byte-identical; `just bazel-test` **73/73**; `just commit-lint` clean. |
| 8I | done | `be347e3b` | **Phase 5d is code-complete.** `cvrt8BitTo1bit` → `Result<std::vector<unsigned char>>`; its single call site in `write_impl` propagates the value unchanged. Two sites, both `kUnsupportedFormat` — a photometric interpretation that is neither MINISWHITE nor MINISBLACK, and a sample depth other than 8 — which is the call every handler makes for an input shape it has no branch for. Message text byte-identical **including the genuine double space** in `"…MINISWHITE or  MINISBLACK"`, which was deliberately preserved rather than tidied: byte-identity is the property the whole phase is verified against, and "while I was there" is how it gets lost. **Two carve-outs stay exception-based and this is the commit that settles them.** `memTiffOpen`'s two `malloc failed` sites are raw allocation failures — ADR-0024 Decision 7 keeps resource exhaustion permanently off the `ErrorCode` axis — and `checked_buf_size_or_throw` is the pre-allocation half of the same family. Both throws still surface from the override as the same `SipiImageError` the external contract already promised, so nothing observable changed by leaving them. Approval goldens byte-identical; `just bazel-test` **73/73**; `just commit-lint` clean over the whole range. |
| 8H | done | `f4cadf15` | **The TIFF encode path.** `write`'s body → `write_impl` (private static member) returning `Result<void>`; `writeExif` and `cvrt8BitTo1bit` became `static` for the same reason `readExif` did — the class carries no instance state, so its private helpers do not need any. Five failure sites, message text byte-identical: the two libtiff open failures (`TIFFClientOpen` for the in-memory target, `TIFFOpen` for a real path) and an unroutable output destination → `kWriteFailed`; a sample depth the encoder has no branch for → `kUnsupportedFormat`, matching J2K and JPEG on the same fault; a peer that went away → `kClientAbort`. **The override keeps the separate `kClientAbort` → `SipiImageClientAbortError` branch**, which is load-bearing: the HTTP seam dispatches on that type to skip Sentry capture, so collapsing it would report every client disconnect as a server fault. All four handlers now carry that branch. One comment was rewritten rather than moved: the client-abort note referenced "the old OUTPUT_WRITE_FAIL abort signal" — machinery that no longer exists — and now states the enduring rule (a non-zero sink return means the peer is gone). Approval goldens byte-identical, which is the real check on an encode path; `just bazel-test` **73/73**. |
| 8G | done | `4f9687d9` | **The TIFF pixel readers.** `read_standard_data` and `read_tiled_data` return `Result<std::vector<T>>`, and the four call sites in `read_impl` propagate the helper's value **unchanged** (`return std::unexpected(std::move(r).error())`) rather than re-describing the failure. Because `read_impl` was already `Result<bool>`-returning, **no adapter code existed at any point** — the reason the entry point was converted before its helpers. Eight sites, message text byte-identical. All five failed scanline/tile reads → `kDecodeFailed`: libtiff funnels a truncated strip, a codec error, and an access mode it refuses for the directory's compression into the same return code, so the cause is genuinely erased there and `kMalformedInput` would over-claim — the same call PNG's single landing site makes. (This is not theoretical: `knora/tiffJpegScanlineBug.tif` fails at exactly this site for the *third* of those reasons; see the side finding.) A directory that claims tiles without tile dimensions, or whose tile count contradicts its own geometry → `kMalformedInput`. A sample depth the tiled reader has no branch for → `kUnsupportedFormat`. `checked_buf_size_or_throw`, which both helpers call for buffer sizing, stays exception-based. Verified byte-identity on the busiest site end-to-end: the known-broken JPEG-compressed fixture still reports `TIFFReadScanline failed on scanline 0, dimensions=7197x5441, channels=3, bps=8` with the source location intact. No test assertion edited; `just bazel-test` **73/73**. |
| 8F | done | `5ddfa11a` | **The TIFF decode path.** `read`'s body → `read_impl`, a **private static member** returning `Result<bool>`; the override re-wraps to a throw carrying `raw_message()`/`errnum()`/`location()`, vtable untouched. The static-member shape is forced, not stylistic — the decode reaches `SipiImage`'s protected pixel and metadata state and `friend class SipiIOTiff` grants that to the class, not the TU. **One thing had to move with it:** `read` calls `readExif`, a *non-static* private member, which a static `read_impl` cannot reach; `SipiIOTiff` has no data members and `readExif` uses none (verified), so it became `static` in the same change. Seven live failure sites convert, message text byte-identical. Three missing required tags (image width, image length, colormap) and an out-of-range palette index → `kMalformedInput`: the file already opened *as a TIFF*, so a directory that will not yield a required field is malformed data rather than the wrong format, and the palette-index case matches how the J2K decode classifies an undersized LUT. Unsupported bit depth and unsupported photometric interpretation → `kUnsupportedFormat`, matching J2K and JPEG on the same fault. **Two `throw` lines in the same line range are inside a large commented-out block** and were correctly left alone — the naive `grep -c "throw"` count of 36 for this file overstates the live work. The `bool` still means "is this my format" and **both** values stay successful Results. `checked_buf_size_or_throw` stays exception-based per the `std::bad_alloc` reasoning, and the two scanline/tile helpers still throw at this commit — both surface from the override as the same `SipiImageError` either way, so the intermediate state needed **no adapter code at all**. That is why the entry point was converted before its helpers rather than after. No test assertion edited; `just bazel-test` **73/73**. |
| 8E | done | `8651e5a9` | **The TIFF shape probe.** `read_shape`'s body → a **file-local** `read_shape_impl` returning `Result<SipiImgInfo>`; the override re-wraps to a throw carrying `raw_message()`/`errnum()`/`location()`, so the `SipiIO` vtable does not move. File-local rather than a static member, and the asymmetry with the `read` chunk is forced rather than stylistic: this probe touches no `SipiImage` state at all — only a local `SipiImgInfo`, `read_resolutions`, `Essentials::parse`/`parse_legacy` and the outcome counters. Exactly two sites convert, both `kShapeProbeFailed` (the code J2K and PNG use for a fault *inside* a probe), message text byte-identical: the `TIFFTAG_IMAGEWIDTH` and `TIFFTAG_IMAGELENGTH` tag reads. **Everything else stays a *successful* Result** — an unopenable or non-TIFF file keeps returning a `FAILURE`-marked `SipiImgInfo`, which is how the dispatcher's format probing learns to try the next handler, and the Essentials fast path returns its `info` unchanged. `SipiIOTiff.h` needed no edit; no test assertion was edited. `just bazel-test` **73/73**. One process note recorded by the worker and worth carrying: **do not run `clang-format` over a whole range in this file** — it reflows indentation across nested blocks and flips the file mode 755→644. Hand-check the lines you write against the 120-column limit instead. |
| 8D | done | `6acafddd` | **Phase 5d opens: libtiff's diagnostics are no longer discarded.** Both callbacks format the `va_list` locally with `vsnprintf` into a stack buffer and pass the result to `log_err`/`log_warn` as a **`%s` argument**, never as a format string; each body is locals-only (the handlers are process-global and entered from every decoding thread) and wrapped in a `catch (...)` — a C-library boundary, which is where `CLAUDE.md`'s no-defense-in-depth rule permits a guard, and the comment says so. The stale UTF-8 blame comments are gone. **Three call sites went with the re-enablement, and the third is the reason this chunk took two passes.** (a) `TIFFSetWarningHandler(nullptr)` at the top of both `read` and `read_shape` disabled the *global* warning handler and never restored it — so no warning could reach the log from either hot entry point, and one thread's call silenced warnings for every other thread mid-decode. (b) `read` re-installed both handlers on every call, which `initLibrary` already owns; per-decode mutation of process-global libtiff state from many threads is the same defect. (c) **`read` requested `TIFFTAG_JPEGCOLORMODE` unconditionally.** That pseudo-tag is registered only by libtiff's JPEG codec (`tif_jpeg.c:229`; OJPEG does not register it) and exists only while that codec is bound to the directory, so on every TIFF that is not JPEG-compressed `TIFFSetField` rejected it — invisible while the handler was a no-op, but **one error line per decode of the commonest TIFF shape** the moment it went live. Verified directly (`cmyk.tif`, `cielab.tif`) before and after the guard. Skipping the call changes no decoding behavior: the setting never took effect on those files. Full corpus sweep (`unit/`, `bilevel/`, `knora/`, `malformed/`, both `read` and `read_shape`) after the guard: every remaining line is genuine signal — real faults on the malformed fixtures, benign tag warnings at **warn** level on two `knora` files and `image_orientation.tif`. Approval goldens byte-identical; `just bazel-test` **73/73**; `just commit-lint` clean over the whole range. Landed as a `fix(format_handlers):` — swallowed diagnostics and the spurious pseudo-tag error both exist on `main`. |
| 8C | done | `940d8528` | **The `log_vformat` signature bug, fixed at the root.** `logger.h` declared `void log_vformat(LogLevel, const char *, ...)`; `logger.cpp` defines `void log_vformat(LogLevel, const char *, va_list)`. Those are two *different* functions, not one — the variadic overload has no definition anywhere, so a caller outside `logger.cpp` reaching for the obvious name (the `va_list` twin of `log_format`, the way `log_vsformat` is the twin of `log_sformat`) binds to the declaration and fails at **link** time on an undefined symbol. Latent since `b2899d74` only because all five call sites live inside `logger.cpp` and bind to the definition. The header is what was wrong, so the declaration was corrected rather than the definition. `#include <cstdarg>` added in the same change: the header has used `va_list` since `log_vsformat` was introduced and was relying on a transitive include from its consumers. Standalone `fix(logging):` because the bug exists on `main`. Grep across `src/` and `test/` confirms no external caller. Full `//src/...` build green — the real check, since this header is included nearly everywhere; `just bazel-test` **73/73**. |
| 8B | done | `767d7a02` | **Both JPEG `setjmp` landing blocks now read defined values, on the maintainer's ruling.** The round-7 side finding was one violation; the pass found a second, larger one in the same file. (a) **Decode.** `icc_buffer_guard` is declared before the `setjmp`, assigned during ICC marker parsing *inside* the risk window, and handed to `free()` from the landing block — the one-token fix, `unsigned char *volatile`, with the reference alias `icc_buffer` re-qualified to match (it does not compile otherwise). No cast was needed at any of the seven use sites. `linbuf` and `marker` are modified in the same window but the landing block never reads them, so they are correctly left alone. (b) **Encode, and this is the part worth the reviewer's eye.** `volatile` is not expressible on a `std::unique_ptr` — `operator bool`, `operator->` and the destructor are not volatile-qualified — so this took ADR-0024's *other* sanctioned route, hoisting out of the risk window. It was also **four objects, not one**: the landing block reads `html_buffer` directly, and `sink_stream`/`html_buffer`/`file_buffer`/`destmgr` are all **destroyed on the return path out of it**, and a destructor run is a read. All four (plus `outfile_guard.fd`) were assigned *after* the `setjmp`. Construction now happens before `jpeg_create_compress`; only the libjpeg wiring calls (`jpeg_html_dest`/`jpeg_stdio_dest`/`jpeg_file_dest`) stay behind the `setjmp`, because `jpeg_stdio_dest` is libjpeg's own and can `longjmp`. Consequence: the unopenable-output-file rejection now precedes `jpeg_create_compress` and its `jpeg_destroy_compress` call was dropped — destroying a struct that was never created is wrong — with the error code and message text byte-identical. Approval goldens byte-identical; `just bazel-test` **73/73**. PNG's `http_ctx` was assessed and left: `&http_ctx` escapes to `png_set_write_fn` *before* the `setjmp`, so the compiler must keep it addressable. |
| 8A | done | folded into `8a512105` (was `9de5d8c6`) | **ADR-0024's TIFF rationale corrected to the real root cause, on the maintainer's ruling.** Decision 5's TIFF bullet stated that `tiffError`/`tiffWarning` were disabled "because malformed UTF-8 in libtiff's own format strings caused segfaults when passed through `vsnprintf`-family formatting". That is folklore. The bullet now states the verified defect: the commented-out body forwards a `va_list` into `log_err`'s `...` slot — a va_list-vs-varargs ABI mismatch that misreads the caller's variadic state for **any** format string carrying conversions, nothing UTF-8-specific — and the line never compiled in the first place (it names an undeclared identifier). The **decision is unchanged**: TIFF migrates last, and the callbacks are re-enabled with formatting safety rather than blind-uncommented. The bullet now also names the safe mechanism (`log_vsformat` formats the `va_list`; the result is passed as a `%s` **argument**, never as the format string) and its in-repo model (`SipiIOPng.cpp`'s `sipi_error_fn`), so a reader following the ADR can act on it, and records that neither TIFF handler has a `setjmp` landing site — libtiff reports failure through ordinary return codes the call sites already check — which is why TIFF does not appear in Decision 6. Written as settled fact with no trace of a claim having changed; that is what the fold-in buys. Whole file swept for other instances of the folklore: there were none. Docs-only. |
| 7F | done | `c6e10878` (was `e2dc3143`) | **The JPEG encode path — Phase 5c is code-complete.** `write`'s body → `write_impl` (private static member) returning `Result<void>`; eleven failure sites, message text byte-identical. `kWriteFailed` for the three quality-argument rejections, the unopenable output file and a libjpeg encode failure; `kUnsupportedFormat` for the four channel-count mismatches and the unsupported colorspace; `kClientAbort` preserved as its own branch in the override, throwing `SipiImageClientAbortError` (the Sentry-skip dispatch). Two decisions worth the reviewer's eye. (a) **No new `ErrorCode` variant was invented for the quality argument.** An out-of-range compression parameter is not malformed image data, and there is no "invalid argument" variant; adding one would need ADR-0024 *and* `error-model.md` changed, so it reports `kWriteFailed` — the honest bucket, since the write could not be performed. (b) The `stoi` `try`/`catch` stays: it catches a standard-library throw, not a codec failure. **One factual comment correction rode along**, and it is the reason to read this diff: the "NOTE on RAII" block claimed the objects leaked inside the `setjmp` risk window "are freed when the thread handles the next request". They are not — a `unique_ptr` whose destructor `longjmp` skipped never frees, at any later point. The leak is real, bounded (<65 KB) and on an error path, which is the actual reason it is accepted, and the note now says that instead. Approval goldens byte-identical; `just bazel-test` **73/73**. |
| 7E | done | `69900f99` | **The JPEG decode path.** `read`'s body → `read_impl` (private static member) returning `Result<bool>`; the override re-wraps to a throw. Four failure sites, message text byte-identical: the `setjmp` landing site → `kDecodeFailed` (one funnel for every libjpeg error, cause erased, same reasoning as PNG); `jpeg_read_header` not returning `JPEG_HEADER_OK` → `kMalformedInput`, which is grounded rather than arbitrary — the file already passed the SOI magic check, so it *is* a JPEG whose header will not parse; both unrecognised colorspaces (`JCS_UNKNOWN` and the `default` arm) → `kUnsupportedFormat`. `jpegErrorExit` and the `char[] error_message` untouched per the ADR. The three `try`/`catch (const std::exception &)` blocks around the `Iptc`/`Exif`/`Xmp` constructors and `parse_photoshop` stay, with their comment intact — Phase 7 deletes them when those constructors become `Result` factories. `parse_photoshop` was **already `static`**, so the static-member `read_impl` calls it unchanged; that was the one thing that could have blocked the chunk. Two comments corrected: the `setjmp` banner carried in-flight history from an earlier change ("This replaces the scattered try/catch(JpegError) blocks…") and the landing block described its own throw. `just bazel-test` **73/73**. |
| 7D | done | `4aae7a4e` | **The PNG encode path — Phase 5b is code-complete.** `write`'s body → `write_impl` (private static member, same friendship reason as `read_impl`) returning `Result<void>`. Five failure sites: three libpng resource failures and an unopenable output file → `kWriteFailed`; an unsupported channel count → `kUnsupportedFormat`, matching how the J2K encode classifies an unsupported sample depth; the `setjmp` landing site keeps its client-abort branch. **The override keeps the separate `kClientAbort` → `SipiImageClientAbortError` branch** — the HTTP seam dispatches on that type to skip Sentry capture, so collapsing it would report every client disconnect as a server fault. The `try`/`catch (SipiError &)` around the ICC bytes is untouched; it belongs to the metadata layer's own migration. Two comments were corrected: the declare-before-`setjmp` one now states the rule, and the "kept alive for the whole of `SipiIOPng::write`" one now names `write_impl` (the rest of it — addresses staying valid across `longjmp` — was already exactly right and stayed). Approval goldens byte-identical, which is the real check on an encode path; `just bazel-test` **73/73**. |
| 7C | done | `1adfdd8a` | **The PNG decode path.** `read`'s body → `read_impl`, a **private static member** returning `Result<bool>`; the override re-wraps to a throw, vtable untouched. The static-member shape is forced, not stylistic — the decode reaches `SipiImage`'s protected pixel and metadata state and `friend class SipiIOPng` grants that to the class, not to the TU — which is why this chunk's shape differs from `7B`'s file-local probe. Three sites convert, **all `kDecodeFailed`**, message text byte-identical. The uniform code is the point rather than a shortcut: PNG has exactly one `setjmp` landing site and it funnels *every* libpng error (truncation, CRC failure, a bad chunk, an I/O error mid-read), so the cause is genuinely erased there and `kMalformedInput` would over-claim. J2K classifies more finely only because it converts at individually named Kakadu call sites. Two comments describing the declare-before-`setjmp` discipline were rewritten to state the rule (destructors run on the normal C++ path out of the landing block; a `longjmp` skips anything constructed inside the window) rather than the throw that used to follow it — the mechanism moved, the rule did not. `sipi_error_fn` untouched per the ruling; `validate_decode_dims` untouched; both `return FALSE;` signature-mismatch paths still successful Results carrying `false`. Volatile-locals check clean: `png_ptr`/`info_ptr` are assigned before the `setjmp` and never mutated inside the window, and the landing block reads no vector. `just bazel-test` **73/73**, `PngErrorPath.TruncatedPngReadThrowsCleanly` passing with its assertion unedited. |
| 7B | done | `dde844cf` | **Phase 5b's first conversion — the PNG shape probe.** `read_shape`'s body moves into a file-local `read_shape_impl` returning `Result<SipiImgInfo>`; the override re-wraps to a throw, so the `SipiIO` vtable does not move. Only two sites convert — the `png_create_read_struct` and `png_create_info_struct` null returns, both `kShapeProbeFailed`, message text byte-identical. **Everything else stays a *successful* Result**: the unopenable file, the PNG-signature mismatch, and the `setjmp` landing site all keep returning a `FAILURE`-marked `SipiImgInfo`, because the dispatcher's format probing reads that as "not a readable PNG, try the next handler". That is the same line J2K's shape probe draws. File-local (not a static member) is correct here and asymmetric with the `read` chunk on purpose: the probe touches no `SipiImage` state. Worker correctly reported that the plan's test-target names do not exist — `imginfo_test.cpp` and `format_error_path_test.cpp` both compile into the single `//src/format_handlers:formats_test`. `just bazel-test` **73/73**, no assertion edited. |
| 7A | done | folded into `9de5d8c6` (was `8afd88da`) | **ADR-0024 Decision 6 reversed for PNG, on the maintainer's ruling.** PNG keeps `setjmp`/`longjmp` via `png_jmpbuf` and takes JPEG's landing-site pattern; only the landing block's final control transfer changes from `throw` to `return std::unexpected`. Decision 6 was restructured so PNG and JPEG are presented as one treatment with two shared disciplines (RAII-before-`setjmp`, volatile locals) plus one JPEG-specific rule (`jerr.error_message` stays `char[]`); Decision 5's "two different treatments" pointer was corrected; and the Considered Options bullet was **inverted** — the libpng throw hook is now the rejected option, carrying the three grounds the maintainer gave: the shipped `fix:` `94f45a28` (2026-04-04) removed throw-through-libpng as exception-through-C UB, libpng is a BCR `bazel_dep` whose compile flags are not ours (no guaranteed unwind tables, and vendoring it solely for throw propagation is not worth it), and JPEG carries the identical hazard at `SipiIOJpeg.cpp:86`. Written as settled fact with **no trace of a decision having changed** — that is what the fold-in buys, and it is why the ADR carries no "amended" note. The rest of the file was swept for PNG-throws-from-callback assumptions; Decision 4 and Consequences had none. Docs-only. |
| 6F | done | `26bfff9c` (was `ef423c93`) | **The J2K encode path — Phase 5a is code-complete.** `write`'s body → `write_impl` (private static member, same friendship reason as `read_impl`) returning `Result<void>`. Three failure sites: an invalid tiling parameter and a Kakadu encode failure → `kWriteFailed`, an unsupported sample depth → `kUnsupportedFormat` (matching how the decode path classifies the same fault), a peer that went away → `kClientAbort`. **The override keeps a separate branch for `kClientAbort` → `SipiImageClientAbortError`**, which is load-bearing rather than cosmetic: that type is what the HTTP seam dispatches on to skip Sentry capture, so collapsing it into the generic error would start reporting every client disconnect as a server fault. Two things deliberately not touched: the inner `catch (kdu_exception)` around the colour-space setup is a **recovery** path (it falls back to a standard profile and carries on), not a failure report; and the error path still does not close the JPX target, because closing a `jpx_target` with incompletely written codestreams raises another Kakadu error. The "Tiling parameter invalid!" throw sat inside the big `try` but was never caught by its `kdu_exception` handler, so it escaped without that handler's `codestream.destroy()` — an early `return` behaves identically, and no cleanup was invented. Approval goldens byte-identical (this is the encode path, so a golden diff would have meant real movement); `just bazel-test` **73/73**. |
| 6E | done | `42917204` | **The J2K decode path.** `read`'s body → `read_impl` returning `Result<bool>`; the override re-wraps to a throw, vtable untouched. Eleven failure sites became error values with **byte-identical message text**: corrupt containers / truncated codestreams / buffer-size overflows → `kMalformedInput`, unimplemented colour spaces / ICC spaces / channel counts / sample depths → `kUnsupportedFormat`. The palette check now propagates its own value (`return std::unexpected(validated.error())`) instead of the throw-wrap chunk `6D` had to leave at that call site. Three things worth the reviewer's eye. (a) **`read_impl` is a private *static member*, not a file-local function like `read_shape_impl`** — the asymmetry is forced, not stylistic: the decode reaches `SipiImage`'s **protected** pixel and metadata state, and `friend class SipiIOJ2k` grants that to the *class*, not to the TU. A file-local free function does not compile. A private static member does not touch the vtable. (b) **The `bool` still means three things and all three survive**: "not a JPEG2000 file" *and* a failed `pull_stripe` both stay a **successful** Result carrying `false`. The second is the cause-erasure the round-5 side finding describes; converting it here would change which handler the dispatcher believes owns the file, so it is deliberately left for the dispatcher's own redesign. (c) The maintainer's ruling was honored: `checked_buf_size` and `validate_decode_dims` are untouched — only how their failure is *reported* changed. `decompressor.finish()` still runs before the unsupported-bits/sample report, and the three `catch (SipiError &)` metadata blocks are untouched (they belong to the metadata-factory phase). Approval goldens byte-identical; `just bazel-test` **73/73**. |
| 6D | done | `950aa81c` | **The palette guard, and the first throw-assertion test conversion.** `validate_j2k_palette_mapping` → `Result<void>`, messages byte-identical. Codes: the three "the expansion loop cannot address this shape" cases (`bps != 8`, `nc != 1`, `numcol != 3`) are `kUnsupportedFormat`; an undersized LUT is `kMalformedInput`, which is grounded rather than arbitrary — the fixture pinning it is literally `malformed/palette_undersized_lut.jp2`. The six direct-call assertions in `j2k_palette_regression_test.cpp` moved from `EXPECT_THROW(..., SipiImageError)` to result assertions that check `error().code()` **and** a distinguishing substring of `raw_message()`, so they now pin *which* failure was reported; before, all four rejections were indistinguishable to the test. The two end-to-end tests that drive `img.read(...)` were left untouched and still pass — the decode path re-wraps at the call site, so its external contract has not moved. Orchestrator caught one thing the worker missed: the grown return type pushed the declaration to 144 columns and the definition to 129, over the repo's `ColumnLimit: 120`; `clang-format --lines=` on exactly those two lines, leaving the four *pre-existing* over-long lines in the file alone. `just bazel-test` **73/73**. |
| 6C | done | `09dfa8cf` | **Phase 5a's first conversion — the J2K shape probe.** `read_shape`'s body moves into a file-local `read_shape_impl` returning `Result<SipiImgInfo>`; the override re-wraps to a throw, so the `SipiIO` vtable does not move (ADR-0024 Decision 4 step 1). `kdu_exception` is caught at exactly the three Kakadu edges that raise it — `jpx_in.open`, `access_codestream`/`open_stream`, and `codestream.create` + `set_fussy` — each becoming a `kShapeProbeFailed` value whose wording is copied from `read()`'s equivalent conversion, so the two entry points describe the same fault the same way. Three points worth the reviewer's eye. (a) **One deliberate behavior change, and it is the point of the chunk:** a Kakadu failure in the shape probe used to escape as a bare `kdu_exception` (an `int`) into a `catch (...)` past the FFI seam; it now surfaces as a `SipiImageError`, which is what `read()` already does deliberately at the same three calls. (b) **"Not a JPEG2000 file" stays a *successful* Result** carrying a `FAILURE`-marked `SipiImgInfo` — only a genuine Kakadu fault becomes `std::unexpected`, so the dispatcher's format-probing behavior is untouched. (c) The `siz->get(...)` shape reads sit **inside** the `create()` guard because they parse the same header; the four ints they fill are hoisted with explicit `= 0`. `KduReadTeardown`, the Essentials fast path and all four outcome counters are unchanged, and `imginfo_test` / `j2k_dimension_regression_test` / `j2k_palette_regression_test` pass with **no assertion edited** — which is the check that the override's external contract really did not move. Orchestrator sent the chunk back once, for comment wording: the impl's banner described the change ("converted to Result per … Decision 4 step 1" — both in-flight history *and* a work-breakdown step reference in shipped code), one clause claimed `read()` already converts to a value when it still converts to an exception, and `siz_params *siz` was hoisted out of the `try` despite being used only inside it. `just bazel-test` **73/73**. |
| 6B | done | `9ff74121` | **The accessor trio the wrap layer needs.** `SipiValueError` stored its message, `errno` and `source_location` but exposed neither raw: `client_message()` redacts paths and `diagnostic_message()` splices the location in, so neither reproduces the fields. `raw_message()`/`errnum()`/`location()` let a handler override — whose signature still promises an exception — construct a `SipiImageError` carrying **the same text, the same `errno` and the same origin** as the value its internals returned. That last part is why `location()` exists and why the wrap is not simply `throw SipiImageError(err.client_message())`: without it every J2K failure would report the boundary's line instead of the real failure site, and the redacted form would strip the full paths that server-side logs and Sentry need. Named `raw_message()`, **not** `message()`, because `SipiImageError::message()` means the *redacted, client-safe* form and reusing the name would invert it. Given its own commit rather than folded into `6C` — the two are separate packages and the accessors stand on their own. |
| 6A | done | `98e64b52` | **The round-5 `redact_paths` finding, fixed at the root on the maintainer's ruling.** `redact_paths` (`src/util/cpp/PathRedact.h`) kept only what followed a token's last `/`; because the whitespace-delimited token of `Cannot read file "/srv/images/sub/foo.jp2": broken` *includes the opening quote*, that quote was discarded with the directory prefix and every path-quoting message reaching an HTTP body or a Lua string carried an unbalanced `"`. A leading run of quote characters (`"`/`'`) is now preserved ahead of the redacted substring — which is what the function's own doc comment already claimed, so the fix makes code and comment agree rather than choosing between them. Both doc comments now state the rule explicitly. New `src/util/cpp/path_redact_test.cpp` (5 exact-string cases: quoted, unquoted, no-slash, multi-whitespace preservation, single-quoted) folded into the existing `util_test`; `sipi_value_error_test.cpp`'s expectation — which round 5 deliberately wrote against *verified buggy* behavior with an explanatory comment — flipped to the correct text and the comment rewritten. `format_error_path_test.cpp` needed no change: it asserts substring presence/absence, never the exact buggy string. Standalone `fix(error):` because the bug exists on `main`; scope is `error` (the concern is client-facing error text) even though the code lives in `util`. `just bazel-test` **73/73**. |
| 5D | done | `f3fd0a42` | **Phase 4's other half — the seam conversion helpers, and Phase 4 is now closed.** `status_for` and `report_value_error` in `//src/ffi` (`serve_image.{h,cpp}`), which is where the seam's `SipiStatus` (`serve_response.h`) and `SipiImageErrorReport` (`sipi_ffi.h`) actually live — putting them in `//src/error` would have inverted the dependency direction, since the foundational leaf cannot see the seam's vocabulary. Additive: `build_image_response` and **every existing `catch` clause are untouched**, so the helpers' only callers today are their tests. Two decisions worth recording. (a) `status_for` switches on `policy_for(...).http_status_class`, **never on a raw `ErrorCode`** — that indirection is the entire point of the policy table, and a seam switching on codes would rebuild the scattered dispatch this work removes. (b) `report_value_error` delegates the struct flattening to the existing anonymous-namespace `report_image_error` rather than duplicating it, and passes `diagnostic_message()` — matching the existing call sites, which pass the unredacted `to_string()` form, so the text reaching Sentry will not move when the seam migrates. The `MetricHint` third of the policy got **no** helper: it has no clean single conversion today and belongs to Phase 8. `//src/error` became an explicit dep of `//src/ffi:sipi_ffi` (it was arriving transitively through `//src/image`). Orchestrator sent the chunk back once: three comments described the new code *relative to the exception-era catches they replace*, which rots the moment those catches are deleted later in this same PR; all three were rewritten to state the enduring rule instead. `just bazel-test` **73/73**. |
| 5C | done | `c520e8e9` | **Phase 4's foundation.** `SipiValueError` + `ErrorCode` (the seven variants `error-model.md` documents, no more) + `HttpStatusClass`/`SentryPolicy`/`MetricHint`/`ErrorPolicy` + `constexpr policy_for` + `Result<T>`, all in one header at `src/error/cpp/SipiValueError.h`. **Header-only** — a value type that formats nothing at construction has nothing to put in a TU. Additive: not one caller migrated, and no file outside `//src/error` + `ARCH-MAP.md` was touched. Three things worth the reviewer's eye. (a) `policy_for` is an exhaustive `switch` with **no `default`**, so a new `ErrorCode` without a policy fails to compile under `-Wswitch` rather than silently inheriting one; the test asserts the exact table values on top of that. (b) `client_message()`/`diagnostic_message()` reproduce `SipiImageError::message()`/`to_string()` **verbatim**, including the `(system error: …)` splice — which is why the type carries an `errnum` the ADR's field list does not name. That is a deliberate addition for behavior preservation: without it, every migrated `throw SipiImageError(msg, errno)` site would change its emitted text. Unlike `SipiImageError` it does **not** cache a formatted string, so it stays cheap to return by value. (c) `HttpStatusClass` has exactly one variant today (`kInternalError`) because every documented `ErrorCode` maps there; no speculative variants were invented, and the header says why the enum exists at all. `ARCH-MAP`'s `error` entry updated in the same commit — the map and the package it describes land together. `just bazel-build` green, `just bazel-test` **73/73** (up from 72; the new `//src/error:error_test` joins the `//src/...` sweep). |
| 5B | done | folded into `6509ccc4` (was `fad85de0`) | **The sibling flake, hardened on the maintainer's extended ruling.** `timeout_kill_after_commit_aborts_the_stream` (`test/e2e/tests/lua_hardening.rs`) carried the *identical* head-vs-abort race as its already-fixed sibling — `.expect("GET failed")` panics when the deadline kill wins the race against the response head flushing. It now `match`es on `send()` and accepts either ordering, structurally mirroring `script_error_after_commit_aborts_the_stream`, with the assertion's teeth intact: `Ok` + 200 + a cleanly-terminated `bytes()` read is still a hard failure. Doc comment rewritten for deadline-kill wording rather than copied from the sibling. **No retry loop, no `#[ignore]`, `.bazelrc`'s `--flaky_test_attempts` allowlist untouched** — the maintainer's rationale is to kill the flake at the root, not to paper over it. `bazel test //test/e2e:lua_hardening` green, then 3 further uncached runs (`--nocache_test_results --runs_per_test=3`) green; `just bazel-rustfmt-check` + `just bazel-clippy-check` green. Folded rather than committed standalone because it is the same fix to the same class of bug in the same file. |
| 5A | done | folded into `8afd88da` (was `7b7d5480`) | **ADR-0024 accepted.** `status: proposed` → `accepted`. The maintainer affirmed the ADR's own recommendation on its single contested point, so Decision 6's PNG block was rewritten from a recommendation-plus-escape-clause into a settled decision: PNG adopts libpng's sanctioned throw-from-error-callback converted to `Result` at the handler boundary, which removes the `longjmp`-skips-destructors bug class for PNG entirely (the RAII-before-`setjmp` discipline and the volatile-locals footgun stop applying there). JPEG's half is untouched — libjpeg has no sanctioned escape hatch, so it keeps the setjmp landing-site pattern, and the two rules that must survive the rewrite (volatile locals, `jerr.error_message` stays `char[]`) stand. The uniform-with-JPEG alternative moved from "not rejected — the maintainer's call" to a properly rejected Considered Option with its one-line reason, which is where a settled ADR keeps a road not taken. The file was swept for any other pending-decision framing; the one remaining "genuinely contested" (line ~289) is a factual statement about the wider C++ community's exceptions-vs-values debate, not an open decision here, and was deliberately left. Docs-only. |
| 4j | done | `7b7d5480` | **Phase 3 — the error-strategy ADR.** `docs/adr/0024-value-based-image-errors.md` (322 lines, `status: proposed`) records all nine decisions with reasoning: canonical `Sipi::SipiValueError` living in `//src/error` (the name collision with the existing `Sipi::SipiError` is why it is not simply "SipiError" — that type is still actively caught for HTTP-400 dispatch and stays untouched); `ErrorCode` + a `constexpr policy_for()` table turning the seams' catch-by-type dispatch into a data lookup that reproduces **all four** current policies, not a client-safe/diagnostic split; `Result<T>` alias colocated in the same header; internals-first-then-one-vtable-flip; J2K → PNG → JPEG → TIFF with rationale; per-codec longjmp bridges incl. the volatile-locals footgun and the "`jerr.error_message` must stay a `char[]`" warning; `std::bad_alloc` permanently exception-based; the `Result`→throw adapter as intermediate-commit-only state. `docs/src/development/error-model.md` created — `SipiImageError.h`'s long-dangling docstring reference already pointed at exactly that path, so it now resolves with no header edit. Wired into `docs/mkdocs.yml` nav. Frontmatter matches the house shape (bare `status:`), verified against ADR-0007/0021/0023. Docs-only; `just bazel-build` green. |
| 4i | done | `00625ba1` | **Phase 2's closing chunk.** ARCH-MAP gained an `image_processing` component and the three boundary invariants recorded as `bazel query` checks — flagged in-document as **CI-verifiable, not locally runnable** (a query universe reaching `@google_benchmark` cannot fetch `@libpfm` on this DNS-less box), which is the honest framing rather than claiming a run that did not happen. `UBIQUITOUS_LANGUAGE.md` gained the **Image processing** entry (ADR-0007's pending glossary delta); CLAUDE.md and CONVENTIONS.md tables gained the package and its commit scope; `codecov.yml` lost a `src/ffi/SipiLua.cpp` entry that predates this branch; `tools/format-handlers-fanout.sh` was genuinely broken by an earlier move and is fixed. ADR dispositions applied per the maintainer's status-dependent rule and recorded in full below. `just bazel-test` **72/72 green**. |
| 4h | done | `c7f35790` | `src/server-rs/` → `src/server/rust/`, `src/cli-rs/` → `src/cli/rust/`. **No `-rs`-suffixed folder remains anywhere under `src/`**, and `src/cli/` is now genuinely polyglot (`cpp/` + `rust/`), the `iiifparser`/`throttling` shape. Label churn reached far past the two packages — Docker image layers in `src/BUILD.bazel`, the crates hub in `MODULE.bazel`, `justfile`, the e2e harness (`sipi_e2e_test.bzl` + tests), `rustfmt.toml`, `bazel/mimalloc.BUILD.bazel`, and a prose line in `bazel/patches/libmagic_have_strndup.patch`. That last one was checked by the orchestrator: the edited line sits in the patch's *descriptive preamble*, above the hunks, so patch application is unaffected (and libmagic builds). **`_ALLOCATOR` and the malloc-override shim moved untouched** — production allocator selection is unchanged, which matters given the RSS/OOM history. Commit-scope vocabulary followed: `server-rs` → `server`, `cli-rs` folded into `cli`. There is no commitlint scope allowlist (`.commitlintrc.yml` deliberately has none), so that is a docs-only change. `just bazel-test` **72/72**, `rustfmt-check` + `clippy-check` green (re-run by the orchestrator). |
| 4g | done | `e137daf1` | `ffi` and `cli` converted to `cpp/`, finishing the C++ half of the layout sweep; `//src/cli/commands` → `//src/cli/cpp/commands`. **`//src/cli:sipi`, `//src/cli:sipi_report` and `//src/ffi:sipi_ffi` keep their labels** because the BUILD files stay at the package roots — that is what spares the Docker image targets, the CI workflows, the justfile and the Rust shell's build from any change at all. One consumer file touched (a path reference in `iiif_transform_matrix_test.cpp`); otherwise zero include churn again. `just bazel-test` **72/72 green**. Worker flagged a **pre-existing** dead reference it correctly left alone: `codecov.yml:26` still lists `src/ffi/SipiLua.cpp`, a file deleted long before this branch — folded into the docs-scrub chunk. |
| 4f2 | done | `913ef070` | `include/VideoHD.icm` deleted per the maintainer ruling. Kept a **separate commit** from `1c27fab7` deliberately: an orphan-file deletion and a package-layout sweep fail the "And" test, and the maintainer's ruling explicitly allowed a small `chore` of its own. `include/` now holds only live build inputs (`SipiVersion.h.in`, `SipiConfig.h.in`, `ICC-Profiles/`). |
| 4f | done | `1c27fab7` | `util`, `logging`, `observability`, `metadata` converted to the `cpp/` subfolder layout; `//src/metadata/internal` → `//src/metadata/cpp/internal` (subpackages live *beneath* the language folder, per the `//src/iiifparser/cpp/value_objects` precedent). **Zero cross-package `#include` churn** — verified by the orchestrator with a `git diff --stat` over every consuming package, which came back empty. That is the whole point of the `strip_include_prefix = "/src/<pkg>/cpp"` + `include_prefix = "<pkg>"` twinning, and it had to be applied *per package* because the three started from three different attribute shapes (`util` had `strip_include_prefix = "/src"`, `metadata` had neither, `logging` had only `include_prefix`). `essentials.proto` and the generated-proto include path followed the move. `just bazel-test` **72/72 green**. |
| 4e | done | `856cc991` | `test/unit/sipiimage/` **dissolved** and its 19 tests colocated per ADR-0003: 8 codec regressions → `//src/format_handlers`, 6 processing regressions → `//src/image_processing`, 4 (incl. `sipiimage.cpp` → `sipiimage_test.cpp`) → `//src/image`, 1 → `//src/metadata`. Two landed away from where their filename suggests, both verified by the orchestrator against the files: `imginfo_test.cpp` instantiates the four concrete handlers and tests each one's `read_shape()`, so it is a `format_handlers` test, not an `image` one; `essentials_prefix_invariant_test.cpp` drives `SipiImage::write()` through the real JP2/TIFF writers to assert byte-layout placement (ADR-0004), not `metadata`'s parse/serialize logic, so it is an `image` test. The custom `main.cpp` was inert boilerplate → `@googletest//:gtest_main`; fixture generators moved up to `test/unit/fixtures/` so every package reaches them without duplication. `just bazel-test` **72/72 green** (up from 70 — the colocated targets now ride the `//src/...` sweep). No stale `//test/unit/sipiimage` or `sipi_image_tests` label remains outside `CHANGELOG.md`, which is a historical record and correctly untouched. |
| 4d | done | `fad85de0` | **Maintainer-directed fix to a `main`-owned flake**, outside this plan's original scope (see the ruling below). `script_error_after_commit_aborts_the_stream` (`test/e2e/tests/lua_hardening.rs`) assumed the response head always reaches the client before the Lua abort lands, so it read the abort as a failing *body* read after a successful `send()`. The two race; when the abort wins, `send()` itself fails with `hyper::Error(IncompleteMessage)` and the test panicked on `.expect("GET failed")` — the darwin-arm64 failure seen in run `33188874637`. Now `match`es on `send()` and accepts either ordering, **keeping the assertion strong**: `send()` ok + 200 + `bytes()` ok (a complete, cleanly-terminated body) is still a hard failure, which is the property the test exists to defend. No retry loop, no `#[ignore]`, and `.bazelrc`'s `--flaky_test_attempts` allowlist deliberately untouched — retrying would paper over the race instead of describing it. Standalone `fix:` because the bug exists on `main` (the in-branch fold-in rule does not apply). `bazel test //test/e2e:lua_hardening` green over 5 uncached repeat runs; `just bazel-rustfmt-check` + `just bazel-clippy-check` both green (re-run by the orchestrator, not just the worker). |
| 4c | done | `ae0a68f4` | **Watermark / arithmetic / comparison** subset extracted into `cpp/compose.cpp`, completing the extraction: `add_watermark`, `compare`, `maxPixelDelta` as `Sipi::processing` free functions; `operator-=`, `operator-`, `operator+=`, `operator+`, `operator==` as free operators in **plain `namespace Sipi`, not `Sipi::processing`** — `SipiImage` lives in `Sipi`, so ADL finds them there and every `a - b` / `a == b` call site compiles unchanged; putting them in `processing` would have forced a `using`-directive at each site. `read_watermark`'s declaration moved to `processing.h` (see the boundary ruling below) and the `bilinn` forward-declaration hack from `4a` is gone. `process_benchmark.cpp` moved to `src/image_processing/cpp/` with its `cc_binary`, so **the `src/` root now holds only `BUILD.bazel` and `nsswitch.conf`**; `justfile`'s `bench` tier map gained a `process) pkg="src/image_processing"` arm. 22 files. `just bazel-test` **70/70 green**, approval goldens byte-identical. Worker also dropped a dead `POSITION` macro that bracketed `add_watermark` but was never expanded (verified against `git show`) and two now-unused includes. |
| 4b | done | `c18b8da4` | **Colour / channel / bit-depth** subset extracted into `cpp/color.cpp`: `convertYCC2RGB`, `convertToIcc`, `removeChannel`, `removeExtraSamples`, `to8bps`, `toBitonal`. `removeExtraSamples` had to move even though the disposition table does not name it separately — it was an *inline header* method calling `removeChannel`, so leaving it on `SipiImage` would have pointed `//src/image` at `//src/image_processing` and closed exactly the cycle ADR-0007 rules out. In-place ops use `pixels_writable()`; geometry-changing ops go through one `set_pixels()`; `setEs`/`setPhoto`/`set_icc` carry the metadata each operation changes alongside its buffer. `image_processing` gained `//src/metadata` + `@lcms2` deps and `SipiImage.cpp` dropped its now-unused `lcms2.h` include. ICC emission still funnels through `Icc::iccBytes()` (CLAUDE.md invariant). 16 files; `just bazel-test` **70/70 green**, approval goldens byte-identical. |
| 4a | done | `44c94ea8` | `//src/image_processing` created and the **geometry** subset extracted: `crop`×2, `scaleFast`, `scaleMedium`, `scale`, `rotate`, `set_topleft`, plus the two private `bilinn` statics. One umbrella header `cpp/processing.h` (`namespace Sipi::processing`) + `cpp/geometry.cpp`; BUILD at the package root with `include_prefix = "image_processing"` / `strip_include_prefix = "/src/image_processing/cpp"`. `resample.{cc,h}` moved along with its only callers and its `HWY_TARGET_INCLUDE` literal was rewritten by hand; `SipiImage.cpp` dropped the `resample.h` include, so **`//src/image` gained no edge to `//src/image_processing`**. `//src/format_handlers`, `//src/ffi`, `//src/cli`, `//src/cli/commands`, the benchmark and the tests gained the explicit dep. 28 files, −1046/+139 before the new package's own lines. `just bazel-test` **70/70 green**, approval goldens byte-identical. Two things worth the reviewer's eye, both reported by the worker and verified by the orchestrator against `git show HEAD~1`: (a) `bilinn` is still needed by `add_watermark`, which stays on `SipiImage` until chunk `4c`, so it has external linkage inside `Sipi::processing` and is forward-declared in `SipiImage.cpp` — the same link-time asymmetry already used for `SipiImage::io` and `read_watermark`, and it disappears when `add_watermark` moves; (b) `crop`/`rotate`'s `else` branch (bps neither 8 nor 16, a "clean up and throw exception" stub that was never filled in) used to assign `nx`/`ny` *without* resizing the buffer — a latent desync. `set_pixels()` makes buffer and geometry atomic, so that branch now leaves geometry untouched and still returns `true`. Unreachable in practice (`bps` is 8 or 16 everywhere), and the goldens confirm no reachable behavior moved. |
| 3b | done | `865f2c25` | Public mutator surface on `SipiImage`, purely additive — no existing method body touched, so no hot-path change and no benchmark run required. `set_pixels(vector&&, nx, ny, nc, bps)` is the sole resize path and routes its size check through the existing `checked_buf_size_or_throw`; `pixels_writable()`/`pixels_view()` return `std::span`, *not* `std::vector<byte>&`, so a caller cannot resize/clear/reassign and desynchronize the buffer from the geometry. Derivation the worker reported: `set_pixels` ← the nine bulk-replace methods (`convertYCC2RGB`, `convertToIcc`, `removeChannel`, both `crop`s, `scaleFast`/`scaleMedium`/`scale`, `rotate`, `to8bps`); spans ← `toBitonal`/`add_watermark` (in-place) and `compare`/`maxPixelDelta`/`operator-=`/`operator+=` (incl. `rhs` access); `getEs()`/`setEs()` ← `removeChannel`'s `es.erase(...)`; `setPhoto()` and `set_icc()` ← `convertToIcc`. **No `setNx`/`setNy`/`setNc`/`setBps`** — every listed method changes those only alongside a buffer replace, which `set_pixels` already covers atomically. `just bazel-build` + `just bazel-test` 70/70 green, approval goldens byte-identical. |
| 3a | done | folded into `8826293d` | **Fix for a branch-introduced CI break** (no standalone commit — the bug never existed on `main`). `oci_image` target `//src:image` → `//src:sipi_image`, unblocking the `//src/image` package that chunk `2k` created; see CI findings §1 for the output-path-prefix rule that forced it. 12 files: `src/BUILD.bazel` (target name + the three `image = ":image"` attributes on `image_push_amd64`/`image_push_arm64`/`image_load`), `justfile`, `.github/workflows/ci.yml`, `bazel/distroless/bookworm.yaml`, `CLAUDE.md`, `CONVENTIONS.md`, and five living docs under `docs/src/development/`. `image_load`, `image_push_*`, `image_created`, `image_labels` deliberately keep their names — their output paths are not prefixes of `src/image/…`. `docs/adr/**`, `docs/specs/**`, `docs/archive/**` left frozen for the docs-scrub chunk. `bazel query 'kind("oci_image rule", //src:*)'` = `//src:sipi_image` alone; `just bazel-build` + `just bazel-test` 70/70 green. |
| 2l | done | `ab1992e4` (was `0a1b4b1a`) | `formats` → `format_handlers`, sources into `cpp/`, `fuzz/` + `corpus/` as siblings; `strip_include_prefix = "/src/format_handlers/cpp"` + `include_prefix = "format_handlers"` twinning applied to both targets. 66 files; every moved source byte-identical apart from include lines except `output_sink.h` (one prose path in a comment) and `format_registry.cpp` (prose label). `tools/formats-fanout.sh` → `tools/format-handlers-fanout.sh`; `justfile`, `fuzz.yml`, and the dev docs follow. Scope vocabulary retires `formats`. Build + 70/70 + the 4 fuzz corpus-replay tests green. |
| 2k | done | `8826293d` (was `6e0c8966`) | `//src:engine` dissolved into `//src/image` (`src/image/cpp/`): `SipiImage.{h,cpp}`, `SipiIO.h`, `SipiImageError.h`, `populate_from_image.{h,cpp}`, `resample.{cc,h}`. 71 files touched, all 8 moved files **byte-identical** (verified by `diff` against `HEAD`) except one line — `resample.cc`'s `HWY_TARGET_INCLUDE` needed the physical path `src/image/cpp/resample.cc`, because Highway's `foreach_target` re-include resolves on disk, not through the virtual `image/` prefix. `:sipi_lib`'s root glob is now empty and carries `allow_empty = True`. `src/` root holds only `BUILD.bazel`, `nsswitch.conf`, `process_benchmark.cpp`. Build + 70/70 tests green, approval goldens byte-identical; Rust gates run too (a `src/server-rs/BUILD.bazel` comment was touched). |
| 2j | done | `d7c24ec0` | Follow-on cleanup that 2i created: `//include:headers` had zero reverse deps, so the `cc_library` and its last file (`favicon.h`, which no C++ TU ever included) are deleted; the package keeps only its `exports_files` labels. Also fixed two comments (`src/server-rs/BUILD.bazel`, `lib.rs`) that described the Rust favicon handler in terms of the removed C++ oracle — a production-surface-rule violation independent of this work, found by the worker. Rust touched → `rustfmt-check` + `clippy-check` run and green, plus build + 70/70 tests. |
| 2i | done | `44d3833f`, `80ba4149` | Two commits. (a) `SipiConf.{h,cpp}` → `src/ffi/` (header came out of `include/`); consumers use `ffi/SipiConf.h`. (b) `SipiReport.{h,cpp}` → `src/cli/` as a new `:sipi_report` leaf target — *not* folded into `:cli_app`, because `:cli_app` deps one-way on `//src/cli/commands` and `convert_access_file.cpp` also reports, so a shared dep in `cli_app` would cycle. Consequence: `//include:headers` now has **zero** reverse deps and the five targets carrying it dropped it. `just bazel-build` + `just bazel-test` 70/70 green. |
| 2h | done | `d31d1e03`, `ef6c55a3` | Two commits. (a) `SipiCommon.{h,cpp}` deleted — both were empty TUs; the JPEG handler's include of it dropped. (b) `SipiFilenameHash.{h,cpp}` moved into `//src/util` unchanged; consumers now `#include "util/SipiFilenameHash.h"`. `util` gains `//src/logging` as its first internal dep (`log_debug` calls), so it is no longer a dependency-free leaf — ARCH-MAP's boundary rule updated to say so rather than papering over it. `just bazel-build` + `just bazel-test` 70/70 green. |
| 2g | done | `08305d8e` | `//src/cache` carved (`src/cache/cpp/SipiCache.{h,cpp}`); dropped from `:engine`'s srcs/hdrs and the `:sipi_lib` glob exclude; `//src/ffi:sipi_ffi` gained the explicit dep it had been inheriting. `somepath(//src/cache, //src:engine)` empty; first-party deps = {cache, error, logging, observability, util}. `just bazel-build` + `just bazel-test` 70/70 green. `test/unit/cache` stayed on `//src:sipi_lib` — `//src/cache`'s `//src:__subpackages__` visibility does not reach `//test/unit/...`, and widening it is a policy call, not a mechanical one. |
| 2f | done | `848a1b32` | `//src/error` carved (`src/error/cpp/SipiError.{h,cpp}`, files byte-identical); `//src:sipi_top` deleted and all consumers repointed; cross-package includes now `error/SipiError.h`; `ARCH-MAP.md` gained an `error` component + `CONVENTIONS.md` an `error` scope row. `bazel query 'deps(//src/error)' --output=package` = {`src/error`, `src/util`} first-party only. `just bazel-build` + `just bazel-test` 70/70 green. |
| 2d | done | `3eb952b5` | `CONVENTIONS.md` § Directory layouts gained the language-subfolder rule (`cpp/`/`rust/`, no `-rs` suffix, `fuzz/`/`corpus/` as package-level siblings) and the `strip_include_prefix`/`include_prefix` twinning. First worker output over-generalized ("an unsplit package sets neither attribute"); corrected on re-spawn against the real BUILD files — `util`/`formats`/`ffi` set `strip_include_prefix = "/src"`, `metadata` sets neither, `logging` sets only `include_prefix`. |
| P0 | done | (no code) | Preconditions verified by the session: wave-1 on main; `git lfs pull` done (fixtures are real bytes). |
| 2c | done | `31fc50a0` | Raw-pixel-use audit → `02-raw-pixel-use-audit-design.md` (332 lines). Seams/CLI/tests/benchmarks are already public-API-only, so the question is confined to the four handlers + the processing methods; their access collapses to 9 patterns. Proposes a narrower surface than the ADR's opening `pixels_writable()` (bulk setter + `std::span` accessors + targeted setters), because a mutable `vector&` reference re-opens everything the friendships did. |
| 2b | done | `142e6161` | Retargeted 9 stale `../deep-modules.md` links in ADR-0004/0006/0007 to `../archive/2026-05-08-modularization-analysis.md`; every fragment anchor verified against a real heading. Opportunistic — found while verifying 2a's links, fixed repo-wide rather than in one file. |
| 2a | done | `a810f36e` | ADR-0007 → `status: accepted`, brought to current fact (vector pixels, precise friend description, `image_handle.cpp`/`bindings/image.rs` replacing the deleted `SipiLua.cpp`, Probe 6 facade question resolved as "no facades — a facade cycles"), plus the full disposition table, no-`engine/`-folder + standalone-`//src/error` reasoning, the `read_watermark` two-option boundary, and the three `bazel query` invariants. |
| 1f | done | `7e492536` | `fuzzing.md` restructured around two harness families + new crash-triage section; `CONVENTIONS.md` `formats` scope row extended. Full sweep after this chunk: `just bazel-test` = 70/70 pass. No Rust touched all round, so the rustfmt/clippy gates do not apply. |
| 1e | done | `10d150f3` | `malformed/j2k_oversized_dimensions.jp2` (730 B, `ycbcr16.jpx` with `ihdr` + `SIZ` patched to 262144px), registered as a J2K fuzz seed, documented in the malformed README, pinned by a new unit test. Rejection message observed end-to-end: `Image dimensions exceed the supported decode limit`. |
| 1c+1d | done | `9b01a82e` | Vendored AFL++ tiff/jpeg/png dicts; `justfile` fuzz recipes rebuilt around one six-column target table covering all five harnesses; `fuzz.yml` fanned out to a 5-leg matrix (`fail-fast: false`), LFS on, job-level `GH_TOKEN`, per-target `fuzz-corpus-`/`bep-fuzz-`/`fuzz-crashes-` artifacts, `detect_leaks=0` on jpeg/png ASan legs only. Verified locally: `just fuzz tiff` runs at ~1500 exec/s with the dict loaded, 7179 edges, no crashes in 30 s. |
| 1a+1b | done | `bc3cac6a` | `src/formats/fuzz/` — shared `codec_fuzz_harness.h` + four `cc_fuzz_test` targets (tiff/jpeg/png/j2k), four `src/formats/corpus/<fmt>/` packages, four `fuzz_seeds_<fmt>` filegroups in `test/_test_data/BUILD.bazel`. All four corpus-replay tests green on macOS; `just bazel-build` green; `just commit-lint` clean. |

## Maintainer rulings

- **ADR path-scrub policy is status-dependent, not a blanket freeze**
  (maintainer, round 3; supersedes the round-2 "freeze all historical ADRs"
  reading). For each ADR that still references pre-Phase-2 paths:
  - If the ADR is **still active** (`status: accepted`, not deprecated or
    superseded) **and** the stale path would confuse a reader or an AI agent
    following it today — i.e. updating it is necessary for the ADR to make sense
    — **update** the path references to the new layout.
  - If the ADR is **deprecated/superseded**, or the stale path is incidental
    historical context that does not affect comprehension, **leave it frozen**.

  Judgment is applied **per ADR** during the docs-scrub chunk, and the
  **per-ADR disposition is recorded here**. Phase 2's "docs scrubbed repo-wide"
  checkbox is read against this rule, not against a blanket exemption. The rule
  itself belongs in the repo wherever the docs-scrub convention is written down.

  Known candidates carried into the scrub chunk: ADR-0003, 0005, 0019, 0021
  (all still say `src/formats`); ADR-0007 line 64 (describes `//src/error` via
  the deleted `//src:sipi_top`); ADR-0014 line 78 and ADR-0015 line 71 (both
  cite `//src:image`, renamed this round, *and* an already-stale
  `//bazel/platforms:linux_amd64` — the platform package is now `//platforms`,
  so these two are doubly stale and want one deliberate pass, not a one-word
  patch). `docs/archive/**` and `docs/specs/**` stay frozen regardless — they
  are records of a moment, not instructions.

- **`checked_buf_size` in `SipiIOJ2k.cpp` stays exactly as it is** (maintainer,
  round 4; **final**). Round 3's side finding observed that
  `validate_decode_dims` (`SipiIOJ2k.cpp:520`) caps dimensions at `kMaxDecodeDim`
  and channels at 32, which makes the downstream overflow-checked multiply
  (`:700/717/741`) unreachable on 64-bit, and asked whether that collides with the
  repo's no-defense-in-depth rule. **It does not.** This is a security-motivated
  overflow guard at a C-library boundary, and the two checks guard *different
  contracts* — the dimension cap is a policy limit on what SIPI agrees to decode,
  the buffer-size check is an arithmetic-safety property of the multiply itself.
  Neither is redundant with the other.

  **Do not "simplify" either guard away.** This matters concretely: **Phase 5a
  (DEV-7063) converts this exact file's internals to `Result`** and will be
  reading these lines closely. The ruling is recorded here so that round does not
  re-litigate it.

- **`include/VideoHD.icm` is deleted** (maintainer, round 4; **final**). The
  round-3 side finding established it is a true orphan — no BUILD target, no
  source, no current doc references it; the only mentions are two in the archived
  2026-05-08 modularization analysis. Git history preserves the bytes, so it is
  removed outright rather than carried. Folded into the mechanical `cpp/`-sweep
  chunk, which is where the `include/` tree's remaining contents are being settled
  anyway.

- **`//test/e2e:lua_hardening` darwin flake is fixed on this branch**
  (maintainer, round 4; supersedes round 3's "main-owned, not this plan's scope"
  and the round-4 brief's "do not fix it"). The named test
  `script_error_after_commit_aborts_the_stream` was made tolerant of the
  early-truncation ordering, as a standalone `fix(e2e):` — the bug exists on
  `main`, so the fold-into-the-introducing-commit rule does not apply. Landed as
  chunk `4d` (`fad85de0`).

  **Sibling resolved** (maintainer, round 5; **final**). The ruling extends to
  `timeout_kill_after_commit_aborts_the_stream` (same file, ~line 84), which
  carries the identical head-vs-abort race: *"kill the flake at the root"* applies
  to both. It was hardened the same way and **folded into the same commit**
  (`fad85de0` → `6509ccc4`) rather than committed separately, since it is one fix
  to one class of bug in one file. Landed as chunk `5B`. Nothing in
  `test/e2e/tests/lua_hardening.rs` now carries the bare `.expect("GET failed")`
  shape.

- **ADR-0024 is accepted, with its own recommendation affirmed on the contested
  point** (maintainer, round 5; **final**). PNG uses libpng's sanctioned
  throw-from-error-callback (`png_set_longjmp_fn` mechanism) converted to
  `Result` at the handler boundary; JPEG keeps the setjmp landing-site pattern.
  The acceptance was **folded into the ADR's introducing commit**, so the ADR
  never appears on `main` as `proposed` — the branch ships one commit that adds
  an accepted ADR. Phases 4–8 are unblocked. Landed as chunk `5A`.

- **The `BM_ScaleHigh` finding gets a real A/B, and a decision rule to read it
  by** (maintainer, round 5). Building a temporary `git worktree` at the
  pre-extraction commit `865f2c25` is explicitly authorized for this purpose (and
  is the one sanctioned exception to staying inside this worktree; it is removed
  when done). The rule given: (a) similar delta with provably identical source
  semantics → record as a binary-layout effect and accept; (b) parity → the
  earlier baseline was stale/machine drift, record and accept; (c) only a delta
  above ~5 %, or one with a per-call-overhead signature (constant across `/256`
  and `/1024`), is a blocker. **Explicitly forbidden: alignment or `copt` hacks
  to chase layout luck.** Result recorded in the benchmark section below.

- **`redact_paths` is fixed on this branch** (maintainer, round 6; **final**).
  The round-5 side finding (unbalanced quote in client-facing text + a doc
  comment describing behavior the function does not have) is main-owned, but
  error-text correctness is this plan's subject and the standing pattern from
  the `lua_hardening` ruling is to fix a main-owned bug surfaced by this work at
  the root. Landed as chunk `6A` (`98e64b52`), one standalone `fix(error):`
  carrying both halves — the behavior and the doc comment — because they are one
  disagreement between code and comment, not two changes.

- **ADR-0024 Decision 6 is reversed for PNG** (maintainer, round 7; **final**).
  PNG keeps `setjmp`/`longjmp` via `png_jmpbuf`; the post-`setjmp` landing block
  returns `std::unexpected(...)` instead of throwing — exactly as safe as
  today's pattern, since the safety property belongs to `setjmp`/`longjmp`
  itself and not to what the landing block does with control afterward. The
  RAII-before-`setjmp` discipline and the volatile-locals rule therefore keep
  applying to PNG. Grounds: (1) the shipped fix `94f45a28` (2026-04-04) removed
  throw-through-libpng as exception-through-C UB — evidence the ADR did not
  have; (2) libpng is a BCR C build (`MODULE.bazel`, 1.6.54) with no guarantee
  of unwind tables, and vendoring it solely to make throw propagation safe is
  rejected; (3) uniform treatment with JPEG's identical hazard
  (`SipiIOJpeg.cpp:86`) is more legible than opposite treatments for the same
  hazard. The ADR was amended by **folding into its introducing commit**, so it
  ships stating only the settled decision. The defensive comments at
  `SipiIOPng.cpp:120-126` stay in place. Landed as chunk `7A`.

- **The JPEG volatile-locals violations are fixed on this branch** (maintainer,
  round 8; **final**). Same standing pattern as `lua_hardening` and
  `redact_paths`: a main-owned bug surfaced by this work is fixed at the root,
  as one standalone `fix(format_handlers):`, never folded into a `refactor:`.
  Covers `icc_buffer_guard` and `html_buffer` — the same defect family in the
  same file. PNG's `http_ctx` was confirmed benign and left alone. Landed as
  chunk `8B` (`767d7a02`), which found the encode half to be **four** objects
  rather than one; see the chunk row. The rule the commit body states: any local
  the `setjmp` landing block reads must be `volatile` or be assigned outside the
  risk window.

- **The `log_vformat` signature bug is fixed on this branch** (maintainer,
  round 8; **final**). Main-owned, own `fix(logging):` commit, same pattern.
  Landed as chunk `8C` (`940d8528`).

- **ADR-0024's TIFF rationale is corrected, folded into its introducing
  commit** (maintainer, round 8; **final**). Decision 5's TIFF bullet stated the
  "malformed UTF-8 caused segfaults" folklore as fact. The evidence — a
  va_list-vs-varargs ABI mismatch in a line that never compiled; `2b60176a`
  silenced the handlers, `b2899d74` added the safe `log_vsformat` primitive six
  days later; no UTF-8 evidence anywhere in the repo — replaces it. The
  **decision is unchanged**: TIFF migrates last and its callbacks are re-enabled
  with formatting safety. Folded rather than appended so the ADR ships on `main`
  stating only the true rationale. Landed as chunk `8A`.

- **The missing PNG decode benchmark is an accepted gap for this PR**
  (maintainer, round 8; **final**). `@sipi_bench_fixtures` ships no PNG master
  and regenerating the 321 MB pinned archive is a scheduled maintainer action,
  not branch work. Phase 5b's gate stands on the encode tier plus the structural
  argument that every retyped return is on a failure path. Recorded as an
  explicit gap in Deferrals.

## ADR path-scrub dispositions (round 4, chunk `4i`)

Applied per the maintainer's status-dependent rule. This is the record that rule
asks for — Phase 2's "docs scrubbed repo-wide" checkbox is read against it.

| ADR | disposition | reason |
|---|---|---|
| 0003 | **updated** | One `src/formats` mention → `src/format_handlers`. Status is `proposed`, but the colocation convention is actively in force (this round executed it), so a stale path would mislead. |
| 0005 | **updated** | `src/formats/SipiIOJ2k.cpp` → `src/format_handlers/cpp/SipiIOJ2k.cpp`. Active, and the path is load-bearing for a reader following the Essentials packet. |
| 0007 | **updated** | All live-prose `formats` mentions → `format_handlers`; the `//src:sipi_top` line reframed as historical (that target is dissolved). **The "Source today" disposition table is left frozen** — it is an explicit point-in-time before/after record, and "correcting" the before column would destroy its meaning. |
| 0014 | **updated** (deliberate pass) | Doubly stale: `//bazel/platforms:linux_{amd64,arm64}` → `//platforms:linux_{x86_64,aarch64}` **and** `//src:image` → `//src:sipi_image`. Reread rather than word-patched, per the ruling. |
| 0015 | **updated** (deliberate pass) | Same two staleness axes as 0014, same treatment. |
| 0019 | **updated** | `src/SipiCache.cpp` / `src/formats/SipiIOJ2k.cpp` → current paths. Active allocator ADR; the paths point at real evidence. |
| 0020 | **frozen** | Its `src/shttps/` references *are* its subject — it records the oracle's removal. Updating them would be incoherent. |
| 0021 | **updated** | `src/formats` example → `src/format_handlers`; `//src:engine` / flat `src/SipiIO.h` / `src/SipiImage.cpp` → `//src/image` + `//src/format_handlers` and the prefixed include forms. It is the polyglot-colocation ADR this round kept citing as precedent, so it has to describe the tree that exists. |
| 0022 | **frozen** | `//src:engine` appears once as incidental lineage ("carved out of"). Does not affect comprehension of the admission-control decision. |

`docs/archive/**`, `docs/specs/**` and `CHANGELOG.md` stayed frozen throughout,
as records of a moment rather than instructions.

## CI findings — round 3 triage

Three CI failures were open at round start. All three were root-caused from the
real job logs before any code was touched.

1. **`//src:image` vs `//src/image` output-path collision — REAL, ours,
   blocking.** Chunk `2k` (`6e0c8966`) created the Bazel package `src/image`.
   Bazel forbids one action's output path being a prefix of another's, and the
   `oci_image` target named `image` in package `src` declares the output
   *directory* `bazel-out/<cfg>/bin/src/image` — a strict prefix of every output
   of `//src/image:image` (`libimage.a`, `image.cppmap`, `_virtual_includes/…`).
   Both Linux legs of run `33188874637` (head `0a1b4b1a`) failed at analysis.
   **macOS never sees it**: `//src:image` is `target_compatible_with`-gated to
   Linux and is skipped there, so a fully green local run cannot catch this
   class of defect. Fixed in chunk `3a` by renaming the OCI target to
   `//src:sipi_image` (see below); the C++ package keeps the name the plan and
   ADR-0007 mandate. Only the bare `image` target collides — `image_load`,
   `image_push_*`, `image_created`, `image_labels` all have output paths that
   are *not* prefixes of `src/image/…` and keep their names.

2. **`//test/unit/sipiimage:sipi_image_tests` TIMEOUT under asan-ubsan —
   infra, not ours; no history rewrite.** Run `33177146450` (head `31fc50a0`),
   shard 4 of 8, 1259.9 s. The round-2 prime suspect was the new J2K
   oversized-dimension fixture test (`10d150f3`). **Disproved:** that test runs
   in **0.2 s** locally, because the fixture is rejected by
   `validate_decode_dims()` at header validation, before any decode or buffer
   sizing — there is no work for ASan to slow down. Three further facts point at
   RBE contention rather than any test: the shard's `test.log` is **completely
   empty** (no gtest banner, so the process produced no output at all — a slow
   test would have printed its first `[ RUN ]` lines), only 1 of 8 shards was
   affected while the other 7 passed, and 1259.9 s matches **no** configured
   timeout (`sipi_image_tests` sets no `size`/`timeout`, so it inherits
   medium/moderate = 300 s; nothing in `.bazelrc`, `ci.yml`, or the
   `bazel-rbe`/`ci-setup` composite actions overrides `--test_timeout`). The
   number is Bazel's wall clock for an action submitted with `--jobs=192`
   against a 4-worker backend. A `size` bump would not help — the process never
   started. `10d150f3` is left untouched.

3. **`//test/e2e:lua_hardening` on darwin-arm64 — suspected pre-existing
   flake.** Run `33188874637`:
   `script_error_after_commit_aborts_the_stream` panicked at
   `test/e2e/tests/lua_hardening.rs:112` with reqwest
   `hyper::Error(IncompleteMessage)` on the `GET` itself. The test is a race
   between the script abort and the response head reaching the client; whether
   the abort lands before or after the head flushes decides which side sees the
   truncation. Nothing on this branch touches Lua, the scripting runtime, or the
   e2e harness. `lua_hardening` is **not** in `.bazelrc`'s
   `--flaky_test_attempts` allowlist (which covers `latency`,
   `iiif_compliance`, `resource_limits`, `memory_budget`, `server`, `upload`).
   Not fixed here — it is main-owned code outside this plan's scope. Surfaced
   for a maintainer ruling: either add it to the allowlist or make the test
   tolerate the early-truncation ordering.

## BLOCKER (RESOLVED round 7) — ADR-0024's PNG decision contradicted a shipped `fix:` on `main`

> **Resolved by the maintainer in round 7: option (b).** ADR-0024 Decision 6 is
> reversed for PNG — it keeps `setjmp`/`longjmp` and the landing block returns
> `std::unexpected`, uniform with JPEG. The ADR was amended by folding into its
> introducing commit (chunk `7A`), so it never appears on `main` recommending
> the throw hook. The defensive comments at `SipiIOPng.cpp:120-126` stay: they
> are now doubly load-bearing. Phase 5b is unblocked. The section below is kept
> as the evidence record that produced the ruling.

**Phase 5b cannot start until the maintainer rules.** This is not a
re-litigation of the round-5 acceptance; it is evidence the ADR did not have.

ADR-0024 Decision 6 says PNG should replace `longjmp` with **libpng's
throw-from-error-callback**, and the maintainer affirmed exactly that point when
accepting the ADR in round 5. But `sipi_error_fn`
(`src/format_handlers/cpp/SipiIOPng.cpp:120-126`) currently carries this comment:

```
  // Use longjmp via png_jmpbuf — the canonical libpng error handling pattern.
  // Throwing C++ exceptions through libpng's C stack frames is undefined behavior.
```

and `git log -S` traces that line to **`94f45a28` (2026-04-04), authored by the
maintainer**: *"fix: eliminate C++ exception-through-C UB in image format
handlers"*, whose PNG section reads, verbatim, *"Replace throw with
`longjmp(png_jmpbuf)` in `sipi_error_fn`"*. So the mechanism ADR-0024 proposes
adopting is precisely the one a shipped `fix:` on `main` removed as undefined
behavior four months ago. **ADR-0024 never mentions that commit or the UB
concern** — its justification is only that libpng's manual sanctions the hook.

Three facts that bear on the ruling:

1. **`libpng` is a BCR `bazel_dep` (`MODULE.bazel:97`, 1.6.54).** Its compile
   flags are not ours. Unwinding a C++ exception through libpng's frames needs
   that C code to carry unwind tables; we cannot guarantee or set that from
   here without vendoring libpng as a native `cc_library` (the repo's
   BCR-drop-in-or-vendor rule would then apply).
2. **The same defensive stance is still in the JPEG handler**
   (`SipiIOJpeg.cpp:86`, "This avoids throwing C++ exceptions through libjpeg's
   C frames") — and there the ADR *agrees*, keeping the setjmp landing site. The
   two codecs are being given opposite treatments for the same hazard.
3. **The ADR already records the uniform-with-JPEG alternative as a rejected
   Considered Option**, so choosing it is a deliberate reversal of an accepted
   decision, not a gap to be filled by a worker.

**Do not guess this.** The three options, stated so the ruling can be made
without reading any code: **(a)** stand by ADR-0024 — the April fix was
over-cautious, adopt the throw hook, and amend the ADR to say why `94f45a28`'s
reasoning does not apply (and settle whether libpng must be vendored to
guarantee unwind tables); **(b)** reverse Decision 6 — PNG keeps `setjmp` and
takes JPEG's landing-site pattern (the ADR's own rejected option), with the
landing block returning `std::unexpected` instead of throwing, and the ADR
amended to record the reversal and its evidence; **(c)** something narrower,
e.g. adopt the hook only if a scoped experiment proves the exception propagates
on all three CI platforms. **Option (b) is the cheapest and is consistent with
everything else in the tree**, but it overturns an accepted ADR, which is the
maintainer's call and not an orchestrator's.

Phase 5c (JPEG) is **not** blocked by this — its mechanism is settled and
uncontested — but it was deliberately not started mid-round, because a
half-migrated handler at a round boundary is exactly the state the
boundary-completeness rule exists to prevent.

## Where the next round starts (after round 14)

**The eight phases stayed finished; round 14 is the maintainer-approved
post-migration work, and the fuzz harness's first real harvest.** Two things
changed the branch's standing:

1. **The Linux CI verdict finally arrived, and it is green.** Run 33256167199
   on `7d0c7258` passed every leg — `test / linux-amd64`, `test / linux-arm64`,
   `test / darwin-arm64`, `asan-ubsan / amd64`, `commit-lint`, `docs`. That was
   the highest-value outstanding action for eight rounds and it ticks the last
   Linux-only acceptance item. Ninety-odd commits, first full run, no failures.
2. **The harness found a real memory-safety bug in code neither this branch nor
   the codecs own** (DEV-7078, chunk `14A`) — and then a second instance of the
   same defect turned up by hand in `Icc`'s stream operator, on a path reachable
   from `sipi query` and from Lua's `tostring(img)` with an ordinary profile.

Ten commits landed; `just bazel-test` is **74/74** (the new `//src/metadata:icc_parse_test`
is the extra target), `just commit-lint` is clean over the whole branch, the
approval goldens are byte-identical throughout, and no Rust was touched, so the
rustfmt/clippy gates do not apply this round.

**Two blockers come back, and neither is a config knob.**

1. **The J2K libFuzzer timeout**, promoted by triage from "tune the config" to a
   genuine availability finding: a 5.7 KB JP2 spins Kakadu's box parser for at
   least 293 s of CPU inside `read_shape`, and the loop is in code SIPI cannot
   patch.
2. **The PNG metadata round-trip**, found while trying to *test* the
   four-channel geometry question: SIPI's PNG writer truncates EXIF to three
   bytes and IPTC to zero, and since round 13 made a bad EXIF chunk fatal, SIPI
   refuses to read a PNG it wrote itself. This one is arguably the more urgent
   of the two, because the branch is what made it fail loudly.

Both are written up in full under Side findings, each with the decision the
maintainer owns.

**What is left after them:** the Q2 items nobody has asked for yet (XMP
parsing, the archived PNG decode benchmark), the still-uncovered degenerate
scale surfaces round 12 named, and the standing observation that there is no CI
gate for C++ formatting — every round has caught over-length lines by hand.

## Where round 13 said the next round starts (historical — superseded by the section above)

## Where the next round starts — **DONE** (after round 13)

**The plan is finished. There is no next round of implementation.** Round 13
landed the last five commits — the three maintainer rulings that round 12
returned blocked on, plus a wording fix folded into the first — and every
checkbox in the plan is ticked **except the two that only Linux CI can tick**
(`detect_leaks` confirmed by the scoped experiment; corpus-replay verified on
linux-x86_64 and linux-aarch64). Both are verification-on-CI items, not work.

What round 13 closed, in the order the rulings were given:

- **HTTP 400 for a rejected one-pixel size** (`8e9609c1`). `kInvalidRequestParameter`
  → `HttpStatusClass::kClientError` → `SipiStatus::BadRequest`, Sentry policy
  `kSkip`. The degenerate-dimension guard in the three resamplers split into a
  target check (the client's parameter, 400) and a source check (the
  repository's content, unchanged 500). No other code reclassified.
- **The `sample_with_icc.png` fixture** (`1fe886e6`). The truncated EXIF zTXt
  record stripped at the byte level, `PngRoundTrip` byte-identical, PNG's EXIF
  arm made fatal. `error-model.md` no longer records any reader-level exception
  to the fatal-metadata contract, because there is none.
- **Metadata-fatality coverage** (`75456d94`, `df40e27c`, `7d0c7258`). One
  crafted fixture per remaining format, each one reaching a parser and failing
  *there* rather than at a bounds guard, each verified through `sipi query`
  before its test was written, each added to its format's fuzz seed corpus.

Final state: `just bazel-test` **73/73**, approval goldens byte-identical,
`just bazel-rustfmt-check` / `just bazel-clippy-check` / `just commit-lint`
clean. `grep -rn "throw SipiImageError" src/format_handlers src/image
src/image_processing src/metadata` returns **18** sites, all of them allocation
guards, buffer-size-overflow invariants, `SipiImage`'s construction/geometry
invariants and the `getPixel`/`setPixel` accessors — no fallible path throws.

**What is left is not implementation:**

1. **The Linux CI verdict.** Still the highest-value action available, and now
   eight rounds old. No commit above `6509ccc4` has been seen by Linux CI. The
   two Linux-only checkboxes are ticked by that run, not by a worker.
2. **Deferrals, all maintainer-owned and untouched by this round:** the 16-bit
   watermark silent no-op, `TIFFOpenExt`, four-channel-PNG-encode geometry, XMP
   parsing, `Icc::createRGB`'s log-and-continue (the one metadata site the
   fatality ruling deliberately did not reach — it synthesizes a profile from
   colour tags rather than parsing an embedded blob), and the archived PNG
   decode benchmark.
3. **Two surfaces named in round 12 and still true.** The degenerate-scale
   rejection is wider than `1,1`: `SipiSize`'s proportional math can compute a
   `1` on the unpinned axis for an extreme-aspect-ratio source, and a genuinely
   1-pixel-tall source can no longer be scaled at all (the latter now answers
   500 rather than 400, which is the honest split, but it is still a behavior
   change no fixture exercises).
4. **There is still no CI gate for C++ formatting.** Every round has caught
   over-length lines with a hand-run `awk 'length > 120'`. Worth adding to CI;
   recorded here rather than acted on, since a new CI gate is the maintainer's
   call.

## Where round 12 said the next round starts (historical — superseded by the section above)

**Every phase of the plan is complete, and every acceptance criterion is
ticked.** Round 12 added **fifteen** commits and closed Phase 8: the facade
(`read`, `readSource`, `write`), `validate_decode_dims`, `read_watermark`,
`compose.cpp`, `SipiImageError`'s role and docstring, the fuzz-harness
contract, the documentation sweep, and both maintainer rulings that arrived
mid-round. `grep -rn "throw SipiImageError" src/format_handlers src/image
src/image_processing src/metadata` returns **only** allocation guards, OOM
sites and caller-invariant checks — no fallible path throws. The hot-path gate
ran over all three tiers and is flat.

**There is no code work left in this plan.** What remains is not
implementation:

1. **Push and get a Linux CI verdict.** This is now seven rounds old and still
   the highest-value action available. No commit above `6509ccc4` has been seen
   by Linux CI and the branch is 90+ commits deep. Round 12 did **not** rewrite
   history, so a plain force-push with a lease pinned to the last-pushed SHA is
   all it needs. Two of this round's commits change user-visible behavior
   (`da0914f7`, `5a8914e1`), so the e2e legs are the ones to watch.
2. **Three decisions the maintainer still owns**, all small and all recorded
   above with their evidence:
   - **The HTTP status for a rejected one-pixel size.** The ruling said 400;
     `status_for` cannot produce one, because `HttpStatusClass` has a single
     variant. Either add a status class plus a code only this case uses, or
     remap `kMalformedInput` globally — and the second would turn every
     corrupt-*file* rejection into a 400, which blames the client for the
     repository's content. One row, real consequences.
   - **`sample_with_icc.png` carries a truncated EXIF chunk.** Under the
     metadata-fatality ruling that fixture is corrupt, which is why PNG's EXIF
     arm is the one reader still tolerating a bad blob. Strip the chunk (the
     worker's analysis says the golden is byte-output-neutral) or replace the
     fixture, then finish the arm. Editing an LFS fixture is the maintainer's
     call per `test/approval/CHANGELOG.approval.md`.
   - **Metadata-fatality coverage for JPEG, J2K and TIFF.** Every `malformed/`
     fixture pins a *bounds guard*, which stops parsing early **without**
     producing a parse error, so none of them exercises the new fatal path.
     Three small crafted fixtures would close it; fabricating binaries was out
     of a chunk's scope.
3. **Carried forward, unchanged and maintainer-owned:** the 16-bit watermark
   silent no-op, `TIFFOpenExt`, four-channel-PNG-encode geometry, XMP parsing,
   and `Icc::createRGB`'s log-and-continue on failure (the one metadata site
   the fatality ruling deliberately did not reach — it synthesizes a profile
   from colour tags rather than parsing an embedded blob).

Two smaller notes for whoever picks this up:

- **The degenerate-scale rejection is wider than `1,1`.** `SipiSize`'s
  proportional math can compute a `1` on the unpinned axis for an
  extreme-aspect-ratio source (`2,` on a very wide, thin image), and the guard
  also tests the *source* dimensions, so a genuinely 1-pixel-tall image can no
  longer be scaled at all. No fixture exercises either; both are real surfaces.
- **There is no CI gate for C++ formatting.** A chunk pushed nine lines past
  the 120-column limit in a file that had none, and only an `awk 'length > 120'`
  check caught it. Worth adding to the per-chunk checks, or to CI.

## Where round 12 started (historical — superseded by the section above)

**Phase 6 is complete.** Round 11 added nine commits and closed the
`image_processing` conversion, its benchmark gate, and the `Image`-construction
question. `//src/image_processing` is throw-free: `grep -c throw` over both
`color.cpp` and `geometry.cpp` is **0**, and both file-local
`checked_buf_size_or_throw` helpers are gone.

**What is left is Phase 8, and one item dominates it.**

### The facade conversion — scoped, decomposable, and already started

`SipiImage::read` / `readSource` / `write` still throw. `read_shape` **no longer
does** — round 11 converted it (`8c2d230a`) precisely to prove the item
decomposes **by member**, and it does. Retiring the rest is the plan's Phase 8
head item, and the three caller checkboxes under it (`serve_image.cpp`,
`image_handle.cpp`, the CLI verbs) *are* its call sites — the maintainer's
ruling moved them here so they land together.

Measured scope, so round 12 does not re-measure:

| | sites |
|---|---|
| `read_shape` | **3** — done, all FFI seam |
| remaining production | **22** — `cli_app.cpp` 7, `image_handle.cpp` 5, `serve_image.cpp` 5, `convert_access_file.cpp` 2, `convert_service_file.cpp` 2, `verify.cpp` 1 |
| remaining tests + benchmarks | **~125** |

**One member plus all its callers per commit**, the same unit that carried all
of round 11. Recommended order is smallest-first, but **verify the dependency
order before trusting it** — round 11's very first correction was that
`image_processing`'s functions call each other and smallest-first was illegal.
Specifically: check whether `readSource` is implemented in terms of `read`
(it looks like it is), in which case they convert together or callee-first.

Two things `11K` learned that the remaining members should reuse:

- **Grep for dead catch arms before rewriting one.** `read_shape`'s Lua caller
  caught `Sipi::InfoError`, an enum **thrown nowhere in the tree**; the arm and
  its user-visible message were unreachable. The grep costs seconds.
- **A guard that becomes unreachable is reported, not deleted.** `serve_image.cpp`'s
  `if (info.success == SipiImgInfo::FAILURE)` is now dead. Left in place
  deliberately: a stale defensive line mid-migration is cheaper than a wrong
  deletion, and it can be swept once the facade is fully converted.

The test-side bulk (~125 sites) is mechanical: `ASSERT_NO_THROW(img.read(...))`
→ capture and assert `has_value()`. Do not let its size push a member into two
commits — the whole point of the unit is that a call site is touched once.

The seam conventions are all fixed and were exercised repeatedly in round 11 —
HTTP (`status_for` + `report_value_error`), Lua (`client_message()`), CLI
(`diagnostic_message()` into the verb's own reporting path, `EXIT_FAILURE`
unchanged), benchmarks (`std::abort()`), tests (`ASSERT_NO_THROW` → assert
`has_value()`). **Copy them; they are settled.** The one structural trick worth
reusing is the `Result<void> r;`-above-the-switch shape, which absorbed a
three-arm quality switch and three seven-arm orientation switches unchanged.

Note that retiring the facade's throw is also what makes
`SipiImage.cpp`'s `throw_from_value_error` unnecessary — it exists *only* to
convert a handler's `Result` into the exception the facade promises. It should
die with the facade's contract, not before.

### Then the rest of Phase 8, all small and independent

`SipiImageError` reduced or removed per the ADR; remaining throwing convenience
wrappers removed; fuzz-harness catch blocks narrowed (**do this after the
facade, not before** — the harness catches what `read` currently throws);
`UBIQUITOUS_LANGUAGE`/docs sweep; the ADR path-scrub per the standing
status-dependent ruling. `docs/src/development/error-model.md` is verified
current and is **not** outstanding.

### Blockers waiting on the maintainer

Three, all in this journal above: the **1×1 IIIF request** served at full size
(the one that needs a product decision), the **16-bit watermark** silently
skipped, and the **metadata-fatality** harmonisation carried forward from
round 10. None blocks Phase 8.

## Where round 11 started (historical — superseded by the section above)

**Phase 7 is complete. Phase 6's remaining work is decomposed and started.**
Round 10 added **six** commits: five `refactor:` and one `fix:`
(`86dd36fd`, a real user-visible defect — see below). HEAD is `86dd36fd`.

**Push and get a CI verdict. This is now six rounds old and still the single
highest-value action available.** No commit above `6509ccc4` has ever been seen
by Linux CI, and the branch is 66 commits deep. Round 10 did **not** rewrite
history, so a plain force-push with a lease pinned to the last-pushed SHA is
all that is needed.

### The one thing that changes the plan's shape

**The merged Phase 6/8 finale is decomposable, and round 10 proved it.** The
maintainer's ruling (no throw-adapters, no touching ~80 call sites twice) was
read in round 9 as "one enormous atomic change". It is not. The unit that
satisfies the ruling is **one `image_processing` function plus every one of its
call sites, in a single commit**. Each such commit is boundary-complete, needs
no adapter, and touches each site exactly once. `crop` landed that way
(`86dd36fd`, 6 sites: two handlers, the Lua seam, the benchmark, two tests).

Remaining twelve, smallest first — that is also the recommended order, since
the small ones settle each seam's convention before the 37-site `rotate`:

| function | call sites |
|---|---|
| `set_topleft`, `convertYCC2RGB`, `removeChannel`, `removeExtraSamples`, `toBitonal` | 2 each |
| `scaleFast`, `scaleMedium` | 5 each |
| `to8bps` | 6 |
| `add_watermark` | 7 |
| `scale` | 13 |
| `convertToIcc` | 15 |
| `rotate` | 37 |

> **Correction (round 11): "smallest first" is not a valid order — the
> functions call each other.** The call-site counts above are right, but four
> of these functions have an *internal* caller inside `//src/image_processing`,
> and a callee must be converted before (or with) its caller or the caller has
> to swallow a `[[nodiscard]] Result` — the adapter the ruling forbids. The
> edges, read out of the sources:
>
> | caller | callee |
> |---|---|
> | `set_topleft` (7 calls) | `rotate` |
> | `toBitonal` | `convertToIcc` |
> | `removeExtraSamples` | `removeChannel` |
> | `operator-=` / `operator+=` in `compose.cpp` (2 calls) | `scale` |
>
> So `set_topleft` (2 sites) cannot go first — it is gated on the 37-site
> `rotate` — and `toBitonal` (2 sites) is gated on the 15-site `convertToIcc`.
> The dependency-valid order, still smallest-first within each tier:
> **`convertYCC2RGB` (2) → `removeChannel` + `removeExtraSamples` together (4)
> → `to8bps` (6) → `scaleFast` (5) → `scaleMedium` (5) → `add_watermark` (7) →
> `scale` (13, incl. the two `compose.cpp` operator sites) → `convertToIcc`
> (15) then `toBitonal` (2) → `rotate` (37) with `set_topleft` (2)**.
> Where a pair is tightly coupled (`removeChannel`/`removeExtraSamples`,
> `rotate`/`set_topleft`), converting both in **one** commit is what avoids the
> double touch; the commit subject still names one concern, so it passes the
> "And" test.
>
> **Second correction: the file-local `checked_buf_size_or_throw` helpers.**
> `color.cpp`, `geometry.cpp`, `SipiImage.cpp` and `SipiIOTiff.cpp` each carry
> one, and they throw `SipiImageError` on a pixel-buffer size overflow — a
> throw on a fallible path, which Phase 8's acceptance criterion forbids. The
> resolution that avoids double-touching: **each chunk replaces the helper
> calls inside the function it converts** with the `std::optional`-returning
> `Sipi::checked_buf_size` (`src/util/cpp/checked_arith.h`) plus a
> `kMalformedInput` error value, and the helper itself is deleted by the last
> chunk that empties it. `crop` still uses it and is the one exception — it was
> converted before this was noticed, so `geometry.cpp`'s helper dies with
> `rotate`/`scale`, and `crop`'s two calls are swept then.

**The seam conventions are already decided by `crop`; copy them, do not
re-derive:**
- **Handlers** (`Result`-returning already) — `return std::unexpected(e.error());`.
- **Lua seam** (`image_handle.cpp`) — `emit_str(err, err_ctx, e.error().client_message()); return 1;`,
  mirroring the file's existing `catch (SipiError &)` arm. `client_message()`
  is redacted and location-free; **`diagnostic_message()` would leak paths to a
  script** — do not use it here.
- **CLI verbs** — not yet exercised by any converted function. The first chunk
  that reaches one (`convertToIcc` and `rotate` will) sets that convention;
  `error-model.md` says stderr gets `diagnostic_message()` and the exit code
  derives from `HttpStatusClass`.
  > **Round 11 correction, before any chunk reaches a CLI site.**
  > `error-model.md` says "the exit code is derived from `HttpStatusClass`
  > (client-class failures and internal failures map to distinct non-zero
  > codes)". **The shipped CLI does no such thing** — `cli_app.cpp`,
  > `convert_access_file.cpp`, `convert_service_file.cpp`, `verify.cpp` and
  > `health.cpp` are uniformly binary `EXIT_SUCCESS` / `EXIT_FAILURE`, and each
  > header documents exactly those two. Since the plan's acceptance criterion
  > is that **CLI exit codes are verified unchanged**, the convention to set is:
  > `diagnostic_message()` into the verb's existing `report_error` /
  > `emit_json_report` path, then `return EXIT_FAILURE` — byte-identical to the
  > `catch (const SipiImageError &)` arm it replaces. Introducing distinct exit
  > codes is a user-visible CLI contract change and needs a maintainer ruling;
  > it is **not** something a migration chunk may take. The `error-model.md`
  > sentence is aspirational and should be corrected to describe the binary
  > contract in the Phase 8 docs sweep (or the doc kept and the split raised
  > with the maintainer as its own decision).
- **Benchmarks** — handle the result to satisfy `[[nodiscard]]` without adding
  work to the timed region.
- **Tests** — an `EXPECT_FALSE` becomes an assertion on failure **plus** the
  `ErrorCode`. Strengthen, never weaken.

Then Phase 6's last item (`Image` construction → `Result` factories), then
Phase 8's tail: `SipiImageError` narrowed or removed, remaining throwing
wrappers removed, fuzz-harness catch blocks narrowed, the CONVENTIONS.md rule,
the `UBIQUITOUS_LANGUAGE`/docs sweep, and the ADR path-scrub per the standing
status-dependent ruling. **`docs/src/development/error-model.md` already exists
and is current** — that Phase 8 item is verified done, not outstanding.

### Cautions for the next round, each paid for once already

1. **Verify every "fact" in a brief against the code before acting.** Three of
   round 10's briefs were wrong about a call site's guarding, and in one case
   about whether the code being converted existed at all (`Xmp`'s throws were
   inside a comment block). Workers refused two bad briefs and were right both
   times. The pattern that keeps working: state facts *with* line numbers and
   tell the worker to confirm them cheaply and stop if they do not hold.
2. **Never let a failure become a success.** Round 10's one sent-back chunk
   turned PNG's fatal IPTC failure into log-and-continue. Converting a *fatal*
   path to a *reported* one is always safe; the reverse changes what SIPI
   serves and needs a ruling. This is chunk `9H`'s rule, and it recurred
   immediately.
3. **`clang-format` changed lines only.** A worker reformatted the whole of
   `processing.h`; it caught and reverted itself. That file is about to be
   edited twelve more times.
4. **Watch for the dangling `else { // clean up and throw exception }`** — see
   Side findings. It is in both `crop` overloads and may be in `scale` and
   `rotate` too; check rather than re-discover.

### Benchmark state

The `-c opt` on-disk pairs, all valid as the next phase's "before" while the
tree does not move: `decode-p7-after.json` at `14b6d89d`,
`process-p6crop-after.json` at `86dd36fd`, `encode-p6-before.json` (encode was
untouched all round). **Round 10 needed no A/A control** — decode geomean
`+0.12 %`, process geomean `+0.16 %`, every case inside ±2.5 % against a
calibrated ±6 % per-case / ±2 % geomean band. Read the round-9 A/A section
before calling any future number a finding.

### Carry-forward, unchanged

`detect_leaks` + Linux corpus-replay ride the first nightly; the PNG decode
benchmark gap is accepted; `TIFFOpenExt` per-handle handlers and the
four-channel-PNG-encode geometry finding remain maintainer-owned. **New this
round, all in Side findings and all wanting a maintainer rather than a worker:**
the four handlers' disagreement about whether malformed embedded metadata is
fatal (with no test coverage anywhere), `crop`'s silent success on an
unsupported `bps`, and the discovery that SIPI has never parsed or validated
XMP at all — on a thread-safety premise that exiv2 0.28.5 plus the already-wired
`XmpParser::initialize` lock appears to have made obsolete.

## Where round 10 started (historical — superseded by the section above)

**Phases 1–5 are complete and Phase 6 is more than half done.** All four
handler friendships with `SipiImage` are gone (`grep "friend class"
src/image/cpp/SipiImage.h` is empty), `app14_transform` is off the image, and
the fallback loops no longer retry the dispatched handler or throw a causeless
error. Round 9 added **eight** commits; two are `fix:`es for main-owned bugs the
maintainer ruled on, six are `refactor:`.

**Push and get a CI verdict. This is now five rounds old and it is still the
single highest-value action available.** No commit above `6509ccc4` has ever
been seen by Linux CI, and the branch is now 60 commits deep. Round 9 did *not*
rewrite history, so a plain force-push with a lease pinned to the last-pushed
SHA is all that is needed.

**Phase 6 has exactly two items left, and they share one unresolved design
question — see "Open question for the maintainer" below. Do not start either
until it is answered.**

1. **`image_processing`'s free functions → `Result`** (plan Phase 6 item 2).
2. **`Image` construction → `Result` factories** (plan Phase 6 item 1).
3. **The facade adapter retirement** (plan Phase 6 item 5) was reassessed and
   deliberately not attempted; see the deferral below. In short: as written it
   means `SipiImage::read`/`read_shape`/`write` return `Result`, which forces
   *all* of Phase 8's caller work (ffi, Lua surface, CLI verbs) into Phase 6.
   Recommendation: move the checkbox to Phase 8, where its call-site work
   already lives.

**Benchmark state.** The `-c opt` "after" triple on disk is
`decode-p6-after.json` / `encode-p6-after.json` / `process-p6-after2.json` at
`c2bbbd13`, valid as the next phase's "before" side as long as the tree does not
move. **Read the round-9 benchmark section before interpreting any number**: an
A/A control on this machine showed a `−2.08 %` geomean and a `−6.32 %` swing on
`decode_tile/flat_tiff/256` with *zero* code change. Nothing below that band is
a finding here without an A/A control of its own.

Carry-forward, unchanged: `detect_leaks` + Linux corpus-replay ride the first
nightly; the PNG decode benchmark gap is accepted. Closed this round: the dead
`separateToContig` member (`9A`), the `TIFFSetDirectory`/`JPEGCOLORMODE` decode
bug (`9B`). Still open for the maintainer: `TIFFOpenExt` per-handle handlers,
and the new four-channel-PNG-encode geometry finding.

## Round 9's starting notes (historical — superseded by the section above)

**Phases 1, 2, 3, 4, 5 (a, b, c, d **and e**) are complete.** All four handlers
report every internal failure as a `SipiValueError` value, the `SipiIO` vtable
is `Result`-returning, and the transitional throw-wrappers are gone — the single
conversion back to an exception lives in `SipiImage`'s dispatcher. **Phase 6
(the hub) is next, and nothing blocks it.** The only unticked Phase 1 items are
still the two that can close only on Linux CI (`detect_leaks` confirmation,
corpus-replay on linux-x86_64/aarch64).

**Push and get a CI verdict before writing more code. This is now four rounds
old and it remains the single highest-value action available.** No commit above
`6509ccc4` has ever been seen by Linux CI. This round rewrote history once more
(the ADR fold-in, `9de5d8c6` → `8a512105`) and added **eight** commits, one of
which — the vtable flip — touches the base class every handler and the
dispatcher share. Round 3 lost an entire round to exactly this trap: the
`//src:image` output-path collision was invisible locally for two rounds because
the colliding target is Linux-gated and Bazel silently skips it on darwin.
Nothing this round adds a package, renames a target or edits a BUILD file, so
that specific class is not in play — but eight unverified commits on the
codec layer is a lot of unverified surface.

Then, in order:

1. **Phase 6 (the hub, DEV-7059) is the next code.** Its five items are already
   written down in the plan. Two carry warnings from this plan's own history:
   - **`app14_transform` keeps its own commit**, never folded into the
     friend-removal change. The audit calls it a genuine behavior change (JPEG
     inverts CMYK/YCCK at decode; downstream sees standard CMYK) dressed as a
     mechanical refactor. Verify it against the approval goldens on its own.
   - **`SipiImage::read`'s fallback loop is a redesign, not a retyping.** Every
     phase so far *deliberately preserved* it: a handler's "not my format" early
     return is a **successful** `Result` carrying `false`, and the dispatcher
     throws immediately on an error value rather than falling through. All four
     handlers now have the value plumbing the redesign needs — the loop can
     finally distinguish "this handler does not recognise the format" (try the
     next) from "this handler recognised it and failed" (stop, propagate the real
     error) instead of erasing both into `Error reading file <path>`. Do that
     deliberately; do not let it happen as a side effect of retiring the adapter.

2. **Three items are now waiting on a maintainer ruling, all cheap to answer**
   and all written up in full below — no code reading required.
   - **`TIFFOpenExt` per-handle libtiff handlers** (Deferrals). Recommended, not
     taken. It is the only mechanism by which a libtiff error can reach the
     per-call `Result` rather than only the log, and ADR-0024's TIFF bullet
     anticipates it. Deferred because it moves error text at many sites and needs
     a multi-error-per-file design call.
   - **`SipiIOTiff::separateToContig(SipiImage *, unsigned int)` is dead code**
     (Side findings) — zero callers, hidden by a name collision with the two live
     file-static templates, and it carries the one `throw` in that file no chunk
     converts. Recommended: delete it.
   - **`TIFFSetDirectory(tif, 0)` discards `TIFFTAG_JPEGCOLORMODE`** (Side
     findings), which is why JPEG-compressed TIFFs fail to decode. Pre-existing,
     long known (`sipiimage_test.cpp:412` carries a `// BROKEN!` banner over a
     commented-out test; an e2e test accepts a 500), now **legible** because the
     error handler is live. The one-line repair is a real behavior change and
     wants its own `fix:`.

3. **Benchmarking: compare the endpoints, not just consecutive pairs.** This
   round produced the cleanest drift measurement the plan has: Phase 5d read
   `−2.35 %` and Phase 5e read `+1.58 %` on the same cases, while the 5c → 5e
   **endpoint** comparison is `−0.81 %` decode / `+0.13 %` encode — flat. Two
   intermediate gates can each show a 2-7 % move while the composite shows
   nothing. The round-7 rule (check the untouched cases in the same run) still
   holds; this adds the endpoint check for any multi-commit phase.

4. **The `-c opt` benchmark pairs on disk** are `decode-5e-after.json` /
   `encode-5e-after.json` (captured at `66d46060`), valid as Phase 6's "before"
   side for as long as nothing in the tree moves. `/private/tmp` is **not
   durable**; if they are gone, recapture at the merge-base of the phase's first
   commit, never at an earlier tag.

5. **The handler-chunk shape has retired itself.** With the vtable flipped there
   are no more `*_impl`/override pairs — the overrides *are* the `Result`
   functions. Phase 6 works on `SipiImage` and `image_processing`, so the
   relevant precedent is now `SipiImage`'s own dispatcher, not the per-handler
   pattern.

Carry-forward, unchanged: `detect_leaks` + Linux corpus-replay ride the first
nightly. Closed: `redact_paths` (chunk `6A`), the PNG-bridge blocker (chunk
`7A`), the ADR-0024 TIFF rationale (chunk `8A`), the JPEG volatile-locals
violations (chunk `8B`), `log_vformat` (chunk `8C`), and the PNG decode
benchmark gap (accepted, round 8).

## Round 8's starting notes (historical — superseded by the section above)

**Phases 1, 2, 3, 4, 5a, 5b and 5c are complete.** Three of the four handlers —
J2K, PNG, JPEG — now report every internal failure as a `SipiValueError` value
and re-wrap to a throw at the override, with the `SipiIO` vtable still
untouched. **Only `SipiIOTiff` remains before the Phase 5e vtable flip.** The
only unticked Phase 1 items are still the two that can close only on Linux CI
(`detect_leaks` confirmation, corpus-replay on linux-x86_64/aarch64).

**Push and get a CI verdict before writing more code. This is now three rounds
old and it is the single highest-value action available.** No commit above
`6509ccc4` has ever been seen by Linux CI. Round 5 rewrote history and added
two commits, round 6 added six, round 7 rewrote history again (the ADR fold-in)
and added six more. Round 3 lost an entire round to exactly this trap: the
`//src:image` output-path collision was invisible locally for two rounds because
the colliding target is Linux-gated and Bazel silently skips it on darwin.
Round 7's own commits are low-risk on that axis — four C++ files and one ADR, no
new package, no target rename, no BUILD change at all — but the *layout* commits
underneath them still have not been verified in their current form.

Then, in order:

1. **Two maintainer rulings are pending. Neither blocks Phase 5d, and both are
   cheap to answer** — everything needed is written up above, no code reading
   required.
   - **The `icc_buffer_guard` volatile-locals violation** (Side findings). A
     genuine, pre-existing, main-owned violation of the rule ADR-0024 itself
     states, in the JPEG decode path. Recommended: one `fix(format_handlers):`
     adding `volatile`, with `html_buffer` and `http_ctx` assessed in the same
     pass. Left untouched because the standing pattern for a main-owned bug is
     to fix it at the root **on a ruling**, never folded into a `refactor:`.
   - **The missing PNG decode benchmark** (Deferrals). `@sipi_bench_fixtures`
     has no PNG master, so CLAUDE.md's hot-path rule cannot be satisfied for
     PNG decode without regenerating and re-releasing a 321 MB pinned archive.
     Either accept the gap explicitly or schedule the archive work.

2. **Phase 5d (TIFF, DEV-7060) is the next code, and it is now specified rather
   than open** — see the Phase 5d prep section. The historical "malformed UTF-8"
   segfault story is **folklore**: the real defect is a va_list-vs-varargs ABI
   mismatch in a line that never even compiled. That section carries the root
   cause, the two commits involved, what a correct re-enablement must do, the
   `TIFFSetWarningHandler(nullptr)` landmine that would keep `tiffWarning`
   silent anyway, the separate `log_vformat` signature bug in the logging
   module, and the `TIFFOpenExt` per-handle-handler option that would let a
   libtiff error reach the `Result` instead of only the log.

   Chunk it as: (a) the handler re-enablement as its own commit, (b) `read_shape`,
   (c) `read`, (d) `write`. TIFF is 2489 lines with 36 `throw` sites — roughly
   twice the JPEG job — which is why round 7 stopped at a clean phase boundary
   rather than starting it. **Correct ADR-0024's TIFF sentence in the same
   round**, folded into `9de5d8c6`; it states the folklore as fact.

3. **The handler-chunk shape is settled and should just be reused.**
   `read`/`write` internals are **private static members** (`SipiImage`'s pixel
   and metadata state is protected, and the friendship is granted to the class,
   not the TU, so a file-local free function does not compile); `read_shape`
   internals can be file-local because they touch no `SipiImage` state. A
   private static member does not affect the vtable. One commit per entry point,
   override re-wrapping throughout, benchmark pair before the first commit and
   after the last, `kClientAbort` kept as its own branch on any `write`.
   **Check first whether TIFF's `read_shape` even has throw sites** — JPEG's had
   none, so it correctly got no conversion at all and picks up its `Result`
   signature in the vtable flip.

4. **Benchmark drift on this machine is ±2–4 % between sessions**, and two
   rounds of data now show it. Do not read a sub-5 % move as a finding without
   checking the untouched cases in the same run; that calibration has settled
   the gate twice (round 7's PNG `−1.95 %` and JPEG `+2.4 %` were both drift).
   A same-binary A/A control reproduces to ±0.13 %, so per-case CV understates
   the real uncertainty — the untouched-case comparison is the reliable check.

5. **The benchmark "before" pair for Phase 5d is already on disk**:
   `…/scratchpad/bench/decode-5c-after.json` and `encode-5c-after.json`
   (`-c opt`, `--benchmark_repetitions=20`, captured at `e2dc3143`). They are
   valid as the TIFF "before" side for as long as nothing in the tree moves.
   Note `/private/tmp` is **not durable**; if they are gone, recapture at the
   merge-base of the phase's first commit, never at an earlier tag.

6. **A side finding worth folding into Phase 6's design, not just its diff.**
   `SipiImage::read`'s fallback loop re-tries every handler when the
   extension-matched one returns `false`, then reports a single generic
   `Error reading file <path>` — so a resource failure inside the *correct*
   handler is indistinguishable from "wrong format". Phase 5a **preserved** this
   deliberately (J2K's two `pull_stripe` failures still return a successful
   `Result` carrying `false`), because changing it would change which handler the
   dispatcher believes owns the file. Phases 5b and 5c preserved it the same
   way — PNG's and JPEG's "not my format" early returns are still successful
   `Result`s carrying `false`. Phase 6 owns the redesign; the value plumbing it
   needs is now in place for three of the four handlers.

Carry-forward, unchanged: `app14_transform` keeps its own commit in the hub
phase; `detect_leaks` + Linux corpus-replay ride the first nightly. The
`redact_paths` item is **closed** — fixed in chunk `6A` on the maintainer's
ruling. The PNG-bridge blocker is **closed** — ruled on in round 7 and landed
as chunk `7A`.

## Phase 5d prep — the TIFF handlers were never silenced for the reason everyone believes (round 7)

Read-only investigation, run so that round 8 can specify Phase 5d instead of
discovering it. **The "malformed UTF-8 caused segfaults" story is folklore.**
The real defect is an ABI bug, and the evidence is conclusive:

**Root cause.** The commented-out body is `/* log_err(fmt, argptr); */`, and
`log_err`'s signature (`src/logging/cpp/logger.h`) is
`void log_err(const char *message, ...)` — a plain printf-style variadic.
`tiffError`'s third parameter is a `va_list`. Passing a `va_list` into another
function's `...` slot is the classic va_list-vs-varargs mismatch: the callee
receives a pointer to the caller's `va_list` internals as its first "argument"
and then walks that structure's bytes for every `%s`/`%d` in `fmt`, dereferencing
garbage. It would fire on **any** format string with conversions, whatever the
bytes in the values. Nothing about it is UTF-8-specific.

**And that line has never compiled.** The parameter is named `args`; the dead
body says `argptr` — an undeclared identifier in that scope. So the commented-out
code cannot be the source of an observed crash. There is **no stack trace, no
issue reference, no test and no corpus entry** anywhere in the repo; the comment
is the only artifact.

**History.** The handlers were correct at import (`fb13e32c`):
`vsyslog(LOG_ERR, fmt, argptr)` — glibc's canonical `va_list`-taking twin of
`syslog`, exactly matching the callback's signature. Commit **`2b60176a`**
(Raitis Veinbahs, 2025-03-13, *"DEV-4658 … improve logging (#467)"*, body:
"Silence TIFF errors because of segfaults") removed `syslog`/`vsyslog`
repo-wide and introduced the new logging module **in the same commit** — and at
that moment the new module was entirely variadic, with **no `va_list`-taking
sibling at all**. The naive substitution of `log_err` for `vsyslog` is precisely
the mismatch above. Six days later **`b2899d74`** added `log_vsformat` — the
safe primitive that would have unblocked this — and nobody went back.

**What a correct re-enablement needs.**
1. Format the `va_list` into a `std::string` first, via the one correctly
   declared `va_list` primitive: `log_vsformat(LogLevel, const char *,
   va_list)`. Never forward a `va_list` into a `...` slot.
2. Pass the result as a **`%s` argument**, never as the format string —
   `log_err("TIFF error [%s]: %s", module, formatted.c_str())`.
   `SipiIOPng.cpp`'s `sipi_error_fn` already does exactly this and is the model.
3. **The handler must not throw.** `SipiIOTiff.cpp` contains no
   `setjmp`/`longjmp` at all — libtiff reports failure through ordinary return
   codes that the call sites already check, so `tiffError`/`tiffWarning` are a
   pure logging side-channel with no landing site to catch anything. Any
   internal failure (`logger.cpp`'s `vformat` throws `std::system_error` on a
   `vsnprintf` encoding error) must be caught locally and swallowed. The
   handler is process-global and SIPI is multithreaded, so it must also be safe
   to enter concurrently.

**Two landmines and a bonus.**
- **`TIFFSetWarningHandler(nullptr)` at `SipiIOTiff.cpp:950` and `:1564`**
  disables the *global* warning handler at the top of `read()` and
  `read_shape()` and never restores it. Those are the two hot entry points, so
  `tiffWarning` would **never fire** even after re-enablement. Whether that
  suppression is deliberate (noisy header-parse warnings) or leftover from the
  same silencing effort is an open question for the maintainer.
- **`log_vformat` is itself broken**: declared `void log_vformat(LogLevel, const
  char *, ...)` in `logger.h` but *defined* taking a `va_list` in `logger.cpp`.
  A genuine signature mismatch, present since `b2899d74`, latent only because
  nothing outside `logger.cpp` calls it. Main-owned; worth its own
  `fix(logging):` and worth knowing about before someone reaches for the
  obvious-looking name. **`log_vsformat` is the one to use.**
- Vendored libtiff is **4.7.1** (`MODULE.bazel:708`), which ships the modern
  per-handle API — `TIFFOpenExt` + `TIFFOpenOptionsSetErrorHandlerExtR`, whose
  handler takes a `void *user_data`. That is the mechanism that would let a
  libtiff error be captured into the **per-call `Result`** rather than only
  logged through a process-global side-channel. `SipiIOTiff.cpp` uses plain
  `TIFFOpen` today. Worth a deliberate decision in Phase 5d rather than
  defaulting to the classic global handler.

**ADR-0024 and this plan both repeat the folklore** (Decision 5's TIFF bullet:
"disabled because malformed UTF-8 in libtiff's own format strings caused
segfaults when passed through `vsnprintf`-family formatting"). That sentence is
wrong and should be corrected to the ABI root cause. Because ADR-0024 is
introduced *by this branch* (`9de5d8c6`), the correction belongs folded into
that commit, the same way the PNG reversal was — **not** done here on an
orchestrator's initiative, since it edits an accepted ADR's stated rationale.
Surfaced for the maintainer; the natural moment is the start of Phase 5d, which
is where the sentence is acted on.

## Phase 8 benchmark gate — all three tiers, the whole round measured end to end (round 12)

**The round's hot-path gate**, covering everything from `8c2d230a` (round 11's
HEAD) to `5a8914e1`: the facade's `read`/`readSource`/`write` conversion, the
`validate_decode_dims` and `read_watermark` conversions, the metadata-fatality
change, the degenerate-scale rejection and the PNG RAII guards. Decode, encode
**and** process, because this round touched all three tiers.

**Method** — the round-11 technique, unchanged and still the right one: both
endpoints measured in this same worktree, same session, same machine, `-c opt`,
`--benchmark_repetitions=5`, warm Bazel action cache. `just bench {decode,encode,process}`
at HEAD, then `git checkout 8c2d230a -- src/ test/`, the same three runs, then
`git checkout HEAD -- src/ test/` and a `git status` check that the tree came
back clean (it did — only the plan and journal, which live outside `src/` and
`test/`, remained modified). No fresh `git worktree`: its separate Bazel output
base means a cold build of kakadu/libtiff/exiv2 for a handful of numbers.

**Result — flat on every tier.**

| tier | `OVERALL_GEOMEAN` (real) | widest single case (median) |
|---|---|---|
| decode | **+0.26 %** | `decode_tile/jpeg_baseline/1024` −1.14 % |
| encode | **−1.78 %** | `BM_EncodeJ2k` −3.22 % (CPU), −0.52 % real |
| process | **+0.03 %** | `BM_Rotate90` −0.92 % |

Every case sits inside the machine's calibrated ±6 % per-case band and every
geomean inside the ±2 % floor, so **no A/A control was needed**. `BM_EncodeJ2k`
is the only case past 3 %, it is the run's noisiest by CV (2.8 %), and it points
faster — noise, not a win.

**Why flat is the expected answer.** Every check this round added or moved sits
outside a pixel loop: a `!r` test after a call that has already done
megabyte-scale work, a header-field comparison before an allocation, a
`png_destroy_write_struct` on an exit path. The one structural change with any
claim on codegen — `SipiImage::read`/`write` returning `std::expected<void,
SipiValueError>` instead of throwing — happens **once per request**, against a
decode measured in tens of milliseconds. A non-flat result here would have meant
something was wrong with the conversion, not with the benchmark.

## Phase 6 transform-tier benchmark gate — the whole `image_processing` conversion, measured end to end (round 11)

**This is the gate the plan's "before/after benchmark run (transform paths)"
checkbox has been waiting for since Phase 6 opened.** The span is the entire
`image_processing` migration: `86dd36fd` (round 10's HEAD, `crop` converted and
nothing else) → `c281379a` (all thirteen operators converted). Endpoint
comparison over eight commits, which is the right unit — every intermediate
commit changes the same functions, so per-commit runs would measure noise.

**Method.** Both endpoints measured **in this same worktree, same session,
same machine**, `-c opt`, `--benchmark_repetitions=5`, using the warm Bazel
action cache: `just bench process` at `HEAD`, then `git checkout 86dd36fd --
src/ test/`, the same run again, then `git checkout HEAD -- src/ test/` and a
`git status` check that the tree came back clean. Safe because everything was
already committed; the whole point is to avoid a fresh `git worktree`, whose
separate Bazel output base means a cold build of the entire native dep graph
(kakadu, libtiff, exiv2) for two numbers. **Use this technique next time** —
it is the round-5 same-session A/B with the worktree cost removed.

**Result — flat, marginally faster.**

| case | median Δ (real) |
|---|---|
| `BM_ScaleFast/256` | +0.01 % |
| `BM_ScaleFast/1024` | −0.32 % |
| `BM_ScaleMedium/256` | −0.08 % |
| `BM_ScaleMedium/1024` | −0.03 % |
| `BM_ScaleHigh/256` | −1.74 % |
| `BM_ScaleHigh/1024` | −3.88 % |
| `BM_Rotate90` | +0.46 % |
| `BM_Rotate45` | +0.17 % |
| `BM_Crop1024` | +0.24 % |
| `BM_To8bps` | −0.02 % |
| `BM_ConvertToIccAdobeRgb` | −0.28 % |
| `BM_ConvertToIccCmykToSrgb` | −0.62 % |
| `BM_RemoveAlphaChannel` | −0.09 % |
| **`OVERALL_GEOMEAN`** | **−0.45 %** |

Every case is inside the machine's calibrated ±6 % per-case band and the
geomean is inside the ±2 % floor, so **no A/A control was needed**. The two
`BM_ScaleHigh` numbers are the only ones past 1 %, they point the *faster* way,
and `BM_ScaleHigh` is the case the round-5 notes already flag as this
machine's noisiest — read them as noise, not as a win.

**Why flat is the expected answer, not a lucky one.** Every error check this
migration added sits *outside* the pixel loops: a `checked_buf_size` test
before an allocation, a `!r` test after a call. `std::expected<void,
SipiValueError>` is returned by value from functions that already did
megabyte-scale work. Nothing in a hot loop changed. A non-flat result here
would have meant something was wrong with the conversion, not with the
benchmark.

## Phase 7 benchmark gate — the calmest run in ten rounds (round 10)

Before = `decode-p6-after.json` at `c2bbbd13` (round 9's "after", still valid
because the tree had not moved). After = `decode-p7-after.json` at `14b6d89d`.
`-c opt`, `--benchmark_repetitions=20`, same machine, same session. Decode tier
only, which is the right tier and not a gap: metadata construction runs inside
`read()`, and nothing in this phase touched an encode path
(`iccBytes()`/`iccFormatter()` were deliberately left alone).

**`OVERALL_GEOMEAN +0.12 % real / +0.02 % CPU`.** All 18 cases:

- largest negative `decode_tile/jp2/256` **−2.45 %**
- largest positive `decode_tile/flat_tiff/1024` **+1.75 %**
- everything else inside ±1.6 %

Against this machine's calibrated drift band — ±6 % per case on the cheap TIFF
cases, ±2 % on the geomean, established by round 9's A/A control — **nothing
here is even a candidate for a finding**, so no A/A control was run this time.
That is the band doing its job: four rounds of arguing about ±2 % movers, and
the first phase whose numbers need no argument at all.

Worth noting *why* it is this flat. Every retyped return in Phase 7 is on a
metadata **failure** path, and the corpus fixtures have valid metadata, so the
happy path executes the same work with the same allocations. The one structural
change on the success path — factories building into locals and moving into a
private constructor — is a move of an `Exiv2::ExifData`/`IptcData`/`ProfilePtr`
that the constructor used to build in place. It does not register.

## Phase 6 (partial) benchmark gate — the A/A control that settles a +6 % (round 9)

Before = `decode-p6-before.json` / `encode-p6-before.json` / `process-p6-before.json`,
captured at `66d46060` **before any code changed this round** (the round-8 pairs
had been lost, and the journal's "recapture at the merge-base of the phase's
first commit" instruction was followed). After = the `-p6-after` triple at
`c2bbbd13`. `-c opt`, `--benchmark_repetitions=20`, same machine, same session.
**No regression on any tier.**

Headline numbers, read alone, look like a small cost:

- decode `OVERALL_GEOMEAN **+0.43 % real / +0.57 % CPU**`, largest mover
  `decode_tile/flat_tiff/256` at **+6.17 %** — above the maintainer's 5 %
  threshold, and with the `/256`-moves-more-than-`/1024` shape that the round-8
  note identified as the per-call-overhead signature (`+6.17 / +2.75 / +1.73 %`
  across `/256`, `/1024`, thumb).
- encode `**+0.95 % real / +2.32 % CPU**`, `BM_EncodeTiff` at `+4.32 %`.

**Both are refuted by a same-binary A/A control**, which this round ran rather
than argued about: the decode benchmark was executed a **second** time at the
same commit, with no rebuild and no code change, and compared against its own
first run.

- A/A `OVERALL_GEOMEAN **−2.08 % real / −2.03 % CPU**` — a swing four times the
  before/after delta, from nothing at all.
- A/A `decode_tile/flat_tiff/256` **−6.32 %**. That is *the same case* that read
  `+6.17 %` in the before/after comparison, moving the same magnitude in the
  opposite direction with **zero** code difference. The "per-call-overhead
  signature" is a property of that case's noise, not of any change.

**The process tier is this round's untouched control, and it is the first time
the plan has had a genuine one.** No commit this round touched
`image_processing` — the free functions are Phase 6's *remaining* work, not its
landed work — yet the tier read `OVERALL_GEOMEAN −0.65 %` with individual cases
from `BM_ScaleHigh/1024` at **−7.17 %** to `BM_ScaleFast/256` at **+2.83 %**.
Provably identical code, a 10-point spread. That is the round-7 "check the
untouched cases in the same run" rule finally answered with real untouched
cases instead of a proxy.

So: decode flat, encode flat, process flat, and the calibration to carry
forward is that **this machine's per-case run-to-run drift reaches ±6 % on the
cheap TIFF cases and ±2 % on the geomean**. Any future finding on this hardware
below that band needs an A/A control before it is called a finding. **No
alignment or `copt` hack was added**, per the maintainer's standing rule.

One process note: the first `process` capture of the round produced a truncated
JSON (96 KB against the before-file's 140 KB) that `compare.py` rejected as
"not a valid benchmark executable or JSON file". Re-running produced a complete
file. Check the output size before trusting a `just bench` JSON.

## Phase 5e benchmark gate — the vtable flip, and the run that finally explains the drift (round 8)

The virtual-call ABI boundary is where a happy-path codegen regression would
appear, so the flip got its own pair: before = `decode-5d-after.json` /
`encode-5d-after.json` at `be347e3b`, after = `decode-5e-after.json` /
`encode-5e-after.json` with the flip applied. `-c opt`,
`--benchmark_repetitions=20`, same machine and session.

**Flip-only:** decode `OVERALL_GEOMEAN +1.58 % real / +1.44 % CPU`, encode
`+2.18 % / +1.93 %`, with `BM_EncodeTiff` at `+5.42 %`.

Read alone that looks like a cost. It is not, and this round produced the
evidence that settles it rather than another judgement call. **The Phase 5d run
immediately before it moved the same cases −2.35 % / −2.00 %, with
`BM_EncodeTiff` at −7.56 %** — the mirror image. Neither change can plausibly
have made the happy path 2 % faster and then 2 % slower: 5d converted only
failure returns, and the flip changes a return type on calls that happen once
per decode, not per tile.

**So the number that matters is the endpoint comparison, 5c → 5e** — the last
pre-Phase-5d state against the fully flipped state, i.e. everything this round
did to these paths:

- decode `OVERALL_GEOMEAN **−0.81 % real / −0.69 % CPU**`; every one of the 18
  cases within ±3.2 %, most within ±1 %.
- encode `**+0.13 % real / −0.36 % CPU**`; every case within ±2.6 %,
  `BM_EncodeTiff` at −2.54 %.

Flat, in other words — and the two ~2 % intermediate readings are revealed as
one session-drift excursion and its return. **This is the cleanest measurement
of the machine's between-run drift the plan has produced, and it is worth more
than either gate it was taken for.** The round-7 calibration said "do not read
a sub-5 % move as a finding without checking the untouched cases in the same
run"; this round adds: on a multi-commit phase, **also compare the endpoints**,
because a pair of intermediate gates can each show a 2-7 % move while the
composite shows nothing.

One further check specific to this change, since it is the only one in the plan
that could carry a genuine per-call cost: a `Result` returned through a virtual
would cost *constant absolute* time per call, so the cheap `/256` cases would
show the **largest relative** delta. They do not — `flat_tiff` moved equally at
both sizes, `jpeg_baseline` moved more at `/1024`, `pyr_none` more at `/256`.
No per-call-overhead signature. No alignment or `copt` hack was added.

## Phase 5d benchmark gate — TIFF internals, and a −7.5 % that is also not real (round 8)

Before = the Phase 5c "after" pair (`decode-5c-after.json` / `encode-5c-after.json`,
captured at `e2dc3143`, whose code tree is byte-identical to `c6e10878` — the
round-8 ADR fold-in moved no code); after = `decode-5d-after.json` /
`encode-5d-after.json` at `be347e3b`. Both `-c opt`,
`--benchmark_repetitions=20`, same machine, same session. **No regression on
either tier.**

**Decode tier: `OVERALL_GEOMEAN −2.35 % real / −2.10 % CPU.** Everything got
faster, including every case this round never touched. Medians: JPEG
`−1.66…−2.48 %`, JP2 `−0.05…−0.47 %` with `p > 0.36` (noise), and the four TIFF
cases `−1.80…−3.87 %`. The TIFF movement sits **inside** the band the untouched
JPEG cases moved, which is the untouched-case comparison the round-7
calibration established as the reliable check — per-case CV understates the real
uncertainty, so a uniform session-wide shift is what a `−2 %` geomean with every
case negative means.

**Encode tier: `OVERALL_GEOMEAN −2.00 % real / −2.25 % CPU`, and `BM_EncodeTiff`
is the largest mover at `−7.56 % real / −7.15 % CPU`** (p = 0.0275 / 0.0000)
against untouched JPEG/PNG cases at `−1.63…−1.79 %` and J2K at `−0.50 %`
(p > 0.13, noise). That is a ~5.8 % differential on the touched case — above the
maintainer's 5 % threshold, but in the **favorable** direction, so not a blocker;
it is recorded rather than chased.

It is worth saying why it is not treated as a real win. `encode_benchmark.cpp`
builds its master image **once, outside the timed loop** (`master()` is a
function-local static that reads `flat.tif`), and `state.PauseTiming()` excludes
the per-iteration copy — so the timed region is `img.write("tif", out)` **only**.
None of this round's read-path work (the removed per-call handler installs, the
guarded `TIFFTAG_JPEGCOLORMODE` request) is inside it, and the encode success
path's source semantics are provably identical: the conversions land exclusively
on failure returns, and the `static`-ness added to four private helpers removes
only an implicit `this`. On a ~20 ms case that leaves **binary layout** — case
(a) under the maintainer's round-5 decision rule: similar delta, provably
identical source semantics, record and accept. **No alignment or `copt` hack was
added**, per the same rule.

## Phase 5c benchmark gate — JPEG internals, and how to read a +2 % that is not real (round 7)

Before = the Phase 5b "after" pair (`decode-5c-before.json` /
`encode-5c-before.json`, captured at `4aae7a4e`); after = `decode-5c-after.json`
/ `encode-5c-after.json` at `e2dc3143`. Same machine, same `-c opt` build, same
`--benchmark_repetitions=20`.

**The raw numbers look like a regression and are not one.** Every JPEG case
moved up: `decode_tile/jpeg_baseline/256 +2.05 %`, `/1024 +2.42 %`,
`decode_thumb/jpeg_baseline +1.89 %`, `BM_EncodeJpegQ75 +1.65 %`,
`BM_EncodeJpegQ90 +1.86 %` — all with `p = 0.0000`, and all **exceeding their
own CV**, which in this run is unusually tight (0.55–0.90 % on the decode
cases). Taken alone that clears CLAUDE.md's conjunctive bar and would be a
finding.

It is not, and three independent pieces of evidence say so:

1. **The untouched cases moved more.** `decode_tile/flat_tiff/1024 +3.84 %`,
   `/256 +2.34 %`, `decode_thumb/flat_tiff +1.79 %`, `BM_EncodeTiff +3.80 %` —
   TIFF shares no code with this phase. `OVERALL_GEOMEAN +2.36 %` (decode) and
   `+1.56 %` (encode) with the touched cases sitting *below* the tier average.
   A change confined to JPEG's failure paths cannot slow TIFF decode by 3.8 %.
2. **The previous session moved the same amount the other way.** The Phase 5b
   run measured `OVERALL_GEOMEAN −2.28 %` (decode) on a phase that touched no
   decode case at all. The two runs bracket a ±2 % session-to-session envelope
   around the same code.
3. **A same-binary A/A control was run** (`decode-5c-after2.json`, back-to-back
   with `decode-5c-after.json`): the JPEG cases reproduce to **±0.13 %**,
   `OVERALL_GEOMEAN −1.07 %`. So *within* a session the harness is tight, which
   is exactly why the per-case CV is small and why CV alone under-states the
   real uncertainty. The drift is between sessions — thermal/DVFS/background
   state — not measurement noise inside one.

**And the shape is wrong for the mechanism being suspected.** A per-call
overhead (one extra `Result<bool>` construction and one non-inlined call per
`read()`) is *constant in absolute time*, so it would show up as a larger
relative delta on the cheaper case. Observed: `/1024` moved **more**
(`+2.42 %`) than `/256` (`+2.05 %`). That is the opposite signature. It is also
the right order of magnitude to dismiss on physics alone — one `std::expected`
construction against a 164 ms decode.

Read under the maintainer's round-5 decision rule this is case **(b)**: parity
in provably-equivalent code, machine drift, record and accept. It is well under
the ~5 % blocker threshold and carries no per-call-overhead signature. **No
alignment or `copt` hack was added**, per the same ruling. A same-session A/B
against a pre-phase worktree oracle was *not* built: the worktree exception the
maintainer granted was scoped to the `BM_ScaleHigh` investigation, and the
three-way evidence above settles this without it.

## Phase 5b benchmark gate — PNG internals, and a gap in the decode tier (round 7)

The "before" pair is round 6's "after" pair, reused unchanged: nothing in the
tree moved between `ef423c93` and the start of this phase (the ADR fold-in
rewrote SHAs but not one byte of code), so a recapture would have measured the
same binary. Same machine, same `-c opt` build, same
`--benchmark_repetitions=20`:

```
  …/scratchpad/bench/decode-5b-before.json  →  …/decode-5b-after.json
  …/scratchpad/bench/encode-5b-before.json  →  …/encode-5b-after.json
```

**Encode: `BM_EncodePng` median `−1.95 %` real / `−2.15 %` CPU
(`p = 0.0000`).** A speedup, and not a real one — every case in the tier moved
down in the same run: `BM_EncodeJpegQ75 −1.17 %`, `BM_EncodeJpegQ90 −1.10 %`,
`BM_EncodeTiff −3.69 %`, `BM_EncodeJ2k −1.70 %`, `OVERALL_GEOMEAN −2.59 %`.
Three of those four cases are untouched by this phase, so a uniform ~2 %
downward shift is this machine's cross-session drift and PNG sits in the middle
of it. No regression. The decode tier tells the same drift story even more
plainly: all 18 cases moved between `−0.4 %` and `−4.5 %`, `OVERALL_GEOMEAN
−2.28 %`, on a phase that touched no decode case at all.

**The gap, stated plainly: there is no PNG case in the decode tier.**
`decode_benchmark.cpp` covers `pyr-none.tif`, `pyr-zstd.tif`, `pyr-webp.tif`,
`pyr.jp2`, `baseline.jpg` and `flat.tif` — the six-variant matrix of the
`@sipi_bench_fixtures` archive, which ships **no PNG master**. CLAUDE.md's
hot-path rule says to add a benchmark when one does not exist, and that is not
a worker-sized action here: the archive is a 321 MB release asset on
`dasch-swiss/dsp-ci-assets`, fetched by `gh_release_archive` and pinned by
tag/asset/sha256 in `bazel/benchmark_fixtures_extension.bzl`, so adding a case
means regenerating it (`tools/benchmark/generate_fixtures.sh`), cutting a new
release and re-pinning. Recorded as a deferral for the maintainer rather than
worked around or quietly skipped.

What can honestly be said about PNG decode without that case: the retyped
returns are all on **failure** paths, the happy path gained exactly one
`Result<bool>` construction per `read()` call (not per pixel), and the
identical shape measured clean on the J2K decode tier in round 6.

## Phase 5a benchmark gate — J2K internals, both tiers clean (round 6)

The "before" pair was captured at `f3fd0a42` in round 5 (`-c opt`,
`--benchmark_repetitions=20`); the "after" pair was captured on the same
machine, same build config, same repetition count, after the last of the four
J2K commits (`ef423c93`):

```
  …/scratchpad/bench/decode-5a-before.json  →  …/decode-5a-after.json
  …/scratchpad/bench/encode-5a-before.json  →  …/encode-5a-after.json
```

**Decode: `OVERALL_GEOMEAN +0.0001` real (+0.01 %) / `+0.0042` CPU (+0.42 %).**
The three JP2 cases — the only ones this phase can have touched — all move
*down* on wall clock and up a fraction on CPU:

| case | median real | median CPU | U-test |
|---|---|---|---|
| `decode_tile/jp2/256` | `−0.41 %` | `+0.39 %` | `p = 0.1988` |
| `decode_tile/jp2/1024` | `−1.55 %` | `+0.83 %` | `p = 0.0499` |
| `decode_thumb/jp2` | `−1.14 %` | `+0.46 %` | `p = 0.1556` |

Every one sits inside the decode tier's documented `0.74–3.96 %` CV floor, and
CLAUDE.md's rule is conjunctive — a green U-test alone is not a finding, the
median shift must also clear the tier CV. It does not. `jp2/1024`'s `p = 0.0499`
is the only borderline U-test and it accompanies a **speedup**.

**Encode: `OVERALL_GEOMEAN +0.0093` real / `−0.0009` CPU.** `BM_EncodeJ2k` —
again the only case this phase touched — is `−1.45 %` real / `+0.50 %` CPU
against its own `3.85 % / 2.53 %` CV. The JPEG, PNG and TIFF encode cases moved
`+0.4…+1.0 %` in the same run despite **not being touched at all**, which is a
useful calibration: that is what this machine's cross-session drift looks like,
and the J2K case moved less than the untouched ones.

**Reading: no regression on either tier.** This is the result the hot-path rule
exists to check for — ADR-0024 records the genuine concern that `std::expected`
adds a tag check on every return where an exception is free until thrown. On
these paths it does not show, which is unsurprising: the returns being retyped
are all on *failure* paths, and the happy path gained one `Result` construction
per call, not per pixel.

## Extraction benchmark gate — result, and one open finding (round 4)

Post-extraction capture at `ae0a68f4`, same machine, same `-c opt` build, same
`--benchmark_repetitions=20`:
`…/scratchpad/bench/process-r4-after.json` (+ a second same-binary capture
`process-r4-after2.json`, used as the repeatability control).

`just bench-compare process-r3-before.json process-r4-after.json` →
**`OVERALL_GEOMEAN +0.0037` (+0.37 %)**, identical to the +0.37 % the two
*pre-extraction* baselines produce against each other. At the aggregate level the
extraction is free, as expected for a pure move.

**One case does not fit the noise story, and it is left open deliberately.**
`BM_ScaleHigh/1024` is **`+3.71 %`** (`p = 0.0000`) and `BM_ScaleHigh/256`
**`+1.53 %`**. Both clear the process tier's 0.22–1.30 % baseline CV, so
CLAUDE.md's conjunctive rule (U-test green **and** median shift above the tier CV)
is satisfied — this reads as a real delta, not noise. Three checks were run before
concluding anything:

1. **Within-session repeatability is tight.** `process-r4-after` vs
   `process-r4-after2` — the *same binary*, back to back — agrees to within
   ±0.5 % on every case (`ScaleHigh/1024` = `−0.14 %`). So run-to-run scatter
   cannot produce 3.7 %.
2. **Cross-session drift does not touch this benchmark.** Comparing the two
   pre-extraction baselines (`process-before` vs `process-r3-before`, different
   sessions, behaviorally identical binaries) moves `To8bps` `+2.20 %`,
   `ConvertToIccCmykToSrgb` `+2.06 %`, `RemoveAlphaChannel` `+1.82 %`,
   `Rotate45` `−1.54 %` — but `ScaleHigh/1024` only **`+0.03 %`** and
   `ScaleHigh/256` `−0.13 %`. `ScaleHigh` is one of the *stable* benchmarks
   across sessions, so the usual "different session" excuse does not apply here.
3. **The code is provably equivalent.** `git show 865f2c25:…SipiImage.cpp` vs
   `geometry.cpp`: `scale()`'s body differs only in reading geometry through
   `getNx()/getNy()/getNc()/getBps()`, reading pixels through
   `pixels_view().data()` instead of `pixels.data()`, and ending in one
   `set_pixels(std::move(out), …)` instead of `pixels = std::move(out); nx = nnx;
   ny = nny;`. Both targets carry the identical `copts = ["-fexceptions"]` and the
   same `@highway//:hwy` dep. The benchmark's own timed region is unchanged — the
   `PauseTiming()`/`ResumeTiming()` bracket around the source-image copy survives
   verbatim; only the call spelling changed.

**Reading of the evidence.** The added per-call work (`set_pixels`' checked size
computation, one out-of-line call) is *fixed cost*, so it would hit the small case
hardest. The measurement is the other way round — `/1024` is hurt more than
`/256` — so the extra time scales with pixel count and lives *inside* the hot
loop, not around it. Nothing in the diff changes that loop. `ScaleHigh` is the one
process benchmark dominated by a single Highway SIMD kernel (`resample.cc`), and
that TU changed which `cc_library` it is compiled into. The most probable cause is
therefore **binary layout / hot-loop alignment**, which routinely moves a tight
SIMD loop by a few percent without any semantic change — the classic
"wrong data without doing anything obviously wrong" effect.

**Status: not blocking, not silently passed.** The chunk landed because behavior
is provably preserved (goldens byte-identical, bodies equivalent), and the
layout hypothesis was carried into round 5 as an explicit open item rather than
absorbed into the ticked checkbox.

## `BM_ScaleHigh` — resolved by a same-session worktree A/B (round 5)

The maintainer authorized the oracle worktree. `git worktree add` at `865f2c25`
(detached), `bazel build -c opt //src:process_benchmark` there, then **both
binaries benchmarked back to back in one session on one quiet machine**, same
`--benchmark_repetitions=20`:
`…/scratchpad/bench/process-r5-oracle.json` → `…/process-r5-head.json`.

| case | round 4 (cross-session) | round 5 (same-session A/B) | U-test |
|---|---|---|---|
| `BM_ScaleHigh/1024` | `+3.71 %` | **`+3.79 %`** | `p = 0.0000` |
| `BM_ScaleHigh/256` | `+1.53 %` | **`+1.60 %`** | `p = 0.0000` |
| `BM_Rotate45` | `−1.54 %` | **`−2.69 %`** | `p = 0.0000` |
| `OVERALL_GEOMEAN` | `+0.37 %` | **`+0.35 %`** | — |

Every other case lands within `±0.7 %`. The delta **reproduces at the same
magnitude** against a provably-equivalent oracle, so this is the maintainer's
case (a): **a binary-layout / code-placement effect. Accepted and recorded.**

Three things make that reading solid rather than convenient:

1. **It is not a per-call-overhead signature.** The decision rule's blocking
   shape is a delta that stays *constant* across `/256` and `/1024`; the
   measurement is `1.60 %` vs `3.79 %`, i.e. the cost scales with pixel count and
   therefore lives inside the SIMD loop, not around the call. Nothing in the diff
   touches that loop.
2. **The effect is bidirectional in the same binary.** `BM_Rotate45` moved
   `−2.69 %` with `p = 0.0000` — a *speedup* of the same order, in the same
   pair of binaries. Added per-call work cannot make a benchmark faster; code
   placement routinely does both at once. This is the strongest single piece of
   evidence and it did not exist before the A/B.
3. **The aggregate is flat.** `+0.35 %` geomean, indistinguishable from the
   `+0.37 %` two behaviorally identical binaries produce against each other.

Below the 5 % blocking threshold, no per-call signature. **No alignment or
`copt` hack was added** — the ruling forbids chasing layout luck, and the right
response to a layout effect is to leave it alone.

## Decode tier — formally closed (round 5)

`just bench decode --benchmark_repetitions=20` on HEAD
(`…/scratchpad/bench/decode-r5-head2.json`) vs the `decode-r3-before` baseline:
**`OVERALL_GEOMEAN −0.51 %` real / `+0.06 %` CPU**. Every case sits inside the
decode tier's documented `0.74–3.96 %` CV floor except `decode_tile/jp2/256`
(`−5.99 %` real), and that one's CPU column reads `−1.63 %` — real and CPU
disagreeing by 4 points on the noisiest benchmark in the tier (`3.96 %` CV) is
wall-clock scatter, not a signal, and it is a *speedup* in any case. The tier is
unchanged, as expected for an extraction that moved no decode code. Bookkeeping
closed; the hot-path gate for Phase 2 is now complete on both tiers.

## Benchmark baseline (pre-extraction, `-c opt`, darwin-aarch64)

Captured at `3eb952b5` — i.e. on the tree *before* any processing-method
extraction — with `--benchmark_repetitions=20`. This is the "before" side of
the hot-path gate for the `image_processing` extraction.

**The raw JSON survived round 2 → round 3** (needed for the Mann-Whitney U-test
`just bench-compare` runs). Round 2 assumed the orchestration scratchpad is
per-round; it is not — it is keyed by *repo path*, not by round, so the files are
still there:

```
/private/tmp/claude-502/-Users-subotic--github-com-dasch-swiss-sipi--claude-worktrees-image-module/
  878cbccd-2847-4c00-bdf6-8331d4eaa480/scratchpad/bench/process-before.json
  878cbccd-2847-4c00-bdf6-8331d4eaa480/scratchpad/bench/decode-before.json
```

Treat this as a convenience, not a guarantee — `/private/tmp` is not durable
storage.

**Round 3 re-captured the baseline at `865f2c25`** — the exact pre-extraction
tree, mutator surface included — with the same `-c opt` build, the same
`--benchmark_repetitions=20`, and the same machine:

```
  …/scratchpad/bench/process-r3-before.json
  …/scratchpad/bench/decode-r3-before.json
```

**Use the `-r3-` pair as the "before" side of the extraction gate.** The round-2
pair is retained only as corroboration.

The two baselines agree: `just bench-compare process-before.json
process-r3-before.json` reports `OVERALL_GEOMEAN +0.0037` (**+0.37 %**), far
under the 3 % noise floor. Individual U-tests do come back `p = 0.0000` on some
cases (e.g. `BM_RemoveAlphaChannel`, median `+1.8 %`) — a useful calibration of
what this machine's *noise* looks like under 20 repetitions, and exactly why
CLAUDE.md's rule is conjunctive: a green U-test alone is not a finding, the
median shift must also exceed the tier's CV. Two identical binaries produce
"significant" p-values here.

The medians table below is from round 2 and remains valid for sanity checks.

Baseline CVs are the noise floor the decision rule compares against: process
tier 0.22–1.30 %, decode tier 0.74–3.96 % (`decode_thumb/jp2` is the noisiest at
3.96 %, `decode_tile/flat_tiff/256` 2.87 %). Per CLAUDE.md, trust a delta only
if the U-test is green AND the median shift exceeds that tier's CV; sub-3 % is
noise.

| process tier | median | | decode tier | median |
|---|---|---|---|---|
| `BM_ScaleFast/256` | 0.134 ms | | `decode_tile/pyr_none/256` | 0.290 ms |
| `BM_ScaleFast/1024` | 2.103 ms | | `decode_tile/pyr_none/1024` | 2.952 ms |
| `BM_ScaleMedium/256` | 0.886 ms | | `decode_thumb/pyr_none` | 1.270 ms |
| `BM_ScaleMedium/1024` | 14.107 ms | | `decode_tile/pyr_zstd/256` | 0.577 ms |
| `BM_ScaleHigh/256` | 10.168 ms | | `decode_tile/pyr_zstd/1024` | 3.287 ms |
| `BM_ScaleHigh/1024` | 22.190 ms | | `decode_thumb/pyr_zstd` | 2.255 ms |
| `BM_Rotate90` | 13.510 ms | | `decode_tile/pyr_webp/256` | 1.470 ms |
| `BM_Rotate45` | 128.712 ms | | `decode_tile/pyr_webp/1024` | 4.154 ms |
| `BM_Crop1024` | 2.083 ms | | `decode_thumb/pyr_webp` | 5.570 ms |
| `BM_To8bps` | 6.004 ms | | `decode_tile/jp2/256` | 6.054 ms |
| `BM_ConvertToIccAdobeRgb` | 41.540 ms | | `decode_tile/jp2/1024` | 18.348 ms |
| `BM_ConvertToIccCmykToSrgb` | 21.948 ms | | `decode_thumb/jp2` | 6.938 ms |
| `BM_RemoveAlphaChannel` | 2.387 ms | | `decode_tile/jpeg_baseline/256` | 163.976 ms |
| | | | `decode_tile/jpeg_baseline/1024` | 171.678 ms |
| | | | `decode_thumb/jpeg_baseline` | 114.026 ms |
| | | | `decode_tile/flat_tiff/256` | 0.934 ms |
| | | | `decode_tile/flat_tiff/1024` | 8.537 ms |
| | | | `decode_thumb/flat_tiff` | 63.035 ms |

## Design decisions taken during execution

- **`read_watermark` boundary: option A (explicit dep), not the leaf header.** The
  plan and ADR-0007 offer exactly two shapes. Chosen: the declaration moves into
  `image_processing/processing.h`, `//src/format_handlers` compiles its definition
  (`SipiIOTiff.cpp`) against that same declaration, and the call resolves at link
  time — the declare-here/define-there asymmetry `SipiImage::io` already uses.
  **The deciding fact is that the required edge already exists**: the call-site
  rewrites in `4a`/`4b` made every format handler call
  `Sipi::processing::crop/scale/convertToIcc/to8bps`, so
  `format_handlers → image_processing` is a dep the package needs regardless of
  where the watermark declaration sits. Option A therefore costs *zero* new edges,
  while the `output_sink`-style leaf header would add a package and a target to
  carry one free-function declaration — a new boundary for no enforcement gain,
  against the repo's KISS rule. Both BUILD docstrings state the asymmetry
  explicitly, and `src/image/BUILD.bazel`'s docstring was corrected: it no longer
  claims `//src/image` references `read_watermark`, because it no longer does.

- **Free operators go in `namespace Sipi`, not `Sipi::processing`.** ADL looks in
  the namespace of the argument type. `SipiImage` is in `Sipi`, so free
  `operator-`/`operator==`/… must live there for `a - b` to keep resolving. The
  named functions (`crop`, `scale`, `add_watermark`, `compare`, …) stay in
  `Sipi::processing`, which is what ADR-0007 specifies. The consequence worth
  knowing: any TU that compares or subtracts two images must now include
  `image_processing/processing.h`.

- **The const pixel accessor is `pixels_view()`, not the audit's `pixels()`.**
  `SipiImage` already has a data member named `pixels`, and C++ forbids a member
  function and a data member sharing a name in one class. Renaming the member was
  rejected as out-of-scope churn across all four handlers plus `SipiImage.cpp`.
  `pixels_view()` pairs with the ADR-named `pixels_writable()`. Recorded because
  ADR-0007 and the raw-pixel audit both write `pixels()`, and the difference will
  otherwise read as drift.

- **Naming follows the neighbour, not one house style.** The class already mixes
  conventions — camelCase (`getNx`, `setOrientation`, `getPhoto`) beside
  snake_case (`essential_metadata`, `ensure_exif`). The added members match
  whichever neighbour they sit with: `getEs`/`setEs`/`setPhoto` camelCase with the
  geometry accessors, `set_pixels`/`pixels_writable`/`pixels_view`/`set_icc`/
  `exif_writable` snake_case with the metadata ones. This mirrors the audit's own
  §4 table. Not a new inconsistency; an existing one respected rather than
  half-corrected in a chunk that is supposed to be purely additive.

- **`exif_writable()` is added ahead of its consumer, deliberately.** No
  processing method writes `exif`; the caller that needs it is the TIFF handler
  when the friendships come off in the later hub phase. It is included here
  because the raw-pixel audit flagged lazy-EXIF init as an open question that
  `pixels_writable()` alone does not answer, and the plan's checkbox reads
  "`pixels_writable()` + metadata setters". Keeping the whole surface in one
  reviewable commit beats dribbling it out.

- **Where the BUILD file sits in a `cpp/`-split package.** The two existing
  polyglot packages put the BUILD *inside* the language subfolder
  (`//src/throttling/cpp:memory_budget`,
  `//src/iiifparser/cpp/value_objects:iiifparser`) because their `rust/` half
  needs its own BUILD anyway. The plan, however, names the new packages
  `//src/error`, `//src/image`, `//src/image_processing`, `//src/cache`,
  `//src/format_handlers` — labels without the `/cpp` segment, and the
  acceptance criteria are written against exactly those labels. **Resolution:**
  a single-language C++ package keeps its BUILD at the *package root*
  (`src/<pkg>/BUILD.bazel`) and lists `cpp/`-prefixed source paths; the
  `strip_include_prefix = "/src/<pkg>/cpp"` + `include_prefix = "<pkg>"`
  twinning works identically from there. Genuinely polyglot packages keep a
  BUILD per language subfolder (existing precedent). Recorded because the two
  shapes now coexist deliberately and a reader will otherwise read it as drift.

- **Strays land flat in their destination package; the `cpp/` conversion is one
  later sweep.** The disposition table names `src/util/cpp/`, `src/ffi/cpp/`,
  `src/cli/cpp/` as stray destinations, but `util`, `ffi`, and `cli` are all
  still flat today. Two options: convert each destination package to `cpp/` as
  its stray arrives, or land the stray flat and convert every single-language
  package in one mechanical pass. Chose the second — converting per-arrival
  leaves the tree in a half-and-half state for several commits and makes the
  per-package `strip_include_prefix` churn land in commits whose subject is
  about something else. Cost is that the moved files get touched twice; the
  second touch is a pure path change with no content edit.

- **`SipiCommon.{h,cpp}` is deleted, not rehomed.** The disposition table sends
  it to `src/util/cpp/`, but both files are *entirely empty* — a licence header,
  one unused standard include, and an empty `namespace Sipi {}` block each. The
  only consumer (`src/formats/SipiIOJpeg.cpp`) includes the header and uses
  nothing from it. Relocating an empty TU just moves dead weight, so it is
  removed outright along with the stray include. Deviation from the plan's
  disposition table, recorded here rather than silently absorbed.

- **`SipiConf.h` and `SipiReport.h` live in `include/`, not `src/`.** The
  disposition table names only the `.cpp` halves (`SipiConf.cpp` -> `src/ffi/cpp/`,
  `SipiReport.cpp` -> `src/cli/cpp/`); their headers sit in the legacy
  `include/` tree behind `//include:headers` and must ride along, or the move
  leaves a split-brain pair. Same applies to whatever else `//include:headers`
  still carries for these two — check it when that chunk runs.

- **Hub-dissolution order: drain `:engine` of its non-image tenants before
  moving `SipiImage` out.** `:engine` today compiles six TUs, and four of them
  (`SipiCache`, `SipiCommon`, `SipiFilenameHash`, `resample`) turn out to have
  **zero** dependency on `SipiImage` — verified by reading their includes. Only
  `populate_from_image.cpp` includes `SipiImage.h`. But `SipiImage.cpp` *uses*
  `resample`, so carving `//src/image` while `resample.cc` is still a `:engine`
  TU would leave a transient `//src/image -> //src:engine` edge pointing at the
  very target being dissolved. Moving cache + the strays out first shrinks
  `:engine` to `{SipiImage, populate_from_image, resample}`, after which the
  `//src/image` carve dissolves `:engine` outright instead of inverting an edge
  into it. Executed order therefore reads: `error` -> `cache` -> strays
  (`util`/`ffi`/`cli`) -> `image` -> `image_processing` extraction.

- **Seed-corpus wiring (Phase 1).** Per-format corpus packages
  `src/formats/corpus/{tiff,jpeg,png,j2k}/` each expose a `seed_corpus`
  filegroup = `glob(["*"], exclude=["BUILD.bazel"])` (empty at first; this is
  where `fuzz-corpus-merge` checks minimized growth in, matching the iiifparser
  two-tier policy) **plus** an explicit per-format fixture filegroup added to
  `test/_test_data/BUILD.bazel`. Rationale: `cc_fuzz_test`'s `corpus` attribute
  takes labels, and `//test/_test_data` exposes only whole-tree filegroups
  today, so referencing individual small fixtures needs named filegroups there.
- **Chosen seeds** (smallest valid per format + the crafted malformed files):
  - TIFF: `exif_lens_specification.tif` (430 B) + 4 `malformed/tiff_*.tif`
  - JPEG: `HasCommentBlock.JPG` (64 KB — no smaller valid JPEG exists in the
    tree) + 4 `malformed/jpeg_*.jpg`
  - PNG: `mario.png` (2.5 KB) — valid seeds only, per the plan's deliberate
    no-crafted-PNG-fixture decision
  - J2K: `ycbcr16.jpx` (730 B), `gray_with_icc.jp2` (70 KB) +
    `malformed/palette_undersized_lut.jp2`

## Plan corrections (verified against `rules_fuzzing` 0.8.0 sources)

- **The plan's "`cc_fuzz_test`'s `dicts` attribute (one-line BUILD change)" is
  wrong twice over.** The attribute is `dictionary` (singular, one label), and
  it reaches libFuzzer only through `rules_fuzzing`'s Python *launcher*, which
  exports `FUZZER_DICTIONARY_PATH` (`fuzzing/tools/launcher.py:101`). This repo
  deliberately does not use the launcher — it executes `..._bin` directly so
  corpus growth survives the sandbox — so the attribute would be inert.
  **Resolution:** vendor the dicts as plain checked-in files and pass
  `-dict=<path>` explicitly in `just fuzz` / `fuzz.yml`. No BUILD attribute.
- **`<name>_corpus` is the clean seeding mechanism.** `cc_fuzz_test` generates a
  `<name>_corpus` target (`fuzzing_corpus`, `fuzzing/private/common.bzl:82`)
  that materializes the whole corpus — fixture seeds *and* the checked-in growth
  tier — into one flat directory under `bazel-bin`. Verified:
  `bazel build //src/formats/fuzz:tiff_decode_fuzz_corpus` yields 5 files. The
  `just fuzz` seeding step copies from there instead of duplicating the seed
  list in the justfile.

## Environment constraint discovered this round

**`bazel query` over a universe that reaches `@google_benchmark` cannot run on
this machine.** The box has no working DNS, so preloading the transitive
closure fails fetching `@libpfm` (`Unknown host:
netcologne.dl.sourceforge.net`), and the whole query errors out. This bit once
already: `bazel query 'rdeps(//..., //include:headers, 1)' 2>/dev/null` printed
nothing and was read as "no reverse dependencies", when in fact the query had
*failed*. A worker caught it.

Two consequences:

1. **Never run a `bazel query` with stderr suppressed.** An empty result and a
   failed load look identical.
2. **The plan's `bazel query` acceptance checks for the new boundaries**
   (`deps(//src/image_processing/...)`, `somepath(//src/image,
   //src/image_processing)`, `deps(//src/error/...)`) cannot be *executed*
   locally as written. They can still be recorded in ARCH-MAP as the stated
   invariants, but verifying them needs either CI (where fetches work) or a
   universe narrowed to exclude the benchmark targets. Substitute locally:
   grep every `BUILD.bazel`/`.bzl` for the literal label — deps are literal
   strings, so this is sound for "does anything reference X", though not for
   transitive questions.

## RESOLVED (round 10) — the `Result<void>` semantics question

The maintainer ruled on all three sub-questions, taking the journal's own
recommendation each time:

1. **Today's `false` → error value**; the identifiable "nothing to do"
   early-returns (the `geometry.cpp:258`/`:305` class) → **successful no-op**.
   One worker-visible rule: a `false` meaning *"the parameters or geometry made
   the operation impossible"* is an error; a `true`-commented *"nothing to do"*
   is success.
2. **Single-pass conversion.** Phase 6's remaining items execute together with
   Phase 8's caller work — no intermediate throw-adapters, no double-touching
   the call sites. The facade-retirement checkbox formally moves to Phase 8.
3. **The silently-discarded processing failures are a REAL DEFECT.**
   Success-path behavior and approval goldens stay byte-identical; the failure
   path starts reporting.

Round 10 applied all three in `86dd36fd` (`crop`) and found the ruling's
headline case was worse than described: the discarded return meant a client
asking for a region received the **full-size image** instead, silently. That
commit is a `fix:`, not a `refactor:`, for exactly that reason.

Also settled by execution rather than by ruling: (2) does **not** require one
atomic mega-change. See "Where the next round starts".

## Historical — the question as it was posed in round 9

## Open question for the maintainer (BLOCKING Phase 6's last two items, round 9)

**When an `image_processing` free function returns `false` today and every
caller ignores it, what does `Result` mean there?** This is the question that
stopped round 9 rather than guessing, and it gates both remaining Phase 6
items.

The facts. `processing::crop`, `scaleFast`, `scaleMedium`, `scale`, `rotate`
and `set_topleft` return `bool` (`src/image_processing/cpp/processing.h`), and
`geometry.cpp` alone has ~14 `return false` sites next to ~8 `return true`
ones. The `true`s are **not** uniform: some mean "done", and some mean
"nothing to do" (`:258` and `:305` both read `return true;// we do not have to
crop!!`). The `false`s look like genuine parameter/geometry failures. And the
call sites **discard the value** — e.g. `(void)Sipi::processing::crop(*img,
region);` in `SipiIOJpeg::read`. So a failed crop is silently ignored in
production today.

Converting these to `Result<void>` with `[[nodiscard]]` is the point of the
phase — it makes every one of those call sites *decide*. But that is a
**behavior change on the failure path**, not a retyping, and it is the first
one in this whole migration that is: every previous chunk held behavior
byte-identical. Three sub-questions, all of which want one answer rather than a
worker's judgement per site:

1. **Does today's `false` become an error value, or a successful "nothing to
   do"?** Both readings are present in the same function today.
2. **What do the ~80 call sites do with it?** The ones inside handlers are
   already `Result`-returning and can propagate. The ones in `src/cli/cpp/**`
   (38 sites), `src/ffi/cpp/**` (10) and the tests are in throwing contexts, and
   Phase 8 is what converts them. Propagating in Phase 6 means either doing
   Phase 8's caller work early, or standing up throw-adapters at those
   boundaries — which is exactly the shim ADR-0024 Decision 8 says never ships.
3. **Is a silently-ignored failure a bug to fix on this branch** (the standing
   main-owned-bug pattern, as with `lua_hardening` / `redact_paths` /
   `log_vformat`), or is preserving it the behavior-preservation contract?

Recommendation, for what it is worth: (1) `false` → error value, since the
"nothing to do" cases are identifiable and few; (2) do Phase 6's item alongside
Phase 8's caller conversion for the affected verbs, rather than adapting twice;
(3) treat the discarded returns as a real defect and say so in the commit that
closes them. But this is a maintainer call, and taking it wrong costs a rewrite
of ~80 call sites.

The same question shapes **`Image` construction → `Result` factories**: the
throwing constructors are called from CLI, ffi, tests and `image_processing`
alike, so "call sites updated" has the same reach.

## Open question for the maintainer (not blocking)

**Do the ADRs get path-scrubbed, or are they frozen?** Every package move this
round left stale paths behind in `docs/adr/**`: ADR-0003, 0005, 0007, 0019, and
0021 all still say `src/formats`, and ADR-0007 line 64 still describes
`//src/error` as "the `//src:sipi_top` role (today's shared-error-base
target)". They were left untouched throughout, on the reading that an ADR
records a decision against the tree as it stood. But ADR-0007 was itself
*refreshed to current fact* in round 1, which is the opposite policy — so the
repo does not have one consistent rule here. The repo-wide doc scrub is the
right place to settle it, and it wants a maintainer's call rather than a
worker's. Same question applies to `docs/archive/**` (which still cites
`include/SipiConf.h`), though "frozen" is the clearer answer there.

## Deferrals

- **Phase 6's "temporary `Result`→throw adapter at the facade retired" belongs
  in Phase 8** (round 9; recommendation, needs a maintainer's nod). Read
  literally against ADR-0024, "the facade" is `SipiImage::read`/`read_shape`/
  `write`, and retiring the conversion there means those three return `Result`
  — which immediately forces Phase 8's first three checkboxes (`serve_image.cpp`
  consumes `Result`, `image_handle.cpp` consumes `Result`, the CLI verbs consume
  `Result`) into Phase 6. That is not a scheduling nicety: the same ~50 call
  sites would otherwise be touched twice, and the intermediate state needs
  throw-adapters at the ffi and CLI boundaries, which is precisely the shim
  Decision 8 says never ships. Note the Decision-8 adapter itself — the
  per-handler wrap layer — **is** already gone; it was retired by the Phase 5e
  vtable flip. What remains at `SipiImage` is the facade's own throwing
  contract, which is a different thing and is Phase 8's subject. Recommendation:
  move the checkbox.
- **`detect_leaks` experiment (Phase 1).** Cannot run locally: LSan's
  `detect_leaks` is a Linux ASan feature and this Mac's local ASan link is
  broken. Implementing the plan's expected outcome up front —
  `ASAN_OPTIONS=detect_leaks=0` on the PNG/JPEG ASan-paired passes only, TIFF
  and J2K keep it on — and confirming it on the first nightly `fuzz.yml` run.
  If TIFF/J2K report leaks there, the decision is revisited then.
- **libtiff per-handle diagnostic handlers (`TIFFOpenExt`) — recommended, not
  taken; wants a maintainer decision** (round 8). Vendored libtiff is **4.7.1**
  (`MODULE.bazel:708`), which ships `TIFFOpenExt` +
  `TIFFOpenOptionsSetErrorHandlerExtR`, whose callback carries a
  `void *user_data`. That is the only mechanism by which a libtiff error can
  reach the **per-call `Result`** instead of only the process-global log
  side-channel. `SipiIOTiff.cpp` uses plain `TIFFOpen` at all four sites
  (`:375` watermark, `:937` `read`, `:1559` `read_shape`, `:1739` `write`).

  Deliberately **not** done in round 8's handler chunk, for three reasons worth
  recording rather than re-deriving. (a) It is a different concern from
  re-enabling the callbacks — the handler chunk makes the diagnostics *safe and
  visible*; per-handle capture changes *where the text lands*. (b) It would move
  **error message text** at many sites, and every chunk in this migration so far
  has held message text byte-identical; that is the property the whole phase is
  verified against. (c) It needs a per-call capture buffer plumbed through
  `user_data` and a decision about what happens when libtiff reports several
  errors for one file (first wins? last? concatenated?) — a design call, not a
  mechanical one. Recommendation: take it as its own change *after* the TIFF
  internals are on `Result`, so the capture has somewhere to land and the diff
  is legible. ADR-0024's TIFF bullet already anticipates it ("before or as part
  of converting them to populate a `Result`'s error").
- **The decode benchmark tier has no PNG case, and adding one is a maintainer
  action. Ruled on in round 8: the gap is ACCEPTED for this PR**, and the
  archive regeneration is a separately scheduled maintainer action rather than
  a blocker on this branch. The record below stands as the description of what
  closing it would take. `@sipi_bench_fixtures` ships a six-variant matrix
  (`pyr-none.tif`, `pyr-zstd.tif`, `pyr-webp.tif`, `pyr.jp2`, `baseline.jpg`,
  `flat.tif`) with **no PNG master**, so `decode_benchmark.cpp` cannot cover the
  PNG decode path. CLAUDE.md's hot-path rule says to add a benchmark when none
  exists; here that means regenerating a 321 MB release asset on
  `dasch-swiss/dsp-ci-assets` via `tools/benchmark/generate_fixtures.sh`,
  cutting a release, and re-pinning tag/asset/sha256 in
  `bazel/benchmark_fixtures_extension.bzl`. Out of a worker's reach and not a
  decision an orchestrator should take. Phase 5b's PNG decode change was gated
  on the encode tier (`BM_EncodePng`, clean) plus the structural argument that
  every retyped return is on a failure path; the same gap will apply to any
  future PNG decode hot-path work, so it is worth closing deliberately rather
  than re-discovering.
- **`app14_transform` must not ride inside the friend-removal change.** The audit
  flags it as a genuine behavior change (JPEG inverts CMYK/YCCK at decode;
  downstream sees standard CMYK) dressed as a mechanical refactor. The plan
  already schedules it for the hub retyping; keep it a separate commit there
  with its own approval-golden verification, never folded into the mutator-
  surface change.
- **Lazy EXIF init needs a setter, not an accessor.** `ensure_exif()` delegates
  through a private method; the public mutator surface has to expose a setter
  that performs the lazy initialisation, or the handlers cannot drop their
  friendship. Resolved by the audit's proposed API; no decision needed.
- **`fuzz-corpus-merge` for codec targets is imprecise on first use.** The
  checked-in tier (`src/formats/corpus/<fmt>/`) starts empty and the fixture
  seeds live in `test/_test_data`, so `-merge=1 <checked-in-dir> <artifact>`
  cannot see the fixtures' coverage and may propose inputs the fixtures already
  cover. Accepted: the recipe stops at a diff for maintainer review, and
  nothing exercises this path until a nightly has run on `main`. Documented in
  `fuzzing.md` rather than engineered around.

## RESOLVED (round 12) — the maintainer ruled **reject** (option 1); landed in `da0914f7`

The ruling picked the journal's own recommendation: the `<= 1`-axis guard
returns `kMalformedInput` and `ScaleToOnePixelDoesNotCrash` was rewritten to
assert the rejection, with `1,` and `,1` covered alongside. **One part of the
ruling is not yet delivered:** it says "→ HTTP 400 at the seam", and that
cannot follow from the error code alone, because `HttpStatusClass` currently
has a single variant and `status_for` maps everything to `InternalError`.
Delivering a 400 needs either a new status class plus a code that only this
case uses, or remapping `kMalformedInput` wholesale — and the second would
turn every corrupt-*file* rejection (oversized dimensions, undersized palette,
unreadable TIFF tags) into a 400, i.e. blame the client for the repository's
own content. Left for a one-row policy ruling. The original analysis follows.

## BLOCKER (round 11, chunk `11E`) — SIPI serves a full-size image for a IIIF size of `1,1`, and a test pins it

**This one needs the maintainer, and it is not an error-model question.** It is
a live, user-reachable IIIF behavior defect that the migration walked into.

**The finding.** All three resamplers open with

```cpp
if (nnx <= 1 || nny <= 1 || nx <= 1 || ny <= 1) {
  log_warn("scale...: degenerate dimensions (src=%zux%zu, dst=%zux%zu), skipping", ...);
  return false;
}
```

and **every caller discards that `false`** — the four handlers, the Lua seam,
and both `compose.cpp` operators. So a request whose target width or height is
one pixel is not resampled, and the caller serves **the image at its original
size, with HTTP 200**.

**It is reachable, not theoretical.** `SipiSize`'s constructor rejects only a
parsed dimension of exactly zero; `get_size`'s `PIXELS_XY` / `PIXELS_X` /
`PIXELS_Y` branches pass a `1` straight through unclamped. So
`.../full/1,1/0/default.jpg` — and `1,` and `,1` — land on the guard today.
A 1-pixel thumbnail request returns the full image.

**Why the migration did not just fix it.** Converting the guard to
`kMalformedInput` (as `crop` does for a degenerate region, and as the standing
ruling would suggest) fails a **pre-existing** test:

```
[  FAILED  ] SipiImage.ScaleToOnePixelDoesNotCrash
Expected: img.read(leavesSmallNoAlpha, region, size) doesn't throw
  Actual: throws ... "scale: degenerate dimensions (src=864x857, dst=1x1)"
```

That test came in with `ba84f8b2 fix: prevent segfault in bilinn() bilinear
interpolation during image scaling`, and the `"skipping"` wording in the log
says the author chose to skip rather than fail. So this is **not** the
`crop` case: crop's caller discarded an answer the function meant as a failure;
here the function was written to mean "nothing to do". Turning it into an error
would change a request that returns 200 today into one that fails — a
user-visible IIIF contract change, with a test standing in front of it. The
standing rulings forbid a chunk taking that on its own.

**What round 11 did.** Landed the conversion with the degenerate arm
**exactly as it behaves today** (`log_warn` + success, image untouched), and
converted only the unambiguous failures in the same functions — unsupported
bits/sample and pixel-buffer size overflow — to error values. No test edited.
No user-visible behavior changed.

**The decision needed.** Three ways out, and they are genuinely different
products:

1. **Reject it** — `1,1` becomes `kMalformedInput` → HTTP 400. Honest, matches
   `crop`'s treatment of a degenerate region, and needs
   `ScaleToOnePixelDoesNotCrash` rewritten to assert the rejection (its intent
   — "does not segfault" — is still met).
2. **Support it** — make the resamplers handle a 1-sample output axis. This is
   what a client asking for a 1-pixel thumbnail actually wants, and IIIF does
   not forbid the request. Costs real work in `bilinn` and the separable
   kernel, which is why the original segfault fix guarded instead.
3. **Clamp it** — treat `1` as `2` at the size layer. Cheapest, but silently
   returns something other than what was asked for, which is the same class of
   dishonesty as today.

Recommendation if a tiebreak is wanted: **(1)**, as the smallest honest change,
with (2) as a follow-up if a real client needs 1-pixel output. But this is a
product call on a public IIIF surface and the maintainer owns it.

## Side findings

### BLOCKER (round 14, chunk `14F`) — SIPI's PNG writer corrupts EXIF and IPTC, and since this branch the reader refuses the result

**SIPI cannot read back a PNG it wrote itself, if the source carried EXIF.**
Found by chunk `14F` while adding the read-back assertion the four-channel
geometry question needed, then confirmed at the byte level.

The writer puts the raw binary metadata blob into a libpng **text** chunk:

```cpp
exif_buf = exif->exifBytes();
chunk_ptr.add_zTXt(exif_tag, (char *)exif_buf.data(), exif_buf.size());
```

libpng writes a `zTXt` payload as a C string, so the blob is cut at its first
NUL. An EXIF blob begins `II*\0` (or `MM\0*`), so **three bytes survive**.
Verified directly on `sipi convert cmyk.tif out.png -F png`, decompressing the
chunk out of the file:

```
zTXt b'Raw profile type exif'  payload len 3   first bytes: b'II*'
tEXt b'Raw profile type iptc'  payload len 0   first bytes: b''
iTXt b'XML:com.adobe.xmp'      payload len 60  (intact — XMP is text)
```

`II*` is, byte for byte, the payload the crafted `jpeg_exif_truncated.jpg`
fixture uses to represent a corrupt EXIF block. IPTC fares worse: zero bytes
survive, and libpng downgrades the empty `zTXt` to a `tEXt`.

**Why it is a blocker for this PR specifically.** The corruption is
main-owned and old. What is new is the consequence: round 13 made a PNG whose
EXIF chunk does not parse **fatal** (`1fe886e6`), so
`sipi convert x.tif y.png && sipi convert y.png z.tif` now *fails* where it
used to succeed while silently dropping the metadata. The branch turned a
quiet data-loss bug into a hard failure on SIPI's own output, and no test
noticed because **nothing in the suite reads back a PNG that SIPI wrote**.

This also retro-explains round 13's chunk `13B`: `sample_with_icc.png` carried
a truncated EXIF `zTXt` record, which was stripped from the fixture. That
fixture was SIPI output, and the truncation was this bug's fingerprint — the
symptom was treated, not the cause.

**The decision the maintainer owns** is the encoding, because there is no
"just fix it" that leaves output bytes alone:

1. **A real `eXIf` chunk** (PNG 1.5 / libpng ≥ 1.6.32, `png_set_eXIf_1`) — the
   standards-correct home for EXIF in a PNG, and a binary-safe one.
2. **The ImageMagick/ExifTool convention** the key name already claims:
   `Raw profile type exif` is supposed to hold *hex-encoded ASCII* with a
   length header, not raw bytes. This would make SIPI's PNGs interoperable
   with the tools that read that key, and needs the reader taught to decode it.
3. Something else, plus the matching IPTC decision (`Raw profile type iptc`
   has the identical problem and loses everything).

Whatever is chosen changes the bytes SIPI writes, so the PNG approval goldens
move — which is why this is not a chunk I could take unilaterally under the
"goldens stay byte-identical" rule this round otherwise held to.

### BLOCKER (round 14, chunk `14B`) — a 5.7 KB JP2 spins Kakadu's box parser for at least five minutes inside `read_shape`

The nightly J2K leg reported `libFuzzer: timeout after 37 seconds`, which the
brief allowed might be "a pathologically slow but valid decode". It is not.
Replayed locally on the `-c opt` instrumented binary with the timeout raised to
290 s, the input `timeout-7e09ed207028df93676ff1568eca38c6abdcc091` (5745
bytes) ran **293 s wall / 289 s user and had still not finished** — CPU-bound,
not blocked. The stack is the same every time it is sampled:

```
fseeko → kdu_supp::jp2_input_box::read → jp2_input_box::read_box_header
       → jp2_input_box::open → jx_source::finish_jp2_header_box
       → jx_source::parse_next_top_level_box → jx_source::get_codestream
       → jpx_source::access_codestream → Sipi::SipiIOJ2k::read_shape
```

The trigger is visible in the file's box chain: the `jp2h` superbox declares
1007 bytes and its `ihdr` + `colr` children account for 973, leaving a trailing
child whose **length field and type are both `0x00000000`**. A zero length
means "extends to end of file" at top level; as a `jp2h` child it gives Kakadu
a sub-box that consumes nothing, and the walk re-opens it forever, seeking on
every iteration.

**Why this is a blocker rather than a chunk.** The loop is inside Kakadu, which
is a licensed third-party binary-source dependency SIPI cannot patch. Nothing
in `SipiIOJ2k` loops here — SIPI makes one `access_codestream` call. So the
only fixes available are design decisions the maintainer owns:

1. **A structural pre-pass over the JP2 box chain** before handing the file to
   Kakadu, rejecting a child box whose length is smaller than its header (and
   the zero-length-inside-a-superbox case). This is a second container parser
   in SIPI, which the no-defense-in-depth rule normally forbids — but it sits
   at a genuine input boundary.
2. **A decode watchdog** at the seam (a wall-clock cap on a single decode),
   which is a much wider change and affects legitimate slow decodes.
3. **Accept it** and let the nightly J2K leg fail whenever mutation rediscovers
   this shape.

**Do not add this input to the seed corpus.** Every `just bazel-test` replays
the corpus, so pinning it would hang the test suite rather than fail it. The
reproducer lives in the CI artifact `fuzz-crashes-j2k` of run 33254456688.

**Severity, stated honestly.** `read_shape` is on the production request path,
so a file of this shape in the repository hangs a decode thread indefinitely
rather than returning an error — an availability bug, not memory corruption,
and it requires the file to already be in the repository (DSP ingests through
dsp-ingest, so the file would have to survive that path first).

### `validate_decode_dims` bounds each dimension but never their product (round 14, chunk `14B`)

Surfaced by the TIFF OOM triage and left as a maintainer question. The guard
caps `nx`/`ny` at `1 << 17` and `nc` at 32, which permits a claimed decode of
`131071 × 131071 × 32 × 2` bytes — the per-dimension cap does not bound the
allocation at all. In the server that does not matter: `serve_image.cpp`
acquires a `MemoryBudgetGuard` sized from the shape probe before the full-lane
decode, so an over-large claim is admission-controlled. **The CLI verbs have no
such budget** — `sipi convert`/`verify` on a 275-byte crafted TIFF will simply
try to allocate ~3.9 GB. Whether that is worth a total-size cap is a genuine
trade-off (SIPI legitimately serves gigapixel scans, which is exactly why the
bound lives at the seam and not in the codec), so it is recorded rather than
taken.

### `sample_with_icc.png` carries two `IEND` chunks (round 13, chunk `13B`)

Found while walking the fixture's chunks to strip its EXIF record: the file
ends with `IEND` twice. It is **pre-existing** — the copy in `HEAD` before this
round's edit has it too — and harmless, because every PNG decoder stops at the
first `IEND`, which is why it has gone unnoticed through every approval run.
Left alone deliberately: this round's edit removed exactly 46 bytes and nothing
else, and the value of that claim is that it is exactly true. Whoever
regenerates this fixture should drop the duplicate.

### `bazel run` on a fixture generator writes into the sandbox (round 13, chunk `13D`)

`bazel run //test/unit/fixtures:generate_malformed_images -- <relative path>`
appears to succeed and produces nothing in the workspace, because the binary
resolves the relative path inside the runfiles directory. The invocation that
works is `./bazel-bin/test/unit/fixtures/generate_malformed_images
"$(pwd)/test/_test_data/images/malformed/"` — an absolute path, no `bazel run`.
The README documents the relative form, which is the form that silently does
nothing. Worth correcting there.

### `SipiIOPng::read` leaks the read structs on its metadata-failure arms (round 12, chunk `12C`)

The write path's leak was fixed this round (`12A`); the **read** path has the
same shape and was not. `SipiIOPng::read`'s ICC and IPTC parse-failure returns
(around lines 269 and 289) return without `png_destroy_read_struct`, while the
success path and the earlier arms do destroy. The chunk that added a new error
return next to them matched the local discipline rather than being the only
correct arm in the function — the same call `11C` made, and the same reason.

It wants the same treatment `12A` gave `write`: one RAII guard over
`png_ptr`/`info_ptr`, declared before the `setjmp`, after which every arm is
correct by construction. Cheap, mechanical, and the second half of a fix that
is currently half-applied — which is the weakest state to leave it in.

### `SipiImage::getPixel` / `setPixel` throw raw `int`s (round 11, chunk `11I`)

Noticed while enumerating every throw in `src/image/cpp/`, out of that
chunk's scope, not acted on. These two accessors signal an out-of-range
coordinate by `throw`ing a bare `int` — not a `SipiImageError`, not a
`SipiValueError`, not any type the seams catch. A `catch (const
SipiImageError &)` will not see it; `sipi_guard`'s catch-all will, and will
turn it into a generic 500 with nothing to report.

Whether that matters depends on reachability, which was not traced. If the
coordinates are ever request-derived it is an input failure that should be a
value; if they are only ever loop indices it is an invariant, and should at
minimum throw a type the codebase recognises. Either way, throwing a
primitive is not a contract anyone can catch deliberately. **Maintainer-owned.**

### Watermarking a 16-bit image silently does nothing (round 11, chunk `11F`)

`Sipi::processing::add_watermark` blends only when `bps == 8`. Its other arm is

```cpp
} else if (bps == 16) {
  // 16bps support was never really finished, left unimplemented
}
```

so a watermark requested on a 16-bit image is **not applied, and the request
succeeds** — the client gets an unwatermarked image with HTTP 200, and the
operator gets no signal. Left untouched this round on purpose: the comment
shows this is a known, deliberate feature gap rather than a discarded failure,
and erroring would change a currently-succeeding request (the same reasoning
that stopped the degenerate-scale arm above).

It is worth a maintainer's attention for a reason the error model does not
cover: a watermark is a provenance/policy control, and *silently* omitting one
is a different kind of problem from silently returning the wrong size. Either
implement the 16-bit path or make the omission visible. Not a migration
decision.

### `SipiIOPng::write`'s error paths are inconsistent about destroying the write struct (round 11, chunk `11C`) — **RESOLVED round 12, `5a7097f3`**

The maintainer took it under the standing main-owned-bug pattern and it landed
as the single RAII guard this finding recommended (chunk `12A`). One correction
to the finding itself: it enumerated **four** leaking arms; the guard's grep
found **five** (the `icc_LAB` `convertToIcc` failure was missed here). The
original text follows.

Not this migration's business to fix, but found while verifying a call site's
cleanup and worth someone's attention. `SipiIOPng::write` has five early error
returns. The success path at the end of the function does
`png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1)` **and**
`png_destroy_write_struct(&png_ptr, &info_ptr)`. Of the error returns, one
(`:519`) calls `png_destroy_write_struct`; three (`:503`, `:512`, `:557`, and
now the `removeExtraSamples` site at `:538` which was deliberately mirrored on
`:557`) call only `png_free_data` and return, **leaking the `png_ptr` /
`info_ptr` structs themselves**. `png_free_data(..., PNG_FREE_ALL, -1)` frees
the attached data, not the structs.

The new call site was written to match its nearest sibling rather than to be
independently correct, which is the right call for a migration chunk — being
the only correct error path in a function full of leaking ones is worse than
being consistent, and a half-fix invites a reviewer to think the rest are fine.
But the function wants a single RAII guard over `png_ptr`/`info_ptr` (the same
treatment the 2026-07 RAII stack gave elsewhere), after which every arm is
correct by construction. **Maintainer-owned; not taken this round.**

- **`crop` reports success without cropping when `bps` is neither 8 nor 16**
  (round 10, chunk `10F`). Both overloads end with an `if (bps == 8) { … }
  else if (bps == 16) { … } else { // clean up and throw exception }` — and
  that `else` branch contains **only the comment**. No code, no throw. Control
  falls straight through to the success return, so an image with an
  unsupported sample depth is silently left uncropped and the caller is told
  everything worked. It is the same defect class the commit that found it
  closes at the call sites, one level further in.

  Not fixed, deliberately: it needs a decision about what an unsupported
  `bps` *means* here (ruling (1) says "parameters made the operation
  impossible" is an error, which points at returning one), and it is not
  reachable from the current corpus — `bps` is 8 or 16 everywhere the
  handlers produce. Recommendation: return `kUnsupportedFormat` with the
  actual `bps`, in the chunk that converts the remaining geometry functions,
  since the same dangling-`else` shape may well appear in `scale` and
  `rotate` too — **check for it there rather than re-discovering it**.

- **The four codec handlers disagree about whether malformed embedded metadata
  is fatal, and nobody chose that** (round 10, chunk `10B`). For an
  unparseable IPTC block: J2K, TIFF and JPEG log and serve the image without
  IPTC; **PNG fails the whole read**. PNG's EXIF site in the same loop
  swallows the failure *silently* (an empty `catch` with a
  `// TODO: better error handling – now we nothing at all`). So one handler
  has three different policies inside one `for` loop.

  Every one of these was preserved exactly in round 10 — the migration
  converts the *mechanism*, and changing which inputs SIPI serves is a
  separate decision. **The decision is the maintainer's and it is worth
  taking deliberately**, because the answer looks obvious in one direction:
  a corrupt IPTC or EXIF sub-block is not a reason to refuse an otherwise
  decodable image, and an archival server that drops the block silently is
  worse than one that says so. Recommendation: harmonize on
  *log-and-continue*, with a real log line at every site (no empty catches),
  as its own `fix(format_handlers):` with sign-off — it changes what SIPI
  serves for a real class of input, so it must not ride inside a refactor.

  **This path has no test coverage in any format.** Nothing in `test/unit` or
  `test/approval` mentions IPTC; the codec fuzz harnesses this same plan added
  are the natural place to pin it once the policy is settled.

- **SIPI never parses or validates XMP, and nothing in the plan or the ADR
  knew that** (round 10, chunk `10A`). Every XMP packet a codec finds is
  stored verbatim and handed back verbatim; `Exiv2::XmpParser::decode` has
  been disabled behind an unconditional `return;` for years, on a thread-safety
  concern about Exiv2 0.25. Three consequences worth a maintainer's eye, none
  of them acted on in this branch:
  1. **A malformed XMP packet is accepted and re-emitted unchanged.** That is
     arguably the *right* behavior for an archival server — it is lossless —
     but it is not a decision anyone is currently making on purpose, and it is
     not documented outside `xmp.cpp` until this commit.
  2. **`SipiImage`'s stream dump prints an empty `XMP-Metadata:` section.**
     `operator<<` returns the stream untouched. Now commented as such rather
     than looking like a bug.
  3. **The thread-safety premise the disabling rests on is very likely
     stale, and this is the part that wants a maintainer.** The deleted
     comment blamed "Exiv2 Version 0.25", promising to "try again with Exiv2
     v0.26". `MODULE.bazel:652` pins **`exiv2-0.28.5`** — three minor versions
     past the promised retry point. More telling, the mechanism that makes the
     parser thread-safe is *already wired up*: `src/ffi/cpp/startup.cpp:41`
     calls `Exiv2::XmpParser::initialize(Sipi::xmplock_func,
     &Sipi::xmp_mutex)` at startup and `:55` calls `terminate()` at shutdown,
     both using SIPI's own mutex. So SIPI pays for XMP thread-safety and then
     does not use the parser it protects. Establishing whether parsing can be
     switched on is a functional change to XMP handling — new failure modes on
     malformed input, and a possible change to what SIPI re-emits — so it is
     deliberately **not** taken here. It wants its own issue.

     **A caution for whoever takes it:** turning parsing on would make XMP
     construction fallible for the first time, which is exactly the `Result`
     factory this chunk declined to build. Build it then, with real callers,
     rather than speculatively now.

- **The four-channel PNG encode leaves geometry inconsistent with the pixel
  buffer** (round 9, found while removing the PNG friendship).
  `SipiIOPng::write`'s `nc == 4` fallback branch (the one reached when the
  image has four channels but no extra-samples entry, i.e. CMYK rather than
  RGBA) calls `processing::convertToIcc(..., icc_sRGB, 8)` and then sets
  `nc = 3, bps = 8` **without resizing or re-checking the pixel buffer**. If
  `convertToIcc` does not itself replace the buffer with a correctly-sized
  three-channel one, everything downstream of that line indexes a buffer whose
  real geometry disagrees with `nx*ny*nc*bps/8`.

  **Preserved exactly, deliberately** — chunk `9D` routed it through the new
  `set_geometry` precisely so the behavior would not move inside a
  friend-removal commit. Whether `convertToIcc` closes the gap was not
  established; it is the one thing to check before calling this benign. Worth
  a `fix:` of its own if it does not, and worth a comment saying why it is safe
  if it does.

- **`SipiIOTiff::separateToContig(SipiImage *, unsigned int)` is dead code**
  (round 8, found while chunking the write path). The *member* overload
  (`SipiIOTiff.h:87`, defined at `SipiIOTiff.cpp:2464`) has **zero callers**
  anywhere in `src/` or `test/`. What is actually live is the pair of
  file-static **free function templates** of the same name (`:553` for
  `unique_ptr<T[]>`, `:569` for `vector<T>`), called from `read_standard_data`
  (twice) and `read_tiled_data` (once). The name collision is why this is easy
  to miss: a grep for `separateToContig` returns seven hits and only the member
  one is dead.

  It carries one `throw` (`:2499`, "Bits per sample not supported: …"), which is
  why it matters here — it is the only `throw` left in this file that no chunk
  converts, and a reader counting throw sites will trip over it. **Not deleted:**
  removing a private member with no callers is safe, but it is a deletion rather
  than a conversion and belongs to a cleanup pass with a maintainer's nod, not to
  a mechanical `refactor:` in this series. Recommended: delete it (git history
  preserves the bytes), either in the hub phase's sweep or as its own small
  `refactor(format_handlers):`.

  **RESOLVED, round 9** (`3973df30`). The maintainer ruled: delete it. Landed as
  its own small `refactor(format_handlers):`, chunk `9A`.

- **`TIFFSetDirectory(tif, 0)` silently discards `TIFFTAG_JPEGCOLORMODE`, and
  that is why JPEG-compressed TIFFs fail to decode** (round 8, surfaced the
  moment the libtiff error handler went live). `sipi convert` on
  `knora/tiffJpegScanlineBug.tif` now prints, and then fails:

  ```
  libtiff error (module TIFFReadScanline): scanline oriented access is not supported
    for downsampled JPEG compressed images, consider enabling TIFFTAG_JPEGCOLORMODE
    as JPEGCOLORMODE_RGB.
  Error reading image: … TIFFReadScanline failed on scanline 0, dimensions=7197x5441 …
  ```

  **Pre-existing on `main`, not caused by any chunk in this round** — verified
  three ways: the fixture is *named* for the bug; `sipiimage_test.cpp:412`
  carries a `// BROKEN! See the remark about "TIFFSetDirectory(tif, 0);" in
  SipiIOTiff.cpp` banner over a **commented-out** `TiffJpegAutoRgbConvert` test;
  and `test/e2e/tests/iiif_compliance.rs:1661` (`tiff_jpeg_compression_input`)
  explicitly accepts a 500/400 as "known issue". The re-enabled handler did not
  break it, it made it *legible* — which is the clearest single argument for the
  chunk.

  The mechanism, now nameable rather than a "remark": `read` sets
  `TIFFTAG_JPEGCOLORMODE` right after `TIFFOpen`, then `read_resolutions`
  (`SipiIOTiff.cpp:883-903`) walks the pyramid with `TIFFReadDirectory` and ends
  with `TIFFSetDirectory(tif, 0)`. `TIFFSetDirectory` **re-reads** the directory,
  which re-binds the compression codec, and `TIFFInitJPEG` resets
  `jpegcolormode` to `JPEGCOLORMODE_RAW` (`tif_jpeg.c:2808`). The setting made at
  open time is gone before the first scanline is read.

  **Not fixed here, deliberately.** The one-line repair (re-request the tag after
  `read_resolutions` returns) is a genuine **behavior** change — a class of files
  that errors today would start decoding — which flips the branch that e2e test
  takes and wants its own `fix:` and its own maintainer decision. Recorded so the
  next person does not have to re-derive the mechanism from a two-word comment.

  **RESOLVED, round 9** (`084b7051`). The maintainer ruled: fix it on this
  branch. One correction to the analysis above, found while fixing: the repair
  is **not** one line. Re-requesting the tag after `read_resolutions` is only
  half of it — the pyramid-level `TIFFSetDirectory(tif, level)` re-binds the
  codec too, and `TIFFTAG_PHOTOMETRIC` still reports the on-disk YCbCr while
  libtiff now hands back RGB, so `photo` had to follow the decoded layout or the
  *JPEG output* of such a file comes out wrong. That second half was latent and
  unobservable precisely because no file of this class ever decoded far enough
  to reach it. See chunk `9B`.

- **`icc_buffer_guard` is a genuine, pre-existing violation of ADR-0024's
  volatile-locals rule** (round 7, found while converting the JPEG decode).
  `SipiIOJpeg.cpp:496` declares `unsigned char *icc_buffer_guard = nullptr;`
  **before** the `setjmp`, assigns it **inside** the risk window during marker
  parsing, and the landing block reads it (`free(icc_buffer_guard)`). C's
  `setjmp`/`longjmp` contract (C23 7.13.2.1) only guarantees a defined value
  after landing for an automatic local that is `volatile`; a non-`volatile`
  local modified in the window has an **indeterminate** value, so the `free()`
  can be handed either the live pointer, a stale `nullptr`, or garbage. In
  practice every mainstream backend keeps it in memory, which is why this has
  never been seen to bite — but "in practice" is exactly what the ADR rule
  exists to stop relying on. The one-token fix is
  `unsigned char *volatile icc_buffer_guard = nullptr;`.

  The same shape, one degree weaker, appears in the JPEG write path:
  `html_buffer` is a `std::unique_ptr` declared before the `setjmp`, assigned
  inside the window, and read by the landing block
  (`html_buffer && html_buffer->client_aborted`). `SipiIOPng::write`'s
  `http_ctx` is the *benign* member of the family — `&http_ctx` is passed to
  `png_set_write_fn` before the `setjmp`, so its address escapes to an opaque C
  callback and the compiler must keep it addressable; still not a
  standard-sanctioned exemption, but materially safer than the other two.

  **RESOLVED, round 8** (`767d7a02`). The maintainer ruled; both were fixed as
  one standalone `fix(format_handlers):`. Two corrections to the analysis above,
  found while fixing: (1) `html_buffer` is not "one degree weaker" but **worse** —
  it is four `unique_ptr`s, not one, because `sink_stream`/`html_buffer`/
  `file_buffer`/`destmgr` are all *destroyed* on the return path out of the
  landing block and a destructor run is a read; (2) `volatile` is not expressible
  on a `unique_ptr` at all, so that half took ADR-0024's other sanctioned route,
  hoisting construction out of the risk window. `http_ctx` was assessed and left,
  as recommended.

  **Original analysis, left untouched, deliberately.** All three are main-owned, and the standing
  pattern in this plan (the `lua_hardening` flake, `redact_paths`) is that a
  main-owned bug surfaced by this work gets fixed at the root **on a maintainer
  ruling**, as its own `fix(format_handlers):` commit — not folded into a
  `refactor:`. ADR-0024's rule as written is "must not regress during the
  rewrite", which this work honors; it does not say "fix pre-existing
  violations". Recommended if ruled on: one `fix:` commit adding `volatile` to
  `icc_buffer_guard`, with `html_buffer` and `http_ctx` assessed in the same
  pass.


- **`//src/metadata` had an undeclared `logging` dep.** It compiled only
  because `//src:sipi_top` handed it `logging/logger.h` transitively (plus a
  blanket `includes = ["."]`). Deleting `sipi_top` surfaced it; the dep is now
  explicit. Expect more of these as each remaining root target dissolves — any
  target that leaned on `:engine`'s or `:sipi_lib`'s `includes = ["."]` will
  break the moment its supplier goes away. That is the carve working as
  intended, not a regression; fix at the consuming target, never by re-adding a
  blanket `includes`.
- **ADR-0007 line 64 will read stale once the carve is complete.** It describes
  `//src/error` as "the `//src:sipi_top` role (today's shared-error-base target)
  made an explicit, named package" — `sipi_top` no longer exists. Left alone
  deliberately: an ADR records the decision and the state it was taken against.
  Worth a second look during the repo-wide doc scrub, which is where the call
  belongs.

- **Highway's `HWY_TARGET_INCLUDE` takes a physical path, not a virtual one.**
  `resample.cc` re-includes *itself* through `hwy/foreach_target.h`, and that
  macro is resolved against the source tree, so it does not follow the
  package's `include_prefix`/`strip_include_prefix`. Any future move of a
  Highway-vectorized TU has to update the macro literal by hand; nothing
  mechanical will catch it, and it fails at compile time rather than silently.
- **`MutatorSurface.ExifWritableLazilyAllocatesWhenExifIsNull` asserts on a
  moved-from object, by necessity.** Every public `SipiImage` constructor calls
  `ensure_exif()` (default ctor `SipiImage.cpp:70`, geometry ctor `:170`), so the
  *only* way to reach a null-`exif` instance through the public API is the
  user-defined move constructor (`:115-125`), which moves `exif` out and leaves
  the source empty. The test therefore move-constructs and then exercises the
  source. It is well-defined — the move ctor's behavior is explicit in this
  class, not merely `shared_ptr`'s general guarantee — but it will trip
  clang-tidy's `bugprone-use-after-move` if that check is ever turned on (it is
  not a CI gate today; only the Rust rustfmt/clippy gates are). Flagged for the
  reviewer.

- **A Linux-only target means a green macOS run proves nothing about it.** The
  `//src:image` collision (CI findings §1) was invisible locally through two
  rounds because `oci_image` is `target_compatible_with`-gated to Linux and
  Bazel silently skips it on darwin. Every remaining package carve in this phase
  creates a new `src/<pkg>` output directory, so the same trap is live: before
  landing a carve, check that no target in package `src` is named `<pkg>`.
  Current `//src:*` target names worth watching against future package names:
  `sipi_lib`, `sipi_image`, `image_load`, `image_push_amd64`,
  `image_push_arm64`, `image_created`, `image_labels`, `sipi_bin_layer`,
  `sipi_debug_layout`. A local cross-build (`just bazel-cross-build-image amd64`)
  would catch it, but **cannot run on this box** — rules_rust toolchain
  resolution for `linux_x86_64` fails here, almost certainly the same missing-DNS
  fetch that blocks `bazel query` over `@google_benchmark`. So this class of
  defect is CI-only in practice; the cheap local guard is the name check above.

- **`include/VideoHD.icm` is an orphan.** No BUILD target, source file, or
  current doc references it — the only mentions are two in the archived
  2026-05-08 modularization analysis. Not deleted (out of scope for the chunk
  that found it); a candidate for the repo-wide scrub.
- **`docs/archive/2026-05-08-modularization-analysis.md` still cites
  `include/SipiConf.h`.** Left as-is: archived analyses are frozen records, the
  same treatment ADRs and `docs/specs/**` get. Flagging so the repo-wide
  path-scrub does not "fix" it.

- **`SipiIO` name hiding.** Every concrete handler overrides only the 6-arg
  `read`, which hides the base class's 2-arg convenience overloads from
  unqualified lookup on the derived type. The harness reaches the convenience
  overload via `handler.Sipi::SipiIO::read(&img, path)`. Worth a `using
  SipiIO::read;` in each handler at some point — noted, not acted on (out of
  scope; the handlers are untouched by the fuzz work).
- **`checked_buf_size` is unreachable on 64-bit.** `validate_decode_dims`
  (`SipiIOJ2k.cpp:520`) caps dimensions at `kMaxDecodeDim` (1<<17) and channels
  at 32 well before the buffer sizing at `SipiIOJ2k.cpp:700/717/741`, so the
  overflow-checked multiply downstream of it can no longer return `nullopt`.
  Both guards were left exactly as they are. Worth a reviewer's opinion against
  the repo's no-defense-in-depth rule, but not this work's call to make.
- **Two defects were caught in worker output during verification**, both in the
  nightly wiring: the `_bin` binaries are executed outside Bazel, so (a) the
  `rules_fuzzing` `dictionary` attribute would have been inert, and (b) the
  codec legs would have started from a completely empty corpus, silently
  discarding every hand-picked seed. Both are fixed; noting them because the
  same trap applies to any future harness added to this loop.

- **`redact_paths`'s own doc comment gets its example wrong, and the real
  behavior leaves a dangling quote in client-facing text** (round 5, found while
  writing the `SipiValueError` tests). `src/util/cpp/PathRedact.h` documents
  `Cannot read file "/srv/images/sub/foo.jp2": broken` →
  `Cannot read file "foo.jp2": broken`. It does not. The function splits on
  **whitespace**, so the token is `"/srv/images/sub/foo.jp2":` — opening quote
  included — and it keeps only what follows the last `/`. The actual output is
  `Cannot read file foo.jp2": broken`: the **opening quote is eaten with the
  directory prefix**, leaving an unbalanced `"` in every path-quoting message
  that reaches an HTTP body or a Lua string.

  Two separable things, both **main-owned and both left untouched**: (a) the doc
  comment is simply wrong and misleads anyone writing against it — the chunk-5C
  tests assert *verified* behavior with an explanatory comment rather than the
  doc's prose; (b) the unbalanced quote is a real cosmetic defect in
  client-facing error text. Not fixed here because neither is this plan's scope
  and the branch is not implicated — surfaced for a maintainer ruling, exactly as
  the `lua_hardening` flake was. If it is to be fixed, (a) is a one-line comment
  correction and (b) is a behavior change that needs its own `fix:` and touches
  `SipiImageError::message()` / `shttps::Error::message()` output.

- **A decode failure under memory pressure is reported as
  `Error reading file <path>`, with the true cause erased** (round 5). The first
  full `just bench decode` run of the round aborted at
  `decode_thumb/jpeg_baseline` with an uncaught
  `SipiImageError: Sipi image error at [src/image/cpp/SipiImage.cpp: 312]: Error
  reading file …/big_building/baseline.jpg`. It **did not reproduce**: the same
  case runs clean in isolation on both HEAD and the `865f2c25` oracle, a
  `--benchmark_filter='jpeg_baseline'` 20-rep run is clean, and a full re-run on
  a quiet machine completed all 18 cases. The one failing run happened with the
  machine under load (two Bazel servers resident, a process-tier bench having
  just finished), so the working hypothesis is a transient allocation failure —
  **not** fd exhaustion, `ulimit -n` here is 1048576. Nothing was changed in
  response, and the branch is not implicated.

  What *is* worth carrying forward is the **failure-reporting shape**, which is
  squarely this plan's subject. `SipiImage::read` (`SipiImage.cpp:295-312`)
  dispatches on the extension; when that handler returns `false` it then re-tries
  **every other handler** on the same file and, only after all four fail, throws
  a single generic `Error reading file <path>`. So a resource failure inside the
  correct handler (a) triggers three futile decode attempts on a file whose
  format was never in doubt, and (b) surfaces with the cause completely erased —
  the caller cannot tell "not a JPEG" from "ran out of memory decoding a JPEG".
  That is exactly the legibility loss `SipiValueError` exists to fix: once the
  handlers return `Result`, the fallback loop can distinguish "this handler does
  not recognise the format" (try the next one) from "this handler recognised it
  and failed" (stop, propagate the real error). **Phase 6** owns the dispatcher;
  flagging it here so the loop is redesigned rather than mechanically retyped.

## Round 15 — DEV-7079 PNG metadata round-trip (final content item)

| chunk | status | commit | summary |
|-------|--------|--------|---------|
| DEV-7079 PNG metadata round-trip | done | `17f69cef` | PNG EXIF→binary-safe `eXIf` chunk; IPTC→ImageMagick `Raw profile type iptc` hex-text; empty payload emits no chunk; redundant `png_write_info`/`png_write_end` (duplicated `eXIf`+`IEND`) removed; colocated `PngMetadataRoundTrip` test over `palette.tif`; 3 PNG approval goldens moved (IDAT byte-identical), CHANGELOG row added |

Blocker resolutions applied per brief: **DEV-7080** (JP2 hang) left out of scope — no hanging input pinned into any corpus; maintainer delivers a decode-watchdog as its own PR. **DEV-7079** fixed here with a real `eXIf` chunk.

Verification (this box): `just bazel-test` **74/74**; approval suite green; `just commit-lint` clean over the whole branch range. Brief's end-to-end command **succeeds**: `sipi convert cmyk.tif out.png -F png && sipi convert out.png rt.tif` both exit 0 (EXIF survives; cmyk's lone-`ApplicationRecordVersion` IPTC is dropped — see side finding, not a regression). No Rust touched, so the rustfmt/clippy gates were not exercised.

Execution took four worker iterations, all driven by orchestrator verification rather than worker failure: (1) EXIF `eXIf` + IPTC raw-profile + test; (2) removed the duplicate `eXIf`/`IEND` write once a chunk-level dump exposed it; (3) guarded empty EXIF/IPTC payloads once a zero-length `eXIf` was seen in the `PngRoundTrip` output; (4) switched the round-trip test fixture from `cmyk.tif` to `palette.tif` once `cmyk`'s IPTC proved non-encodable. The double-`IEND` was found to be **pre-existing** (already baked into every committed PNG golden) and is cleaned up as a side effect of the correct single-writer sequence.

### Side findings (round 15)

- **`Iptc::iptcBytes()` returns 0 bytes for a lone `ApplicationRecordVersion`
  IPTC record.** `cmyk.tif` and `cielab.tif` both carry only IPTC dataset 2:00
  (`ApplicationRecordVersion`); `Iptc::parse` on the TIFF `RICHTIFFIPTC` tag
  succeeds and `getIptc()` is non-null, but Exiv2's `IptcParser::encode()`
  re-encodes that data to an empty `DataBuf`. So their IPTC does not round-trip
  through *any* carrier (it never did — the old truncating text chunk lost it
  too). This is a pre-existing metadata-layer asymmetry, orthogonal to the PNG
  truncation bug. Fixtures whose IPTC *does* re-encode non-empty and round-trips
  cleanly through the new PNG carrier: `palette.tif`, `CIELab16.tif`,
  `gray_with_icc_another.jpg`, `HasCommentBlock.JPG` (verified two-hop
  PNG→PNG: chunk sizes stable). Follow-up candidate; not fixed here.
- **The empty-EXIF artifact was masking a spurious test pass.** Before the
  empty-payload guard, an image with no real EXIF still emitted a zero-length
  `eXIf` chunk, and an empty IPTC emitted an empty raw profile that
  `Iptc::parse` accepted as a non-null-but-empty object — so a
  `getIptc() != nullptr` assertion passed without any data surviving. The
  round-trip test now asserts non-empty byte-equality, which is what caught it.

## Round 16 — adversarial-review fixes (verified findings)

Review-fix round 1 (cap 3). Every item was pre-verified by the session against
source; the maintainer's scope ruling was: fix the 4 code bugs + the doc/topology
drift + the cheap warnings; DEFER the unreachable throw-through-`Result` gaps to a
follow-up (the session files that issue). All chunks landed. Base for this round:
`17f69cef`.

| chunk | status | commit | summary |
|-------|--------|--------|---------|
| jpeg-colorspace-leak | done | `517467bc` | `jpeg_destroy_decompress` added to the `JCS_UNKNOWN` + `default` colourspace error branches in `SipiIOJpeg.cpp::read()`; full-function audit found no other leaking early return |
| png-dim-guard-leak | done | `2b5bc794` | `png_destroy_read_struct` added to the `validate_decode_dims` failure branch (`SipiIOPng.cpp:340`); read()/read_shape() audited, no other skipping return |
| png-rawprofile-reserve | done | `8e7c7e48` | `decode_raw_profile`'s `result.reserve(length)` clamped to `std::min(length,(end-p)/2+1)` (+`<algorithm>`); regression test `PngErrorPath.RawProfileHugeDeclaredLengthDoesNotOverAllocate` crafts a libpng `Raw profile type iptc` chunk with declared length 999999999999 / 2 hex chars and drives it through `SipiIOPng::read` |
| j2k-stripe-misclassified | done | `332b9713` | the three `pull_stripe` catch blocks (8/12/16-bit) changed from `return false` to `return std::unexpected(kMalformedInput, "corrupt codestream (stripe decode failed)")`, matching the `start()` catch; unused `&exc` bindings dropped. No regression test (no existing malformed-JP2 fixture reaches the stripe path; DEV-7080 hanging input deliberately not pinned). Fuzz-harness catch-scope note left as-is: it already says a rejection is reported "through the `Result` holding no value", which this change makes strictly more accurate |
| docs-topology | done | `8455845a` | ARCH-MAP.md + ADR-0007: stale `friend`-coupling language replaced with the public-mutator-surface description; `image_processing` deps invariant corrected to include `metadata` (both docs); ADR disposition row split so deleted `SipiCommon` has no dead link and `SipiFilenameHash` links to its real `src/util/cpp/` home; `tools/format-handlers-fanout.sh` friend section removed + dispatch grep fixed to `dispatched_key = "..."` (exits 0, non-empty). Fan-out count recomputed ~5 → ~4 sites across 3 files |
| fuzz-registration | done | `6495f9ce` | four codec fuzz harnesses added to testing-strategy.md (Layer 4 table, coverage summary, "what belongs here") and `src/format_handlers/fuzz/**` added to codecov.yml ignore |
| conventions-error-row | done | `5c9a9ac4` | CONVENTIONS.md `error` module row extended with `SipiValueError`/`ErrorCode`/`Result<T>` (ADR-0024) |
| scale-docstrings | done | `ca7c3d29` | scaleFast/scaleMedium/scale `\returns` docstrings corrected: a `<=1` axis is now an error, not a silent no-op (matches the 1,1 hardening) |
| sentry-policy-routing | done | `0a740a55` | serve_image.cpp streamed-write path routes the Sentry skip through `policy_for(err.code()).sentry_policy != kSkip` instead of a hardcoded `kClientAbort` check; bespoke client-abort log/metric unchanged |
| fh-value-objects-dep | done | `15ab43ac` | `//src/iiifparser/cpp/value_objects:iiifparser` added as a direct dep of the `format_handlers` cc_library (declare-what-you-use; `SipiIOJ2k.h` takes `SipiRegion`/`SipiSize` by value) |

**Commit type notes:** the four leak/reserve/misclassification fixes are `fix`
(bugs on `main`). Doc corrections are `docs(docs)`; the fuzz-harness registration
is `test(format_handlers)` (coverage + strategy); the Sentry routing is
`refactor(observability)` (behaviour-preserving for today's codes, prevents future
drift); the BUILD dep is `build(format_handlers)`.

**Verification (this box):** `just bazel-test` **74/74**; `just bazel-test-approval`
green with **no golden movement** (`git status` shows zero approval/golden/.received
files — expected, since every change is an error path or docs); `just commit-lint`
clean over `63cca36b..HEAD`. No Rust files touched in round 16, so the
rustfmt/clippy gates were not exercised (correctly out of scope). Branch not pushed
— the session owns the force-push (lease pinned to the last-pushed SHA `17f69cef`).

### Deferral (round 16)

- **Unreachable throw-through-`Result` gaps** — the adversarial review flagged
  paths where a `throw` can still traverse a function that has otherwise migrated
  to `Result`. The maintainer ruled these OUT of this round; the session files a
  dedicated follow-up Linear issue. Not addressed here.

### Side findings (round 16)

- **No malformed-JP2 fixture exercises the `pull_stripe` catch path.** The J2K
  stripe-decode misclassification fix (`332b9713`) could not be pinned with a
  regression test because no committed fixture reaches it and the DEV-7080 hanging
  input must not be pinned. A fixture that fails mid-stripe (valid header, corrupt
  codestream body) without hanging would close this — follow-up candidate.
