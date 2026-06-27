{
  description = "Sipi — IIIF-compatible media server (dev shells only; build is Bazel-driven)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachSystem
      [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ]
      (
        system:
        let
          pkgs = import nixpkgs { inherit system; };

          # Bazelisk reads `.bazelversion` and drives the whole build (every
          # C/C++ dep is a native `cc_library` compiled by the hermetic LLVM
          # toolchain — no host cmake/autotools/perl). The `kakadu_archive`
          # repository_rule shells out to `gh`; `crane` (go-containerregistry)
          # is used by the `bazel-docker-publish-manifest` recipe.
          commonPackages = with pkgs; [
            bazelisk
            (writeShellScriptBin "bazel" ''exec ${bazelisk}/bin/bazelisk "$@"'')
            gh
            cacert
            go-containerregistry
            just

            # OpenTofu — drives infra/ (NativeLink RBE backend + TF state
            # bucket). Dev-shell-only: infra deploys are out-of-band ops, not
            # part of the Bazel build graph.
            opentofu

            # jpylyzer — JP2 conformance validator (Phase 15.11). Runs against
            # regenerated JP2 goldens to confirm the SIPI UUID box reads as an
            # informational `Unknown UUID` and the file otherwise passes
            # ISO/IEC 15444-1 conformance.
            python3Packages.jpylyzer
          ];
          # Rust toolchain is intentionally NOT in commonPackages — `rules_rust`
          # in `MODULE.bazel` pins a hermetic rustc (1.89.0). E2E + smoke
          # tests run as `rust_test` Bazel targets in CI and locally; a
          # parallel `cargo` path would risk version skew against the
          # hermetic toolchain.

          commonShellHook = ''
            git config core.hooksPath .githooks 2>/dev/null || true
            # gh's Go-based TLS needs an explicit cert bundle on headless Linux.
            export SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt
            # On macOS, prepend /usr/bin so toolchains_llvm's `xcrun --show-
            # sdk-path` probe finds Apple's xcrun (real Xcode CLT SDK) ahead of
            # nixpkgs' xcbuild stub (apple-sdk-14.4, no `libc++.tbd`).
            if [ "$(uname)" = "Darwin" ]; then
              export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"
            fi
          '';

          mkSipiShell =
            {
              name,
              stdenv,
              extraPackages ? [ ],
            }:
            pkgs.mkShell.override { inherit stdenv; } {
              inherit name;
              # `hardeningDisable` applies only to nixpkgs' wrapper around the
              # dev-shell's compiler. It does NOT affect Bazel actions, which
              # run under toolchains_llvm with their own compile flags
              # (production hardening: -fstack-protector-strong,
              # -D_FORTIFY_SOURCE=2, -fstack-clash-protection on Linux,
              # -Wl,-z,now; see `.bazelrc` "Hardening (production target
              # binary)" block). The disable here remains for the dev shell
              # itself: ad-hoc `nix develop`-driven compiles, shell hooks,
              # and tools that link via Nix's wrapped clang. Keeping it off
              # matches the historical setting and avoids surprising
              # interactions for one-off builds done outside Bazel.
              hardeningDisable = [ "all" ];
              packages = commonPackages ++ extraPackages;
              shellHook = ''export PS1="\\u@\\h | ${name}> "'' + commonShellHook;
            };

        in
        {
          devShells = {
            # Clang + libc++ — matches the toolchains_llvm production toolchain.
            default = mkSipiShell {
              name = "sipi";
              stdenv = pkgs.llvmPackages_19.libcxxStdenv;
            };
            # GCC + libstdc++ — diagnostic escape hatch; not used by CI.
            gcc = mkSipiShell {
              name = "sipi-gcc";
              stdenv = pkgs.gcc14Stdenv;
            };
            # LLVM-host-tools shell — `default` + `llvmPackages_19.llvm`,
            # which ships `llvm-cov`, `llvm-profdata`, and `llvm-symbolizer`.
            # Two consumers, both gated on host LLVM binaries on PATH:
            #   - `just bazel-coverage` — Bazel's `collect_cc_coverage.sh`
            #     hard-requires `COVERAGE_GCOV_PATH` (= llvm-profdata) and
            #     `LLVM_COV` (= llvm-cov); the justfile recipe resolves
            #     them via `$(command -v llvm-{cov,profdata})`.
            #   - ASan/LSan in `just bazel-test-e2e --config=asan` —
            #     `llvm-symbolizer` resolves `(/nix/store/.../sipi+0xOFFSET)`
            #     frames into function names so the name-based
            #     suppressions in `.lsan_suppressions.txt` (`leak:lua*`)
            #     can match.
            # Used by `.github/workflows/coverage.yml` and `sanitizer.yml`.
            # Local devs running coverage or sanitizers should
            # `nix develop .#llvm-tools` instead of the default shell.
            llvm-tools = mkSipiShell {
              name = "sipi-llvm-tools";
              stdenv = pkgs.llvmPackages_19.libcxxStdenv;
              extraPackages = [ pkgs.llvmPackages_19.llvm ];
            };
            # Tracy profiler GUI + capture tool, for `just bazel-build-tracy`
            # sessions. Local-dev only. Gated to Linux: the nixpkgs `tracy` GUI
            # build is unreliable on aarch64-darwin, so macOS devs install the
            # GUI via `brew install tracy` instead (it connects over TCP, so a
            # macOS GUI can also attach to a Tracy-instrumented server on Linux).
            profiling = mkSipiShell {
              name = "sipi-profiling";
              stdenv = pkgs.llvmPackages_19.libcxxStdenv;
              extraPackages = pkgs.lib.optionals pkgs.stdenv.isLinux [ pkgs.tracy ];
            };
          };
        }
      );
}
