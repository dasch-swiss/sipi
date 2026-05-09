{
  description = "Sipi — IIIF-compatible media server";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    let
      # Kakadu archive (proprietary). Fetched by a fixed-output derivation
      # (FOD) from the dsp-ci-assets GitHub release via `gh release
      # download`. The sha256 pin makes the build deterministic; a content
      # mismatch fails the FOD instead of producing a silently-different
      # binary.
      #
      # Local dev: export GH_TOKEN=$(gh auth token) once per shell.
      # CI:        env: { GH_TOKEN: ${secrets.DASCHBOT_PAT} } per Nix step.
      # After the first successful build lands in Cachix, downstream
      # consumers substitute the content-addressed output and never need
      # a token.
      #
      # Bump procedure:
      #   1. Update kakaduVersion, kakaduAssetName, kakaduSha256 here.
      #   2. nix build .#default   (FOD re-fetches on hash change)
      kakaduVersion   = "v8.5";
      kakaduAssetName = "v8_5-01382N.zip";
      kakaduSha256    = "c19c7579d1dee023316e7de090d9de3eb24764e349b4069e5af3a540fb644e75";

      mkKakaduArchive = pkgs: pkgs.stdenv.mkDerivation {
        name = kakaduAssetName;

        outputHashMode = "flat";
        outputHashAlgo = "sha256";
        outputHash     = kakaduSha256;

        nativeBuildInputs = [ pkgs.gh pkgs.cacert ];

        impureEnvVars = [ "GH_TOKEN" "GITHUB_TOKEN" ]
          ++ pkgs.lib.fetchers.proxyImpureEnvVars;

        dontUnpack = true;
        dontInstall = true;

        buildPhase = ''
          runHook preBuild

          # gh writes transient state to $XDG_CONFIG_HOME/gh even with
          # GH_TOKEN set; /homeless-shelter is read-only in the Nix
          # sandbox, so redirect.
          export HOME=$TMPDIR
          export XDG_CONFIG_HOME=$TMPDIR/.config
          mkdir -p "$XDG_CONFIG_HOME/gh"

          # Go's TLS needs a cert bundle; /etc/ssl is not in the sandbox.
          export SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt

          if [ -z "''${GH_TOKEN:-}" ] && [ -z "''${GITHUB_TOKEN:-}" ]; then
            echo "error: GH_TOKEN or GITHUB_TOKEN must be set to fetch Kakadu" >&2
            echo "  local dev: export GH_TOKEN=\$(gh auth token)" >&2
            echo "  CI:        env: { GH_TOKEN: \''${{ secrets.DASCHBOT_PAT }} }" >&2
            exit 1
          fi

          gh release download "kakadu-${kakaduVersion}" \
            --repo dasch-swiss/dsp-ci-assets \
            --pattern '${kakaduAssetName}' \
            --output $out

          runHook postBuild
        '';
      };

      overlay = final: prev: {
        kakaduArchive = mkKakaduArchive final;
        # `pkgs.sipi` uses clang + libc++ to match the Docker build and the
        # historic dev-shell toolchain. Without the explicit override, the
        # derivation picks up nixpkgs' default stdenv (GCC on Linux), which
        # rejects the `-stdlib=libc++` flags set in `package.nix`.
        sipi = prev.callPackage ./package.nix {
          inherit (final) kakaduArchive;
          stdenv = final.llvmPackages_19.libcxxStdenv;
        };
      };
    in
    {
      overlays.default = overlay;
    }
    //
    flake-utils.lib.eachSystem [
      "x86_64-linux"
      "aarch64-linux"
      "x86_64-darwin"
      "aarch64-darwin"
    ] (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [ overlay ];
        };

        version = pkgs.lib.strings.trim (builtins.readFile ./version.txt);

        filteredSrc = pkgs.lib.fileset.toSource {
          root = ./.;
          fileset = pkgs.lib.fileset.unions [
            ./CMakeLists.txt
            ./version.txt
            ./generate_icc_header.sh
            ./cmake
            ./ext
            ./vendor
            ./include
            ./src
            ./shttps
            ./fuzz
            ./patches
            ./test
            ./config
            ./scripts
            ./server
          ];
        };

      in
      {
        # ── Packages ───────────────────────────────────────────────────
        packages = {
          # Default: RelWithDebInfo, Clang/libc++, separateDebugInfo
          #   Debug symbols via: nix build .#default.debug
          default = pkgs.sipi;

          # Kakadu archive FOD (cached by content hash after first fetch).
          # Exposed so `nix build .#kakaduArchive` can pre-populate the
          # store in isolation, and so `nix flake check` covers it.
          kakaduArchive = pkgs.kakaduArchive;

          # Debug build with coverage instrumentation
          dev = pkgs.sipi.override {
            cmakeBuildType = "Debug";
            enableCoverage = true;
            enableTests = true;
          };

          # Release build, unstripped (for manual distribution)
          release = (pkgs.sipi.override {
            cmakeBuildType = "Release";
            enableTests = false;
          }).overrideAttrs { dontStrip = true; };

          # Production Docker image: see `//src:image` (Bazel rules_oci)
          # — replaced this attribute set's `.#docker`, `.#docker-stream`,
          # and `.#sipi-debug` outputs in DEV-6346 (PR Y+4). Image build
          # path: `just bazel-docker-build-${arch}` →
          # `bazel run //src:image_load`; debug-info split:
          # `just bazel-docker-extract-debug ${arch}` →
          # `bazel build //src:sipi_debug_layout`. INFRA-1226 moves the
          # in-image HEALTHCHECK to compose-level (Bazel image ships
          # HEALTHCHECK-agnostic per OCI spec).
        };

        # ── Dev Shells ─────────────────────────────────────────────────
        devShells = rec {
          default = clang;

          # Clang + libc++: matches Docker and macOS builds
          clang = pkgs.mkShell.override {
            stdenv = pkgs.llvmPackages_19.libcxxStdenv;
          } {
            name = "sipi";
            hardeningDisable = [ "all" ];
            inputsFrom = [ pkgs.sipi ];
            packages = with pkgs; [
              # Dev-only tools not needed for the build itself
              just
              gcovr
              lcov
              llvmPackages_19.llvm

              # Rust toolchain (e2e test harness)
              rustc
              cargo
              hurl

              # Additional test dependencies
              nginx
              graphicsmagick
              imagemagick
              libxml2
              libxslt

              # Bazel build orchestration (DEV-6342). bazelisk reads
              # `.bazelversion` and downloads the matching Bazel.
              bazelisk
              # `bazel` shim — nixpkgs' bazelisk package ships only a
              # `bazelisk` binary, but the broader Bazel ecosystem and
              # most users' muscle memory expect to type plain `bazel`.
              # Surface a one-line shell wrapper that `exec`s through to
              # bazelisk so both `bazel build //src:sipi` and
              # `bazelisk build //src:sipi` work interactively. The CI
              # workflow (`bazel-build-dispatch.yml`) keeps using
              # `bazelisk` directly to make the version source explicit.
              (writeShellScriptBin "bazel" ''
                exec ${bazelisk}/bin/bazelisk "$@"
              '')
              # Host-tool deps used by the Bazel graph:
              #  - perl       — openssl `Configure` (research §9)
              #  - cmake      — `rules_foreign_cc` `cmake()` invocations
              #  - pkg-config — `configure_make()` autotools probes (curl ↔ openssl)
              #  - autoconf / automake / libtool / m4
              #               — `autoreconf -fi` preflight for libmagic (the
              #                 upstream `file` tarball ships configure.ac
              #                 only); also smooths libpng / lcms2 / openssl
              #                 builds whose Makefiles call libtoolize
              #  - gh         — `kakadu_archive` repository_rule shells out
              #                 here (was previously in the Kakadu FOD's
              #                 `nativeBuildInputs`; the FOD goes away in Y+6)
              #  - cacert     — gh's Go-based TLS needs an explicit cert
              #                 bundle on Linux dev shells without
              #                 /etc/ssl/certs (defensive parity with the
              #                 deleted FOD's behaviour)
              #  - crane      — `just bazel-docker-publish-manifest` shells
              #                 out to `crane index append` to assemble the
              #                 multi-arch manifest from the two pushed
              #                 per-arch digests (DEV-6346, PR Y+4).
              #                 `oci_image_index` cannot consume external
              #                 digests pushed from a different build graph
              #                 (research-findings §1).
              perl
              cmake
              pkg-config
              autoconf
              automake
              libtool
              m4
              gh
              cacert
              # `crane` CLI from go-containerregistry — the package in
              # nixpkgs is `go-containerregistry`, which installs the
              # `crane` binary (and `gcrane`, `krane`) on PATH. Used by
              # `just bazel-docker-publish-manifest` to assemble the
              # multi-arch manifest from the two pushed per-arch digests.
              go-containerregistry
            ];

            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
              export MKSHELL=clang
              git config core.hooksPath .githooks 2>/dev/null || true

              # Match the Kakadu FOD's TLS setup (flake.nix:62) for the
              # Bazel-side `gh release download` flow inside the dev shell.
              export SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt

              # Prepend system paths so toolchains_llvm's `xcrun --show-sdk-path`
              # probe (run inside Bazel repo rules with `--repo_env=PATH`)
              # finds Apple's `/usr/bin/xcrun` ahead of Nix's xcbuild
              # reimplementation. Nix's xcrun unconditionally returns the
              # nixpkgs apple-sdk-14.4 stub (which ships only private
              # frameworks — no `libc++.tbd`/`libc++abi.tbd`), and it
              # ignores `SDKROOT`/`DEVELOPER_DIR` overrides, so the only
              # way to get the system Xcode CLT SDK is to make sure the
              # system xcrun runs instead. Nix tools used by the Bazel
              # graph (`gh` for kakadu_archive, `perl`/`autoconf`/etc. for
              # foreign_cc rules) live in subsequent PATH entries and
              # remain reachable.
              if [ "$(uname)" = "Darwin" ]; then
                export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"
              fi
            '';
          };

          # Clang + libstdc++ for libFuzzer ABI compatibility
          fuzz = pkgs.mkShell.override {
            stdenv = pkgs.llvmPackages_19.stdenv;
          } {
            name = "sipi-fuzz";
            hardeningDisable = [ "all" ];
            inputsFrom = [ pkgs.sipi ];
            packages = with pkgs; [
              just
              gcovr
              lcov
              llvmPackages_19.llvm
              rustc
              cargo
              hurl
              nginx
              graphicsmagick
              imagemagick
              libxml2
              libxslt

              # Bazel host tools (see `clang` shell for rationale).
              bazelisk
              # `bazel` shim — see clang shell for rationale.
              (writeShellScriptBin "bazel" ''
                exec ${bazelisk}/bin/bazelisk "$@"
              '')
              perl
              cmake
              pkg-config
              autoconf
              automake
              libtool
              m4
              gh
              cacert
            ];

            shellHook = ''
              export PS1="\\u@\\h | nix-develop-fuzz> "
              export MKSHELL=fuzz
              git config core.hooksPath .githooks 2>/dev/null || true
              export SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt
              # See the `clang` shell for the rationale.
              if [ "$(uname)" = "Darwin" ]; then
                export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"
              fi
            '';
          };

          # GCC shell
          gcc = pkgs.mkShell.override {
            stdenv = pkgs.gcc14Stdenv;
          } {
            name = "sipi";
            hardeningDisable = [ "all" ];
            inputsFrom = [ pkgs.sipi ];
            packages = with pkgs; [
              just
              gcovr
              lcov
              llvmPackages_19.llvm
              rustc
              cargo
              hurl
              nginx
              graphicsmagick
              imagemagick
              libxml2
              libxslt

              # Bazel host tools (see `clang` shell for rationale).
              bazelisk
              # `bazel` shim — see clang shell for rationale.
              (writeShellScriptBin "bazel" ''
                exec ${bazelisk}/bin/bazelisk "$@"
              '')
              perl
              cmake
              pkg-config
              autoconf
              automake
              libtool
              m4
              gh
              cacert
            ];

            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
              export MKSHELL=default
              git config core.hooksPath .githooks 2>/dev/null || true
              export SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt
            '';
          };
        };
      });
}
