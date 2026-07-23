# Changelog

## [6.0.0](https://github.com/dasch-swiss/sipi/compare/v6.0.0...v6.0.0) (2026-07-23)


### ⚠ BREAKING CHANGES

* **cli:** bare `sipi --convert ...` (and --query, --compare) now fails with a CLI11 usage error. Operators must use the verb-noun subcommand surface introduced in the previous commit:
* **lua:** The HTTP endpoints `GET /api/cache` and `DELETE /api/cache` are removed. External cache management requires either a future dedicated C++ route in route_handlers/ or scraping Prometheus metrics for inspection. The architectural principle established here (Lua = request-shaping; mutation = C++ route) will guide subsequent script-removal decisions in Probe 8 (e.g. exit.lua).
* SipiImgInfo gains nc/bps fields (struct size change).

### Features

* Add --json CLI flag for structured output ([0238b5a](https://github.com/dasch-swiss/sipi/commit/0238b5aaa5a62ec6dcd21937962796fa1721981f))
* Add --version flag to CLI ([0c5da66](https://github.com/dasch-swiss/sipi/commit/0c5da66130e7cecf3769aab57d789cc3371a8d7f))
* **cli-rs:** Parse the full `server` flag surface (unwired) ([29b43c2](https://github.com/dasch-swiss/sipi/commit/29b43c20aaac441461e5f9705a27923756d66b15))
* **cli/server:** Intentional Service File creation ([980aa8a](https://github.com/dasch-swiss/sipi/commit/980aa8af1bea057158414555234a9125f9018a1e))
* **cli:** Add `sipi health` subcommand for container healthchecks ([6dd5dc6](https://github.com/dasch-swiss/sipi/commit/6dd5dc671fd955a5519107d877e6b015d89eaa50))
* **cli:** Remove legacy --convert / --query / --compare flag forms ([7179451](https://github.com/dasch-swiss/sipi/commit/7179451baf8317baa29e1cc8a1bc2d23f6eace95))
* **ffi:** Apply CLI/env overrides onto SipiConf in sipi_init ([20f958b](https://github.com/dasch-swiss/sipi/commit/20f958baee213699218e10fed37f5a1326d1febb))
* **ffi:** Define the concrete SipiServerConfig override struct ([e749716](https://github.com/dasch-swiss/sipi/commit/e749716b4e6638a74dcc8d95bf91182c4ab6fced))
* **ffi:** Emit the IIIF profile Link header on image responses ([c7d23ca](https://github.com/dasch-swiss/sipi/commit/c7d23cafb97cd5589f3a4e163d130a7d4492a587))
* **format:** Expand EXIF metadata coverage and harden rational-array path ([2f6970f](https://github.com/dasch-swiss/sipi/commit/2f6970f35cec9016e5c957346b8e3c88bbfa4685))
* **formats:** Read_shape fast path + cache shrinkage ([04b9dc3](https://github.com/dasch-swiss/sipi/commit/04b9dc33b06878d90d8e528a275e766e4dad6435))
* **formats:** Service File Essentials carriers (JP2 + pyramidal TIFF) ([4017a03](https://github.com/dasch-swiss/sipi/commit/4017a03f8107b8a890dde677b6622ae68258faf2))
* Forward the full server CLI/env flag set into ServerOverrides ([2f5dbb1](https://github.com/dasch-swiss/sipi/commit/2f5dbb1bdfbfd1de2dcaf6c26b289376085ee555))
* **icc:** Normalize ICC profile creation date when SOURCE_DATE_EPOCH is set ([45f1caf](https://github.com/dasch-swiss/sipi/commit/45f1caf21d47cc29f45d0065695575f449a300e5))
* **lua:** Remove cache.* Lua bindings and /api/cache admin endpoint ([e412b25](https://github.com/dasch-swiss/sipi/commit/e412b254a8e8048411c6c8302d0aaa3a9ea396f0))
* Memory budget semaphore for concurrent image decode throttling ([afa682e](https://github.com/dasch-swiss/sipi/commit/afa682eff297ca6b7c41434f2eecc94b7be07f79))
* **metadata:** Protobuf Essentials wire format API ([e130ced](https://github.com/dasch-swiss/sipi/commit/e130ceda8a6c02e3ea67ed1af4a06c5a72262ca5))
* **metrics:** Add sipi_request_duration_seconds Prometheus histogram ([050f922](https://github.com/dasch-swiss/sipi/commit/050f922eb6260ec44a1a2c7b3a637821b5757925))
* **observability:** Read_shape fast-path + Essentials hash-mismatch metrics ([a8efe11](https://github.com/dasch-swiss/sipi/commit/a8efe119af20c668d3a00fee16bac427871e1778))
* OOM prevention, rate limiter, health endpoint, graceful shutdown ([45f3192](https://github.com/dasch-swiss/sipi/commit/45f31929b2894ed7c0c5c94b03e2f03d090770f8))
* Propagate W3C traceparent from Lua outbound calls to dsp-api ([0f7851e](https://github.com/dasch-swiss/sipi/commit/0f7851e194fa635b63933efa4d28ecdc514810b5))
* Serve the /server docroot fileserver in the Rust shell ([221c94a](https://github.com/dasch-swiss/sipi/commit/221c94a0a68532445767d28c9f2dc52d83407014))
* **server-rs,cli-rs:** Cut over crash reporting from sentry-native to Rust sentry + minidump ([a44ba04](https://github.com/dasch-swiss/sipi/commit/a44ba047e0efcc93599892a8b7ea41d0335ed931))
* **server-rs,cli-rs:** Queue requests at the engine pool instead of shedding ([6cc6f2b](https://github.com/dasch-swiss/sipi/commit/6cc6f2b9c6998d9935b5d1d2a4a9597a21c981ba))
* **server-rs:** Add a sipi_port FFI getter as the listener fallback ([06f4cb8](https://github.com/dasch-swiss/sipi/commit/06f4cb8daf31fd6cca15de28b78ada7a435c1175))
* **server-rs:** Bridge engine + pool metrics to OTLP observable instruments ([501e439](https://github.com/dasch-swiss/sipi/commit/501e439c5132f6f3647678c7328b2b945d378eec))
* **server-rs:** Forward overrides through sipi_init via OverridesHolder ([2a68fee](https://github.com/dasch-swiss/sipi/commit/2a68fee2e2e00a444510242c3407fc01017d8351))
* **server-rs:** Mirror SipiServerConfig as repr(C) + layout test ([a2126d5](https://github.com/dasch-swiss/sipi/commit/a2126d5d47ed43635a58cd68d84966a0ac3fcc72))
* **shttps:** Auto-detect thread count from CPU cores with container awareness ([85f05c1](https://github.com/dasch-swiss/sipi/commit/85f05c123131bfc38a5c28f24c06b9e37e1f9e95))
* Support TOML config files for the server (--config *.toml) ([b36d374](https://github.com/dasch-swiss/sipi/commit/b36d374d741d2f02dd98ba90070299f6f5cfb20b))


### Bug Fixes

* **build:** Kakadu libtool conflict with Nix and zig-clean scope ([eaffd72](https://github.com/dasch-swiss/sipi/commit/eaffd72fdd1fdf0b265be3354288176980b0b39c))
* **ci:** Drop the remote downloader, cache external deps via repository_cache ([95274dc](https://github.com/dasch-swiss/sipi/commit/95274dce84e5d1b011d614e6da01072df6535d39))
* **ci:** Repair lint job's bazel-rbe inputs so lint actually uses RBE ([dc3035b](https://github.com/dasch-swiss/sipi/commit/dc3035b9867d1050be1d78accfc509a7004f859e))
* **cli-rs:** Exit 0 on `server --help` / `--version` ([ad25ddd](https://github.com/dasch-swiss/sipi/commit/ad25ddddbdd1b3c293a1a890eaa6739c666ccd98))
* **cli-rs:** Replace ureq with a raw TCP probe in the health verb ([325ae95](https://github.com/dasch-swiss/sipi/commit/325ae95d2bebd0b7be4541a018d8c93e8a357196))
* **cli:** Guard the empty --maxpost length underflow ([4db9285](https://github.com/dasch-swiss/sipi/commit/4db928545382e720fbd72d271cad68a44391eb1e))
* **cli:** Reject negative cache-nfiles + range-check ports (both binaries) ([a39298d](https://github.com/dasch-swiss/sipi/commit/a39298d0e8e2c29bd86a1e260cdb44626e858099))
* **cli:** Shut down sentry transport before curl cleanup on all exit paths ([cfcb38b](https://github.com/dasch-swiss/sipi/commit/cfcb38b1e976867c36a7a8a92969e7189b57957c))
* **cli:** Sipi compare per-channel delta uses absolute difference ([f7e397a](https://github.com/dasch-swiss/sipi/commit/f7e397a553531a9f3817f7fcf0b7be36e34ffc69))
* **cli:** Stringify --quality before storing it in compression params ([5cc59e9](https://github.com/dasch-swiss/sipi/commit/5cc59e95a1f79a6875b9e6b102a6e5d83901ca67))
* Create GitHub Release if missing in publish-static-release ([3a58fa8](https://github.com/dasch-swiss/sipi/commit/3a58fa83a91df41da3d18928e65a3dcd044ae20f))
* **e2e:** Stop racing subject and reference on a shared cache dir ([563fbd2](https://github.com/dasch-swiss/sipi/commit/563fbd2de56dd1c6ad980676b3c1220a57096c81))
* Eliminate C++ exception-through-C UB in image format handlers ([94f45a2](https://github.com/dasch-swiss/sipi/commit/94f45a28c29618ce9477e7cf2ca16de4d9340890))
* **exif:** Plug leak when EXIF parser throws on malformed bytes ([79ddfe3](https://github.com/dasch-swiss/sipi/commit/79ddfe33443e9295182b88b898da937bbef8cf11))
* **ffi:** Clamp negative max-decode-memory/maxpost overrides to 0 ([ce25eb8](https://github.com/dasch-swiss/sipi/commit/ce25eb8c0308c7e4a33a10c2b39dcf473273d446))
* **ffi:** Return 404 when a Lua-route script vanishes after the access check ([74dbf48](https://github.com/dasch-swiss/sipi/commit/74dbf48d32262637acdd9fcc6087a1445cd27441))
* **formats:** Convert corrupt-JP2 Kakadu errors to SipiImageError ([902519d](https://github.com/dasch-swiss/sipi/commit/902519da63b7b62054462acf9cb80bd9a888d0d2))
* **formats:** Make SipiImage::write reject unknown format keys ([61c9eb5](https://github.com/dasch-swiss/sipi/commit/61c9eb542cbf3f839f27d8f839abdb3137fd930a))
* **formats:** Single-thread Kakadu JP2 encode under ASan ([a8b1c38](https://github.com/dasch-swiss/sipi/commit/a8b1c386bea3fae74feaa20833d6f94a218ad522))
* Handle --version before library init to avoid LSan exit-time leaks ([269e8fe](https://github.com/dasch-swiss/sipi/commit/269e8fe86d6d7f694dacd3ba96fa0ab97d71d197))
* HEAD response and LFS case sensitivity for e2e tests ([f3741ea](https://github.com/dasch-swiss/sipi/commit/f3741eae559c7334cf04a65a437c96fb4d89963a))
* **http:** Distinguish client-aborted HTTP writes from real write errors ([53c9d72](https://github.com/dasch-swiss/sipi/commit/53c9d72d9bd58ef8a86e47590e92ef1a3d9eee13))
* **http:** Treat any OUTPUT_WRITE_FAIL as client abort ([311578f](https://github.com/dasch-swiss/sipi/commit/311578f780b5bfde39b4cd11f3219395ea78ea0a))
* **icc:** Bail on gmtime_r failure / out-of-range year ([408a018](https://github.com/dasch-swiss/sipi/commit/408a0181d1b2d7b2bb2bcb6ececcdbe34ca2b999))
* **iiif:** Replace std::regex with hand-rolled validators ([526890e](https://github.com/dasch-swiss/sipi/commit/526890e562ca2a7f49697aef330b243b48126f3d))
* **image:** GetPixel/setPixel use row-major pixel indexing ([8ef46f2](https://github.com/dasch-swiss/sipi/commit/8ef46f2aeccbe615a78a9bd02362b3288373d731))
* Input validation security hardening (R1-R10) ([76b0790](https://github.com/dasch-swiss/sipi/commit/76b07909d426769c0097c54e9d6d69202a353b5e))
* JPEG getDim memory leaks on error paths ([e963c89](https://github.com/dasch-swiss/sipi/commit/e963c89b1798463daeffbd4bb561a2ce8601895e))
* **jpeg:** Deterministic cross-architecture JPEG (integer decode + baseline encode) ([5ba1a05](https://github.com/dasch-swiss/sipi/commit/5ba1a0596972bbd08eefa8cfb51b846e6d21520e))
* **just:** Bench-compare drops empty FLAGS arg in no-flags invocation ([fe99532](https://github.com/dasch-swiss/sipi/commit/fe995327e9561d3ef43ca456136a75075ee9ec91))
* Memory safety for SipiImage and SipiFilenameHash (R11-R19) ([6f0da6c](https://github.com/dasch-swiss/sipi/commit/6f0da6ccb6186185f2a6554ec4ebc7543d7474db))
* PNG getDim returns 0x0 dimensions due to missing png_read_info ([955230e](https://github.com/dasch-swiss/sipi/commit/955230e7e3ab6036672ecbcd66a1f33aec031093))
* Queue liveness check, timeout validation, and unlimited queue default ([f4bba6a](https://github.com/dasch-swiss/sipi/commit/f4bba6a2f544a5366d96d3f1319c3b51ead2a7f4))
* Resilient JPEG metadata + YCCK + CMYK APP14 + XMP scanner (DEV-6250, DEV-6257, DEV-6259) ([2360df8](https://github.com/dasch-swiss/sipi/commit/2360df864a78769a9fd9c7c5e2f6bf2e9e524b3f))
* Resolve all memory leaks detected by LeakSanitizer ([c092928](https://github.com/dasch-swiss/sipi/commit/c092928328fc91026b5e5d191169de5d81a9a17e))
* Resolve ASan/UBSan sanitizer findings (DEV-6038, DEV-6039, DEV-6040) ([90e076c](https://github.com/dasch-swiss/sipi/commit/90e076cdf8479fed16d9d867546d4470c89ed69e))
* Resolve heap-buffer-overflow, stack-use-after-scope, and UBSan findings ([a915198](https://github.com/dasch-swiss/sipi/commit/a9151984c2f3862692bab80987a1028b5516631e))
* **sanitizer:** Symbolize stack frames so Lua leak suppressions match ([fc96101](https://github.com/dasch-swiss/sipi/commit/fc961012b79f37a9be6a669aa629715a21c4149e))
* **server-rs:** Address adversarial-review findings on the port precedence work ([b200216](https://github.com/dasch-swiss/sipi/commit/b2002167963e3ff43d6cfaa6c6d4f73fee19843a))
* **server-rs:** Exit via _exit() under ASan to skip teardown-time join abort ([e484f6a](https://github.com/dasch-swiss/sipi/commit/e484f6a3c6ca9291f53b643249689da1a60243b0))
* **server-rs:** Keep tokio's blocking pool alive under ASan ([137265c](https://github.com/dasch-swiss/sipi/commit/137265c27091423dbb3a7b279fe3f9744ffc03da))
* **shttps:** Fix connection drops under concurrent load (DEV-6024) ([7a4459c](https://github.com/dasch-swiss/sipi/commit/7a4459ca28d8145f4344176ae2586e5031258829))
* **shttps:** Reverse poll loop iteration to prevent index-shifting bug (DEV-6024) ([5cf873f](https://github.com/dasch-swiss/sipi/commit/5cf873f04245ef5c5091a2e36985a1177978b78b))
* **shttps:** Urldecode no longer infinite-loops on trailing % ([1659539](https://github.com/dasch-swiss/sipi/commit/16595399a1c09e91290f5e4d3d854cd267ba84cf))
* Support 1-bit bilevel TIFF (DEV-6249) ([c45145c](https://github.com/dasch-swiss/sipi/commit/c45145cf4b373ad84852e1a47adea459ad9cf5f1))


### Reverts

* Remove mkdocs from nix devShells ([99e72be](https://github.com/dasch-swiss/sipi/commit/99e72be2bce7c9a19b92a68e19675677002b604c))


### Miscellaneous Chores

* Release 6.0.0 ([51b7cb7](https://github.com/dasch-swiss/sipi/commit/51b7cb74a74f9f6e854ea0f7e9415ccca4c30301))

## [6.0.0](https://github.com/dasch-swiss/sipi/compare/v5.0.1...v6.0.0) (2026-07-22)


### Features

* **cli-rs:** Parse the full `server` flag surface (unwired) ([29b43c2](https://github.com/dasch-swiss/sipi/commit/29b43c20aaac441461e5f9705a27923756d66b15))
* **ffi:** Apply CLI/env overrides onto SipiConf in sipi_init ([20f958b](https://github.com/dasch-swiss/sipi/commit/20f958baee213699218e10fed37f5a1326d1febb))
* **ffi:** Define the concrete SipiServerConfig override struct ([e749716](https://github.com/dasch-swiss/sipi/commit/e749716b4e6638a74dcc8d95bf91182c4ab6fced))
* **ffi:** Emit the IIIF profile Link header on image responses ([c7d23ca](https://github.com/dasch-swiss/sipi/commit/c7d23cafb97cd5589f3a4e163d130a7d4492a587))
* Forward the full server CLI/env flag set into ServerOverrides ([2f5dbb1](https://github.com/dasch-swiss/sipi/commit/2f5dbb1bdfbfd1de2dcaf6c26b289376085ee555))
* Propagate W3C traceparent from Lua outbound calls to dsp-api ([0f7851e](https://github.com/dasch-swiss/sipi/commit/0f7851e194fa635b63933efa4d28ecdc514810b5))
* Serve the /server docroot fileserver in the Rust shell ([221c94a](https://github.com/dasch-swiss/sipi/commit/221c94a0a68532445767d28c9f2dc52d83407014))
* **server-rs,cli-rs:** Cut over crash reporting from sentry-native to Rust sentry + minidump ([a44ba04](https://github.com/dasch-swiss/sipi/commit/a44ba047e0efcc93599892a8b7ea41d0335ed931))
* **server-rs:** Add a sipi_port FFI getter as the listener fallback ([06f4cb8](https://github.com/dasch-swiss/sipi/commit/06f4cb8daf31fd6cca15de28b78ada7a435c1175))
* **server-rs:** Bridge engine + pool metrics to OTLP observable instruments ([501e439](https://github.com/dasch-swiss/sipi/commit/501e439c5132f6f3647678c7328b2b945d378eec))
* **server-rs:** Forward overrides through sipi_init via OverridesHolder ([2a68fee](https://github.com/dasch-swiss/sipi/commit/2a68fee2e2e00a444510242c3407fc01017d8351))
* **server-rs:** Mirror SipiServerConfig as repr(C) + layout test ([a2126d5](https://github.com/dasch-swiss/sipi/commit/a2126d5d47ed43635a58cd68d84966a0ac3fcc72))
* Support TOML config files for the server (--config *.toml) ([b36d374](https://github.com/dasch-swiss/sipi/commit/b36d374d741d2f02dd98ba90070299f6f5cfb20b))


### Bug Fixes

* **ci:** Drop the remote downloader, cache external deps via repository_cache ([95274dc](https://github.com/dasch-swiss/sipi/commit/95274dce84e5d1b011d614e6da01072df6535d39))
* **ci:** Repair lint job's bazel-rbe inputs so lint actually uses RBE ([dc3035b](https://github.com/dasch-swiss/sipi/commit/dc3035b9867d1050be1d78accfc509a7004f859e))
* **cli-rs:** Exit 0 on `server --help` / `--version` ([ad25ddd](https://github.com/dasch-swiss/sipi/commit/ad25ddddbdd1b3c293a1a890eaa6739c666ccd98))
* **cli-rs:** Replace ureq with a raw TCP probe in the health verb ([325ae95](https://github.com/dasch-swiss/sipi/commit/325ae95d2bebd0b7be4541a018d8c93e8a357196))
* **cli:** Guard the empty --maxpost length underflow ([4db9285](https://github.com/dasch-swiss/sipi/commit/4db928545382e720fbd72d271cad68a44391eb1e))
* **cli:** Reject negative cache-nfiles + range-check ports (both binaries) ([a39298d](https://github.com/dasch-swiss/sipi/commit/a39298d0e8e2c29bd86a1e260cdb44626e858099))
* **cli:** Sipi compare per-channel delta uses absolute difference ([f7e397a](https://github.com/dasch-swiss/sipi/commit/f7e397a553531a9f3817f7fcf0b7be36e34ffc69))
* **cli:** Stringify --quality before storing it in compression params ([5cc59e9](https://github.com/dasch-swiss/sipi/commit/5cc59e95a1f79a6875b9e6b102a6e5d83901ca67))
* **e2e:** Stop racing subject and reference on a shared cache dir ([563fbd2](https://github.com/dasch-swiss/sipi/commit/563fbd2de56dd1c6ad980676b3c1220a57096c81))
* **ffi:** Clamp negative max-decode-memory/maxpost overrides to 0 ([ce25eb8](https://github.com/dasch-swiss/sipi/commit/ce25eb8c0308c7e4a33a10c2b39dcf473273d446))
* **ffi:** Return 404 when a Lua-route script vanishes after the access check ([74dbf48](https://github.com/dasch-swiss/sipi/commit/74dbf48d32262637acdd9fcc6087a1445cd27441))
* **formats:** Convert corrupt-JP2 Kakadu errors to SipiImageError ([902519d](https://github.com/dasch-swiss/sipi/commit/902519da63b7b62054462acf9cb80bd9a888d0d2))
* **formats:** Make SipiImage::write reject unknown format keys ([61c9eb5](https://github.com/dasch-swiss/sipi/commit/61c9eb542cbf3f839f27d8f839abdb3137fd930a))
* **formats:** Single-thread Kakadu JP2 encode under ASan ([a8b1c38](https://github.com/dasch-swiss/sipi/commit/a8b1c386bea3fae74feaa20833d6f94a218ad522))
* **image:** GetPixel/setPixel use row-major pixel indexing ([8ef46f2](https://github.com/dasch-swiss/sipi/commit/8ef46f2aeccbe615a78a9bd02362b3288373d731))
* **jpeg:** Deterministic cross-architecture JPEG (integer decode + baseline encode) ([5ba1a05](https://github.com/dasch-swiss/sipi/commit/5ba1a0596972bbd08eefa8cfb51b846e6d21520e))
* **just:** Bench-compare drops empty FLAGS arg in no-flags invocation ([fe99532](https://github.com/dasch-swiss/sipi/commit/fe995327e9561d3ef43ca456136a75075ee9ec91))
* **server-rs:** Address adversarial-review findings on the port precedence work ([b200216](https://github.com/dasch-swiss/sipi/commit/b2002167963e3ff43d6cfaa6c6d4f73fee19843a))
* **server-rs:** Exit via _exit() under ASan to skip teardown-time join abort ([e484f6a](https://github.com/dasch-swiss/sipi/commit/e484f6a3c6ca9291f53b643249689da1a60243b0))
* **server-rs:** Keep tokio's blocking pool alive under ASan ([137265c](https://github.com/dasch-swiss/sipi/commit/137265c27091423dbb3a7b279fe3f9744ffc03da))


### Miscellaneous Chores

* Release 6.0.0 ([51b7cb7](https://github.com/dasch-swiss/sipi/commit/51b7cb74a74f9f6e854ea0f7e9415ccca4c30301))

## [5.0.1](https://github.com/dasch-swiss/sipi/compare/v5.0.0...v5.0.1) (2026-06-03)


### Bug Fixes

* **cli:** Shut down sentry transport before curl cleanup on all exit paths ([cfcb38b](https://github.com/dasch-swiss/sipi/commit/cfcb38b1e976867c36a7a8a92969e7189b57957c))

## [5.0.0](https://github.com/dasch-swiss/sipi/compare/v4.1.1...v5.0.0) (2026-05-28)


### ⚠ BREAKING CHANGES

* **cli:** bare `sipi --convert ...` (and --query, --compare) now fails with a CLI11 usage error. Operators must use the verb-noun subcommand surface introduced in the previous commit:
* **lua:** The HTTP endpoints `GET /api/cache` and `DELETE /api/cache` are removed. External cache management requires either a future dedicated C++ route in route_handlers/ or scraping Prometheus metrics for inspection. The architectural principle established here (Lua = request-shaping; mutation = C++ route) will guide subsequent script-removal decisions in Probe 8 (e.g. exit.lua).

### Features

* Add --version flag to CLI ([0c5da66](https://github.com/dasch-swiss/sipi/commit/0c5da66130e7cecf3769aab57d789cc3371a8d7f))
* **cli/server:** Intentional Service File creation ([980aa8a](https://github.com/dasch-swiss/sipi/commit/980aa8af1bea057158414555234a9125f9018a1e))
* **cli:** Add `sipi health` subcommand for container healthchecks ([6dd5dc6](https://github.com/dasch-swiss/sipi/commit/6dd5dc671fd955a5519107d877e6b015d89eaa50))
* **cli:** Remove legacy --convert / --query / --compare flag forms ([7179451](https://github.com/dasch-swiss/sipi/commit/7179451baf8317baa29e1cc8a1bc2d23f6eace95))
* **format:** Expand EXIF metadata coverage and harden rational-array path ([2f6970f](https://github.com/dasch-swiss/sipi/commit/2f6970f35cec9016e5c957346b8e3c88bbfa4685))
* **formats:** Read_shape fast path + cache shrinkage ([04b9dc3](https://github.com/dasch-swiss/sipi/commit/04b9dc33b06878d90d8e528a275e766e4dad6435))
* **formats:** Service File Essentials carriers (JP2 + pyramidal TIFF) ([4017a03](https://github.com/dasch-swiss/sipi/commit/4017a03f8107b8a890dde677b6622ae68258faf2))
* **icc:** Normalize ICC profile creation date when SOURCE_DATE_EPOCH is set ([45f1caf](https://github.com/dasch-swiss/sipi/commit/45f1caf21d47cc29f45d0065695575f449a300e5))
* **lua:** Remove cache.* Lua bindings and /api/cache admin endpoint ([e412b25](https://github.com/dasch-swiss/sipi/commit/e412b254a8e8048411c6c8302d0aaa3a9ea396f0))
* **metadata:** Protobuf Essentials wire format API ([e130ced](https://github.com/dasch-swiss/sipi/commit/e130ceda8a6c02e3ea67ed1af4a06c5a72262ca5))
* **observability:** Read_shape fast-path + Essentials hash-mismatch metrics ([a8efe11](https://github.com/dasch-swiss/sipi/commit/a8efe119af20c668d3a00fee16bac427871e1778))


### Bug Fixes

* **exif:** Plug leak when EXIF parser throws on malformed bytes ([79ddfe3](https://github.com/dasch-swiss/sipi/commit/79ddfe33443e9295182b88b898da937bbef8cf11))
* Handle --version before library init to avoid LSan exit-time leaks ([269e8fe](https://github.com/dasch-swiss/sipi/commit/269e8fe86d6d7f694dacd3ba96fa0ab97d71d197))
* **http:** Treat any OUTPUT_WRITE_FAIL as client abort ([311578f](https://github.com/dasch-swiss/sipi/commit/311578f780b5bfde39b4cd11f3219395ea78ea0a))
* **icc:** Bail on gmtime_r failure / out-of-range year ([408a018](https://github.com/dasch-swiss/sipi/commit/408a0181d1b2d7b2bb2bcb6ececcdbe34ca2b999))
* **sanitizer:** Symbolize stack frames so Lua leak suppressions match ([fc96101](https://github.com/dasch-swiss/sipi/commit/fc961012b79f37a9be6a669aa629715a21c4149e))


### Reverts

* Remove mkdocs from nix devShells ([99e72be](https://github.com/dasch-swiss/sipi/commit/99e72be2bce7c9a19b92a68e19675677002b604c))

## [4.1.1](https://github.com/dasch-swiss/sipi/compare/v4.1.0...v4.1.1) (2026-04-27)


### Bug Fixes

* Create GitHub Release if missing in publish-static-release ([3a58fa8](https://github.com/dasch-swiss/sipi/commit/3a58fa83a91df41da3d18928e65a3dcd044ae20f))
* **http:** Distinguish client-aborted HTTP writes from real write errors ([53c9d72](https://github.com/dasch-swiss/sipi/commit/53c9d72d9bd58ef8a86e47590e92ef1a3d9eee13))
* **iiif:** Replace std::regex with hand-rolled validators ([526890e](https://github.com/dasch-swiss/sipi/commit/526890e562ca2a7f49697aef330b243b48126f3d))
* **shttps:** Urldecode no longer infinite-loops on trailing % ([1659539](https://github.com/dasch-swiss/sipi/commit/16595399a1c09e91290f5e4d3d854cd267ba84cf))

## [4.1.0](https://github.com/dasch-swiss/sipi/compare/v4.0.1...v4.1.0) (2026-04-16)


### Features

* Add --json CLI flag for structured output ([0238b5a](https://github.com/dasch-swiss/sipi/commit/0238b5aaa5a62ec6dcd21937962796fa1721981f))


### Bug Fixes

* Resilient JPEG metadata + YCCK + CMYK APP14 + XMP scanner (DEV-6250, DEV-6257, DEV-6259) ([2360df8](https://github.com/dasch-swiss/sipi/commit/2360df864a78769a9fd9c7c5e2f6bf2e9e524b3f))
* Support 1-bit bilevel TIFF (DEV-6249) ([c45145c](https://github.com/dasch-swiss/sipi/commit/c45145cf4b373ad84852e1a47adea459ad9cf5f1))

## [4.0.1](https://github.com/dasch-swiss/sipi/compare/v4.0.0...v4.0.1) (2026-04-06)


### Bug Fixes

* Eliminate C++ exception-through-C UB in image format handlers ([94f45a2](https://github.com/dasch-swiss/sipi/commit/94f45a28c29618ce9477e7cf2ca16de4d9340890))

## [4.0.0](https://github.com/dasch-swiss/sipi/compare/v3.18.0...v4.0.0) (2026-03-22)


### ⚠ BREAKING CHANGES

* SipiImgInfo gains nc/bps fields (struct size change).

### Features

* Memory budget semaphore for concurrent image decode throttling ([afa682e](https://github.com/dasch-swiss/sipi/commit/afa682eff297ca6b7c41434f2eecc94b7be07f79))
* **metrics:** Add sipi_request_duration_seconds Prometheus histogram ([050f922](https://github.com/dasch-swiss/sipi/commit/050f922eb6260ec44a1a2c7b3a637821b5757925))
* OOM prevention, rate limiter, health endpoint, graceful shutdown ([45f3192](https://github.com/dasch-swiss/sipi/commit/45f31929b2894ed7c0c5c94b03e2f03d090770f8))
* **shttps:** Auto-detect thread count from CPU cores with container awareness ([85f05c1](https://github.com/dasch-swiss/sipi/commit/85f05c123131bfc38a5c28f24c06b9e37e1f9e95))


### Bug Fixes

* Input validation security hardening (R1-R10) ([76b0790](https://github.com/dasch-swiss/sipi/commit/76b07909d426769c0097c54e9d6d69202a353b5e))
* JPEG getDim memory leaks on error paths ([e963c89](https://github.com/dasch-swiss/sipi/commit/e963c89b1798463daeffbd4bb561a2ce8601895e))
* Memory safety for SipiImage and SipiFilenameHash (R11-R19) ([6f0da6c](https://github.com/dasch-swiss/sipi/commit/6f0da6ccb6186185f2a6554ec4ebc7543d7474db))
* PNG getDim returns 0x0 dimensions due to missing png_read_info ([955230e](https://github.com/dasch-swiss/sipi/commit/955230e7e3ab6036672ecbcd66a1f33aec031093))
* Queue liveness check, timeout validation, and unlimited queue default ([f4bba6a](https://github.com/dasch-swiss/sipi/commit/f4bba6a2f544a5366d96d3f1319c3b51ead2a7f4))
* Resolve all memory leaks detected by LeakSanitizer ([c092928](https://github.com/dasch-swiss/sipi/commit/c092928328fc91026b5e5d191169de5d81a9a17e))
* Resolve ASan/UBSan sanitizer findings (DEV-6038, DEV-6039, DEV-6040) ([90e076c](https://github.com/dasch-swiss/sipi/commit/90e076cdf8479fed16d9d867546d4470c89ed69e))
* Resolve heap-buffer-overflow, stack-use-after-scope, and UBSan findings ([a915198](https://github.com/dasch-swiss/sipi/commit/a9151984c2f3862692bab80987a1028b5516631e))
* **shttps:** Fix connection drops under concurrent load (DEV-6024) ([7a4459c](https://github.com/dasch-swiss/sipi/commit/7a4459ca28d8145f4344176ae2586e5031258829))
* **shttps:** Reverse poll loop iteration to prevent index-shifting bug (DEV-6024) ([5cf873f](https://github.com/dasch-swiss/sipi/commit/5cf873f04245ef5c5091a2e36985a1177978b78b))

## [3.18.0](https://github.com/dasch-swiss/sipi/compare/v3.17.2...v3.18.0) (2026-03-08)


### Features

* Add Zig toolchain for static binary distribution ([634ea81](https://github.com/dasch-swiss/sipi/commit/634ea81dfb04190039c433f0f1d37418fb234207))
* Eliminate docker-sipi-base, use ubuntu:24.04 ([684e9e3](https://github.com/dasch-swiss/sipi/commit/684e9e32bfac3d94b3e3be4f71af109d64795016))
* Embed libmagic database into binary for portable runtime ([0550157](https://github.com/dasch-swiss/sipi/commit/0550157cfcb2b0bd8d9b5f47bfccd960384c4c36))
* **metrics:** Add Prometheus cache metrics and /metrics endpoint ([4be73a3](https://github.com/dasch-swiss/sipi/commit/4be73a35a855d9abda89d195864645d72ca4d3e8))


### Bug Fixes

* Address review findings in magic handling and cross-compilation ([2510641](https://github.com/dasch-swiss/sipi/commit/2510641c4546d529be42f56f5ab591d31f8429fc))
* **build:** Kakadu libtool conflict with Nix and zig-clean scope ([eaffd72](https://github.com/dasch-swiss/sipi/commit/eaffd72fdd1fdf0b265be3354288176980b0b39c))
* **cache:** Rewrite cache management with LRU eviction, crash recovery, and config ([92429e1](https://github.com/dasch-swiss/sipi/commit/92429e1031735db3437b18b3ea1524c69696e0ed))
* HEAD response and LFS case sensitivity for e2e tests ([f3741ea](https://github.com/dasch-swiss/sipi/commit/f3741eae559c7334cf04a65a437c96fb4d89963a))

## [3.17.2](https://github.com/dasch-swiss/sipi/compare/v3.17.1...v3.17.2) (2026-03-03)


### Bug Fixes

* Correct degenerate-buffer fallback in bilinn from buf[n*c] to buf[c] ([5f996fe](https://github.com/dasch-swiss/sipi/commit/5f996fe9917060e57dbbc199b533bf04a4bca9c4))
* Prevent segfault in bilinn() bilinear interpolation during image scaling ([ba84f8b](https://github.com/dasch-swiss/sipi/commit/ba84f8b2eba1f7b560289cf2b503d96b18f4956c))

## [3.17.1](https://github.com/dasch-swiss/sipi/compare/v3.17.0...v3.17.1) (2026-02-27)


### Bug Fixes

* **ci:** Add retry logic for transient download and cache failures ([f8d7e03](https://github.com/dasch-swiss/sipi/commit/f8d7e03e6d1600ccbd54ee811f0b2a298738b2d5))
* **ci:** Expose GHA cache variables to Make for Docker layer caching ([bba3cc3](https://github.com/dasch-swiss/sipi/commit/bba3cc3211c59004c5b4b8b3f365c827431fc434))
* **ci:** Filter Docker Scout SARIF to critical/high severities ([3b27580](https://github.com/dasch-swiss/sipi/commit/3b27580dce3841717c6f369e207960ea5836e19d))
* **ci:** Remove invalid --release flag from sentry-cli debug-files upload ([bdc8c78](https://github.com/dasch-swiss/sipi/commit/bdc8c78aa46c73e51cd0764a7f4986e578edb09c))
* **test:** Replace fixed sleep with readiness polling in smoke tests ([ce4af8c](https://github.com/dasch-swiss/sipi/commit/ce4af8c317c15c16b81726ad4e2ea3b6500ac1f2))

## [3.17.0](https://github.com/dasch-swiss/sipi/compare/v3.16.3...v3.17.0) (2026-02-25)


### Features

* Add SARIF code scanning and SBOM generation to Scout integration ([6e89131](https://github.com/dasch-swiss/sipi/commit/6e89131a1c81dee6d6e91a27ae53cb5f452cd14c))
* Add server-side Sentry error capture and debug symbol upload ([9a7454a](https://github.com/dasch-swiss/sipi/commit/9a7454a1647f927773a7bb96a374b381cdffb09e))
* Improve error messages, CLI exit codes, and Sentry integration for image processing failures ([a49324b](https://github.com/dasch-swiss/sipi/commit/a49324bc8e991acdeefa4f2edabd020e220308bc))
* Integrate Docker Scout CVE scanning and rename aarch64 to arm64 ([9949313](https://github.com/dasch-swiss/sipi/commit/9949313686999c8932210cc4ed51527fdb4cc22c))
* Switch sentry-native to inproc backend for proper crash handling ([c62900d](https://github.com/dasch-swiss/sipi/commit/c62900d465520638c9d01c5a77c9d36a309f1ce4))


### Bug Fixes

* Address review feedback on Sentry integration ([0fe9b81](https://github.com/dasch-swiss/sipi/commit/0fe9b816c6dea6af57f667c350d5b65a7258f90e))
* Avoid full Docker rebuild for debug symbol extraction ([d86c67f](https://github.com/dasch-swiss/sipi/commit/d86c67fd447b7a44d5ed9fc25762545d35277f2a))
* Update zlib to 1.3.2 and fix setuptools on Ubuntu 24.04 ([e161b55](https://github.com/dasch-swiss/sipi/commit/e161b558c72ff65ff8cd722816f5d8cb21498a72))
* Upgrade setuptools in workflows before install-requirements ([f86180f](https://github.com/dasch-swiss/sipi/commit/f86180fba5c1eade5f1e230b6d2e05b22591a8c6))

## [3.16.3](https://github.com/dasch-swiss/sipi/compare/v3.16.2...v3.16.3) (2025-08-25)


### Bug Fixes

* Trigger release creation ([025b5f7](https://github.com/dasch-swiss/sipi/commit/025b5f71aa1a3fe3f874ff88387bfe67604a910a))

## [3.16.2](https://github.com/dasch-swiss/sipi/compare/v3.16.1...v3.16.2) (2025-07-25)


### Bug Fixes

* Streaming of large video files ([#486](https://github.com/dasch-swiss/sipi/issues/486)) ([7efc43e](https://github.com/dasch-swiss/sipi/commit/7efc43e547d2aee3b82a35708124b46665dbcbb7))

## [3.16.1](https://github.com/dasch-swiss/sipi/compare/v3.16.0...v3.16.1) (2025-07-23)


### Bug Fixes

* Also copy kakadu v8_5 for the upcoming PR ([04e456c](https://github.com/dasch-swiss/sipi/commit/04e456ce5a1174682eb61362f46b484e8fb0ffdf))
* Don't reinstall, if it's present (e.g. with updated CI base image) ([4159054](https://github.com/dasch-swiss/sipi/commit/41590548e6f7413467e7886cbd8810c54a0495af))
* Fix build on MacOS/CI ([#473](https://github.com/dasch-swiss/sipi/issues/473)) ([d06c2e9](https://github.com/dasch-swiss/sipi/commit/d06c2e994d95b766ce0fa9ce7c0eae3e7f5bbf54))
* Streaming of large files ([#481](https://github.com/dasch-swiss/sipi/issues/481)) ([1845a00](https://github.com/dasch-swiss/sipi/commit/1845a0047fc30bbcf095c8230226e3cb90fae6ce))
* TIFF tiled reading bug ([#478](https://github.com/dasch-swiss/sipi/issues/478)) ([a2ee1bf](https://github.com/dasch-swiss/sipi/commit/a2ee1bf4173d0c00e7705763bbd6e7f04050d344))

## [3.16.0](https://github.com/dasch-swiss/sipi/compare/v3.15.2...v3.16.0) (2025-03-24)


### Features

* Pyramidal TIFF support ([#466](https://github.com/dasch-swiss/sipi/issues/466)) ([807fccf](https://github.com/dasch-swiss/sipi/commit/807fccf0f62434679889ffd7a61b266eca81939a))


### Bug Fixes

* Drop "accepted connection from" from DEBUG to INFO ([e12f5e7](https://github.com/dasch-swiss/sipi/commit/e12f5e7a3f8cb8d9665f2878b37fa7eadd6947d0))

## [3.15.2](https://github.com/dasch-swiss/sipi/compare/v3.15.1...v3.15.2) (2025-03-19)


### Bug Fixes

* Logger.cpp in LuaServer.cpp, remove syslog, missing JSON quotes ([#470](https://github.com/dasch-swiss/sipi/issues/470)) ([b2899d7](https://github.com/dasch-swiss/sipi/commit/b2899d747b25b8476e1d35024c97f5d32b7c026f))

## [3.15.1](https://github.com/dasch-swiss/sipi/compare/v3.15.0...v3.15.1) (2025-03-14)


### Bug Fixes

* Parse percentages for IIIF size more carefully (DEV-4636) ([#468](https://github.com/dasch-swiss/sipi/issues/468)) ([d9a8490](https://github.com/dasch-swiss/sipi/commit/d9a849073a1fe3201077f44b814d5a2fd75cac3f))

## [3.15.0](https://github.com/dasch-swiss/sipi/compare/v3.14.0...v3.15.0) (2025-01-24)


### Features

* Return 404 for missing files (INFRA-735) ([#465](https://github.com/dasch-swiss/sipi/issues/465)) ([f54356f](https://github.com/dasch-swiss/sipi/commit/f54356f443f8bbb5dcb061384bd1aebbac4e85da))


### Bug Fixes

* Fix broken links and disable libwebp building its CLI tool ([#462](https://github.com/dasch-swiss/sipi/issues/462)) ([de9e393](https://github.com/dasch-swiss/sipi/commit/de9e393fe5e0b335751bd6166fefb5727723c675))
* Prevent exif nullptr (DEV-4521) ([#464](https://github.com/dasch-swiss/sipi/issues/464)) ([4231f00](https://github.com/dasch-swiss/sipi/commit/4231f00e839a7209f27be4548cd2ab16d387b25b))

## [3.14.0](https://github.com/dasch-swiss/sipi/compare/v3.13.0...v3.14.0) (2024-10-08)


### Features

* Respect X-Forwarded-Proto (DEV-3499) ([#453](https://github.com/dasch-swiss/sipi/issues/453)) ([d5eaab3](https://github.com/dasch-swiss/sipi/commit/d5eaab3bb36114759861c98a419ecca6955d41ce))
* Support IIIF Image's API HEAD requests (DEV-4072) ([#456](https://github.com/dasch-swiss/sipi/issues/456)) ([f3a9a96](https://github.com/dasch-swiss/sipi/commit/f3a9a9695a3c931e0d95144a07d1f7ba0bc182ae))
* Watermarks with alpha support and respect to ratio (DEV-4072) ([#458](https://github.com/dasch-swiss/sipi/issues/458)) ([da34f6a](https://github.com/dasch-swiss/sipi/commit/da34f6af0c51ceb3a47607e6f9ce9e2541a7497d))


### Bug Fixes

* Fix planar BigTIFF ingestion (DEV-3384) ([#455](https://github.com/dasch-swiss/sipi/issues/455)) ([83c2c94](https://github.com/dasch-swiss/sipi/commit/83c2c949fc9dc0c97fbd777f77a11adf38eac883))

## [3.13.0](https://github.com/dasch-swiss/sipi/compare/v3.12.3...v3.13.0) (2024-08-07)


### Features

* Allow YCbCr/RGB autoconvert, update libtiff (DEV-3863) ([#451](https://github.com/dasch-swiss/sipi/issues/451)) ([ccc68bc](https://github.com/dasch-swiss/sipi/commit/ccc68bc797e59befac48e29cab15b638a0b1f8aa))

## [3.12.3](https://github.com/dasch-swiss/sipi/compare/v3.12.2...v3.12.3) (2024-07-23)


### Bug Fixes

* Update ijg jpeg lib to v9f, partially fixes DEV-3474 ([#449](https://github.com/dasch-swiss/sipi/issues/449)) ([94e72fa](https://github.com/dasch-swiss/sipi/commit/94e72fa1cd3af41705e5f505e50bdaa28cee8b6b))

## [3.12.2](https://github.com/dasch-swiss/sipi/compare/v3.12.1...v3.12.2) (2024-04-07)


### Bug Fixes

* Triggering release creation ([8214a37](https://github.com/dasch-swiss/sipi/commit/8214a37fae0290eea6b29ff24760d288b0cb0baa))

## [3.12.1](https://github.com/dasch-swiss/sipi/compare/v3.12.0...v3.12.1) (2024-04-07)


### Bug Fixes

* Triggering release creation ([7434ed4](https://github.com/dasch-swiss/sipi/commit/7434ed42edb3869d542c96add443ae9af1d16b6d))

## [3.12.0](https://github.com/dasch-swiss/sipi/compare/v3.11.0...v3.12.0) (2024-04-07)


### Features

* Trying out release-please ([4ca8169](https://github.com/dasch-swiss/sipi/commit/4ca8169d87eee061350fed530e1318a2e21ed7f9))

## 3.11.0 (2024-04-05)


### Features

* -f option in curl, does not give output if status is not 200 ([d523213](https://github.com/dasch-swiss/sipi/commit/d523213f0eb657526f480dfe15bca6745709e716))
* Add better support for additional mimetypes, especially *.odd and *.rng ([#384](https://github.com/dasch-swiss/sipi/issues/384)) ([473474f](https://github.com/dasch-swiss/sipi/commit/473474f3d5f73f0f7fde99d2f5a9cce24f84d885))
* Add IIIF 3.0 support ([#324](https://github.com/dasch-swiss/sipi/issues/324)) ([d04725a](https://github.com/dasch-swiss/sipi/commit/d04725a55893e1326595a0560f6811c7923166e5))
* Add mimetype to knora json response ([#305](https://github.com/dasch-swiss/sipi/issues/305)) ([81ab98a](https://github.com/dasch-swiss/sipi/commit/81ab98a91ba7abc91fc33f4226034c2d1b9724aa))
* Add triggering of preflight script for non-image file types (DEV-1664) ([#381](https://github.com/dasch-swiss/sipi/issues/381)) ([b86428a](https://github.com/dasch-swiss/sipi/commit/b86428a952e7db85604da6a65e278a45d0bdddf4))
* Bash test script exit value ([876c270](https://github.com/dasch-swiss/sipi/commit/876c270c7485b0762fc0094d62664df1f7d289b5))
* Extend knora info for video ([#371](https://github.com/dasch-swiss/sipi/issues/371)) ([61cf681](https://github.com/dasch-swiss/sipi/commit/61cf68175e612b878f4cad92cfddec7df1acbc8a))
* Fixes, PDF, IIIF Auth, JPX compression parameters ([#290](https://github.com/dasch-swiss/sipi/issues/290)) ([6f46892](https://github.com/dasch-swiss/sipi/commit/6f46892fc36290cb1bd823b7b1b1659d4301fc79))
* Revert transparent JPG behavior ([#418](https://github.com/dasch-swiss/sipi/issues/418)) ([3675997](https://github.com/dasch-swiss/sipi/commit/3675997f9dc2249f48b9d7c54cef9ea162f697a6))
* Tilted images are imported without tilting (DEV-31) ([#374](https://github.com/dasch-swiss/sipi/issues/374)) ([8f65c6c](https://github.com/dasch-swiss/sipi/commit/8f65c6c451ddee5827bc209ae9399e12110294d7))
* Use gray instead of black values for transparent parts of image when returning as JPEG ([#412](https://github.com/dasch-swiss/sipi/issues/412)) ([f18dcfc](https://github.com/dasch-swiss/sipi/commit/f18dcfc28af5ce1f94a3762cc81cddda209a1461))


### Bug Fixes

* (SipiCache and Logger): added missing libraries ([6cb8a59](https://github.com/dasch-swiss/sipi/commit/6cb8a5955aa64715edf958b72dc02e24aebc660c))
* Add fix for [#83](https://github.com/dasch-swiss/sipi/issues/83). ([44b2090](https://github.com/dasch-swiss/sipi/commit/44b2090b8b60d3629adc91a1d041b85f612b8507))
* Add missing file ([#361](https://github.com/dasch-swiss/sipi/issues/361)) ([71ec889](https://github.com/dasch-swiss/sipi/commit/71ec88983c2eb97623f35e430646a6a48beb5388))
* Add missing files to Docker image ([#342](https://github.com/dasch-swiss/sipi/issues/342)) ([9960014](https://github.com/dasch-swiss/sipi/commit/996001478294909cd03241e34f293238e3beeaa6))
* Add missing include. ([e784251](https://github.com/dasch-swiss/sipi/commit/e784251452fffd9053c9034193a63534811c7c35))
* Better handling of missing sidecar files ([#376](https://github.com/dasch-swiss/sipi/issues/376)) ([bed711f](https://github.com/dasch-swiss/sipi/commit/bed711fc9aa41fea095f139c301f9dfd8f5185fc))
* Correct typo in favicon route. ([7a2e320](https://github.com/dasch-swiss/sipi/commit/7a2e3200de16da832f24d969536b19ed9a42d5c9))
* Crashing on jp2 decompression ([#407](https://github.com/dasch-swiss/sipi/issues/407)) ([93308c2](https://github.com/dasch-swiss/sipi/commit/93308c2bdd2d68cdea02808eda788833d2aaf83d))
* Docs deployment ([e884677](https://github.com/dasch-swiss/sipi/commit/e884677db6cec16b34e2b923b042d2031421b35a))
* Don’t make detached threads, and improve signal handling ([#93](https://github.com/dasch-swiss/sipi/issues/93)). ([4365500](https://github.com/dasch-swiss/sipi/commit/4365500644cc1cceb816d04d9711007e60364beb))
* Fix deadlock on mutex protecting thread_ids. ([f42d3e3](https://github.com/dasch-swiss/sipi/commit/f42d3e3620df18ecd5a65830f0a295736d188d89))
* Fix memory leak in SipiImage. ([aadc63e](https://github.com/dasch-swiss/sipi/commit/aadc63eaa9b89898b1ae85a421087792bcce610a))
* IIIF URL redirection ([#417](https://github.com/dasch-swiss/sipi/issues/417)) ([1905bf2](https://github.com/dasch-swiss/sipi/commit/1905bf2fe740960cc87dae23f71c8268ccde8fd9))
* Incorrect error message ([#383](https://github.com/dasch-swiss/sipi/issues/383)) ([94b50f9](https://github.com/dasch-swiss/sipi/commit/94b50f9985ab6582cfc234e229f37f1344d49c67))
* Invalid watermark crashes sipi ([#406](https://github.com/dasch-swiss/sipi/issues/406)) ([39dc0e8](https://github.com/dasch-swiss/sipi/commit/39dc0e87b5fcdebee162cf251736e3ee9a5229af))
* Issues ([#328](https://github.com/dasch-swiss/sipi/issues/328)) ([2d78c55](https://github.com/dasch-swiss/sipi/commit/2d78c553cfb7465ebdca0abf195f6cebdf1870a9))
* Kakadu error ([#341](https://github.com/dasch-swiss/sipi/issues/341))(DSP-1247) ([d0bc37a](https://github.com/dasch-swiss/sipi/commit/d0bc37a047a8ee75160d490ac9f6903470547c2e))
* Knora upload scripts and configs ([#174](https://github.com/dasch-swiss/sipi/issues/174)) ([51054d0](https://github.com/dasch-swiss/sipi/commit/51054d02f278320d5694b3063070b5dc1370e3c1))
* **knora.json:** Return origin instead of wildcard (DEV-318) ([#369](https://github.com/dasch-swiss/sipi/issues/369)) ([3b73ff7](https://github.com/dasch-swiss/sipi/commit/3b73ff726e987a675a6927c63b6b23e3c75584ea))
* Make signal handler code asynchronous-safe. ([8cc43b3](https://github.com/dasch-swiss/sipi/commit/8cc43b39b5b6177119834c655c8db3d6b5fffe66))
* Memory leaks (DEV-237) ([#365](https://github.com/dasch-swiss/sipi/issues/365)) ([c3b9b35](https://github.com/dasch-swiss/sipi/commit/c3b9b3519bfb2b3d10f50c72c2e81a8d2b506f35))
* Palette color tiffs now read correctly ([83037c5](https://github.com/dasch-swiss/sipi/commit/83037c581acfbcfd3552c7fb9f7d3be20986c427))
* Palette color tiffs now read correctly ([#253](https://github.com/dasch-swiss/sipi/issues/253)) ([83037c5](https://github.com/dasch-swiss/sipi/commit/83037c581acfbcfd3552c7fb9f7d3be20986c427))
* Parse url crash ([#340](https://github.com/dasch-swiss/sipi/issues/340)) (DSP-1247) ([e710237](https://github.com/dasch-swiss/sipi/commit/e7102379998ab38c0b6162fa4c4a3599eb328cb2))
* Printed version strings (DSP-687) ([#332](https://github.com/dasch-swiss/sipi/issues/332)) ([9778b19](https://github.com/dasch-swiss/sipi/commit/9778b19aa27536dd4b11574d6d62fb1824d021cf))
* Replace custom HTTP client code with libcurl. ([fb1bdfc](https://github.com/dasch-swiss/sipi/commit/fb1bdfced897b7e1e5268b8c2f9fe2859b83ae14))
* Return Internal Server Error if Lua function has invalid return value. ([2c72dde](https://github.com/dasch-swiss/sipi/commit/2c72dde59a847b43465f347d3d93aab4c238b5c2))
* Small bugfix in CMakeLists.txt ([cf1c275](https://github.com/dasch-swiss/sipi/commit/cf1c2758c385538f6a6f331780b6c0045a7aa24d))
* Support for grayscale jpegs ([#410](https://github.com/dasch-swiss/sipi/issues/410)) ([fd63dd0](https://github.com/dasch-swiss/sipi/commit/fd63dd089db12c7344d2665dff63b6d2586ae1de))
* Support for TIFF with CMYK and alpha channel ([#408](https://github.com/dasch-swiss/sipi/issues/408)) ([c0cc033](https://github.com/dasch-swiss/sipi/commit/c0cc033cab0cb8d86a267f532eff9e13eff6fee8))
* Take watermark into account when caching ([#421](https://github.com/dasch-swiss/sipi/issues/421)) ([18788b2](https://github.com/dasch-swiss/sipi/commit/18788b20e19d0fe7af97f6e791822f5d503d2650))
* Uploading PNGs with transparency crashes SIPI ([#375](https://github.com/dasch-swiss/sipi/issues/375)) ([01104b5](https://github.com/dasch-swiss/sipi/commit/01104b520ea5e360154bec5b2f4b8080a264e3e4))
* Use PATCH_COMMAND instead of UPDATE_COMMAND so the same patch isn't applied twice ([#53](https://github.com/dasch-swiss/sipi/issues/53)) ([1f9695a](https://github.com/dasch-swiss/sipi/commit/1f9695a8a0ee7b21090699e8ea3750694fa09d50))
* Use prefix if provided in sipi.init-knora-test.lua. ([4b99130](https://github.com/dasch-swiss/sipi/commit/4b99130f3d585e1928a12e85d6fe699c653608aa))
* Use RAII to manage libcurl connections. ([87f1366](https://github.com/dasch-swiss/sipi/commit/87f13664098fb18a22ce05c1d10416efe5848930))
* Watermark support ([#405](https://github.com/dasch-swiss/sipi/issues/405)) ([b7abe85](https://github.com/dasch-swiss/sipi/commit/b7abe857cc7acc6a9820ed327c5e913389d72a44))


### Miscellaneous Chores

* Release 3.11.0 ([f76eebb](https://github.com/dasch-swiss/sipi/commit/f76eebb7a91bd4722f3be5419c9f897336247abb))
