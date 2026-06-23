"""`sipi_e2e_test()` — Starlark macro factoring the common wiring shared
by every non-docker Rust e2e test target.

Each `rust_test` needs the same data deps, env vars, args, and tags.
Without the macro a BUILD.bazel repeats ~10 lines per target and the
test contracts drift across targets the moment one is updated and the
others aren't.

What the macro injects:

  * `data`:
      - `//src/cli:sipi`             sipi binary under test, via `SIPI_BIN`.
      - `//:test_fixtures`       `test/_test_data/`, `config/`,
                                 `scripts/`, `server/` materialised so
                                 `sipi_e2e::repo_root()` resolves under
                                 a real-files tree.
      - `//:lsan_suppressions`   `.lsan_suppressions.txt`, consulted by
                                 LSan under `--config=asan`.
      - `:snapshots`             `insta` snapshot files under
                                 `tests/snapshots/`.

  * `env`:
      - `SIPI_BIN`               `$(rootpath //src/cli:sipi)` — runfiles-
                                 relative; `SipiServer` canonicalises
                                 to absolute before spawning.
      - `SIPI_REPO_ROOT`         `$(rootpath //:test_fixtures)` — the
                                 materialised tree. `repo_root()` then
                                 lazily copies it to `$TEST_TMPDIR`
                                 since the `copy_to_directory` output
                                 is read-only and some tests write
                                 under `test_data_dir()`.
      - `INSTA_WORKSPACE_ROOT`   `.` (runfiles workspace root). `insta`
                                 looks under
                                 `./test/e2e/tests/snapshots/`
                                 where the `:snapshots` data dep
                                 materialises goldens.
      - `LSAN_OPTIONS`           `suppressions=$(rootpath //:lsan_suppressions)`.
                                 Cheap (~80 B) and unconditional — the
                                 file only takes effect when LSan is
                                 active (`--config=asan`).

  * `args`:
      - `--test-threads=1`       sipi can't tolerate parallel test load
                                 on the JP2 → JPEG decode path
                                 (musl-static-binary connection drops).
                                 Combined with `tags = ["exclusive"]`
                                 this forces serial execution within
                                 AND between sibling e2e targets.

  * `tags`:
      - `exclusive`              Bazel runs at most one `exclusive`
                                 test at a time on a given runner,
                                 preventing port collisions between
                                 test binaries that all spin up sipi
                                 on `11024 + (PID % 16384)`.
      - `local`                  Forces local-spawn (skips sandboxing).
                                 Required because macOS Bazel's
                                 sandbox interferes with `realpath()`
                                 resolution against the materialised
                                 `:test_fixtures` tree — sipi's
                                 path-traversal guard
                                 (`SipiHttpServer.cpp:validate_resolved_path`)
                                 then rejects every IIIF request with
                                 "Invalid IIIF identifier".

The macro deliberately does NOT cover `docker_smoke` — that target has
its own data shape (the OCI image tarball, `:sipi_image_tar`) and own
env (`SIPI_IMAGE_TAR`, `SIPI_IMAGE_TAG`). Defining it inline is clearer
than threading those through a second macro layer.
"""

load("@rules_rust//rust:defs.bzl", "rust_test")

def sipi_e2e_test(
        name,
        deps = [],
        crate_features = [],
        extra_data = [],
        extra_env = {},
        extra_tags = []):
    """Defines a single Rust e2e test target with the shared sipi wiring.

    Args:
      name: rust_test target name. The convention is for the name to match
        the Cargo `[[test]]` target, i.e. `tests/<name>.rs`.
      deps: additional Rust dependencies on top of `:sipi_e2e` and the
        crate-universe deps the test sources `use`.
      crate_features: extra `--cfg feature=…` flags. Reserved for future
        opt-in features; none of the current non-docker targets use it.
      extra_data: additional runtime files. Most targets don't need any;
        when a test reads a file outside `:test_fixtures` add it here.
      extra_env: additional env vars (merged on top of the shared dict).
      extra_tags: additional Bazel tags (merged on top of `["exclusive",
        "local"]`).
    """
    rust_test(
        name = name,
        srcs = [
            "tests/{}.rs".format(name),
            "tests/common/mod.rs",
        ],
        crate_features = crate_features,
        # The shared `:sipi_e2e` rust_library and the per-test
        # crate-universe deps go through `deps`. Tests that don't `use`
        # any extra crate beyond what `:sipi_e2e` re-exports can leave
        # `deps` empty.
        deps = [":sipi_e2e"] + deps,
        # `:snapshots` attaches the insta golden tree unconditionally —
        # only a couple of tests consume it but the cost is trivial and
        # makes adding new `assert_*_snapshot!()` calls a no-op wiring-wise.
        data = [
            "//src/server-rs:sipi_server",
            "//:test_fixtures",
            "//:lsan_suppressions",
            ":snapshots",
        ] + extra_data,
        env = {
            # The Rust shell is the binary under test (the Phase C cutover): it
            # serves `server` natively and forwards every offline subcommand to
            # the C++ CLI via sipi_cli_main, so it covers both the server and CLI
            # e2e suites. The C++ `//src/cli:sipi` server is retired at the delete.
            "SIPI_BIN": "$(rootpath //src/server-rs:sipi_server)",
            # Points at the `copy_to_directory` output that materialises
            # `version.txt`, `test/_test_data/`, `config/`, `scripts/`,
            # `server/` as real files (no symlinks). Required so sipi's
            # `realpath()`-based path-traversal guard
            # (`SipiHttpServer.cpp:validate_resolved_path`) keeps
            # imgroot's resolved prefix and per-request files in
            # agreement — under a runfiles tree of symlinks the prefix
            # check rejects every IIIF request. The same materialisation
            # also fixes Lua `require` and `insta` snapshot lookups.
            "SIPI_REPO_ROOT": "$(rootpath //:test_fixtures)",
            # `insta` writes/reads snapshots under
            # `<INSTA_WORKSPACE_ROOT>/<package_dir>/tests/snapshots/`.
            # `:snapshots` data dep materialises the goldens at
            # `<runfiles_workspace_root>/test/e2e/tests/snapshots/`,
            # so pointing INSTA_WORKSPACE_ROOT at the runfiles
            # workspace root (`.`, matching the `SIPI_WORKSPACE_ROOT`
            # convention from the C++ unit tests) lets insta resolve
            # them under `./test/e2e/tests/snapshots/<n>.snap`.
            "INSTA_WORKSPACE_ROOT": ".",
            "LSAN_OPTIONS": "suppressions=$(rootpath //:lsan_suppressions)",
        } | extra_env,
        args = ["--test-threads=1"],
        tags = ["exclusive", "local"] + extra_tags,
    )
