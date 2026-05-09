{
  description = "Sipi — IIIF-compatible media server (dev shells only; build is Bazel-driven)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachSystem [
      "x86_64-linux"
      "aarch64-linux"
      "x86_64-darwin"
      "aarch64-darwin"
    ] (system:
      let
        pkgs = import nixpkgs { inherit system; };

        # Tools every shell needs for the Bazel-driven build:
        #
        #  - bazelisk     — reads `.bazelversion` and downloads the matching
        #                   Bazel; a `bazel` shim is added per-shell so plain
        #                   `bazel build` works alongside `bazelisk build`
        #  - perl         — openssl `Configure` (foreign_cc)
        #  - cmake        — `rules_foreign_cc` `cmake()` invocations
        #  - pkg-config   — `configure_make()` autotools probes (curl ↔ openssl)
        #  - autoconf / automake / libtool / m4
        #                 — `autoreconf -fi` preflight for libmagic; also
        #                   smooths libpng / lcms2 / openssl Makefiles
        #  - gh           — `kakadu_archive` repository_rule shells out here
        #  - cacert       — gh's Go-based TLS needs an explicit cert bundle
        #                   on Linux dev shells without /etc/ssl/certs
        #  - go-containerregistry — provides `crane`, used by
        #                   `just bazel-docker-publish-manifest` to assemble
        #                   the multi-arch manifest from the per-arch digests
        #                   pushed in publish.yml
        #
        # Plus quality-of-life tools (just, lcov, gcovr, llvm) and test
        # runtimes (rustc, cargo, hurl, nginx, graphicsmagick, imagemagick).
        commonPackages = with pkgs; [
          # Bazel build orchestration
          bazelisk
          (writeShellScriptBin "bazel" ''
            exec ${bazelisk}/bin/bazelisk "$@"
          '')

          # Bazel host tools
          perl
          cmake
          pkg-config
          autoconf
          automake
          libtool
          m4
          gh
          cacert
          go-containerregistry

          # Recipe + coverage tooling
          just
          gcovr
          lcov
          llvmPackages_19.llvm

          # Rust e2e harness
          rustc
          cargo
          hurl

          # Test runtimes
          nginx
          graphicsmagick
          imagemagick
          libxml2
          libxslt
        ];

        commonShellHook = ''
          git config core.hooksPath .githooks 2>/dev/null || true

          # gh's Go-based TLS needs an explicit cert bundle on Linux dev
          # shells without /etc/ssl/certs.
          export SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt

          # On macOS, prepend system paths so toolchains_llvm's
          # `xcrun --show-sdk-path` probe (run inside Bazel repo rules)
          # finds Apple's `/usr/bin/xcrun` ahead of Nix's xcbuild
          # reimplementation. Nix's xcrun unconditionally returns the
          # nixpkgs apple-sdk-14.4 stub (private frameworks only — no
          # `libc++.tbd`/`libc++abi.tbd`) and ignores SDKROOT/DEVELOPER_DIR
          # overrides, so the only way to reach the system Xcode CLT SDK
          # is to make sure `/usr/bin/xcrun` runs first. Nix tools used by
          # the Bazel graph live in subsequent PATH entries and remain
          # reachable.
          if [ "$(uname)" = "Darwin" ]; then
            export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"
          fi
        '';

      in {
        devShells = rec {
          default = clang;

          # Clang + libc++ — matches the production binary's stdenv and the
          # `bazel build //src:sipi` action's effective toolchain.
          clang = pkgs.mkShell.override {
            stdenv = pkgs.llvmPackages_19.libcxxStdenv;
          } {
            name = "sipi";
            hardeningDisable = [ "all" ];
            packages = commonPackages;
            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
              export MKSHELL=clang
            '' + commonShellHook;
          };

          # Clang + libstdc++ for libFuzzer ABI compatibility on linux.
          # The fuzz harness is `bazel build --config=fuzz`, which selects
          # the libstdc++-flavoured toolchain; this shell mirrors that for
          # local-dev parity with CI's fuzz.yml.
          fuzz = pkgs.mkShell.override {
            stdenv = pkgs.llvmPackages_19.stdenv;
          } {
            name = "sipi-fuzz";
            hardeningDisable = [ "all" ];
            packages = commonPackages;
            shellHook = ''
              export PS1="\\u@\\h | nix-develop-fuzz> "
              export MKSHELL=fuzz
            '' + commonShellHook;
          };

          # GCC shell — useful for cross-compiler diagnostics when the
          # clang toolchain produces a confusing error.
          gcc = pkgs.mkShell.override {
            stdenv = pkgs.gcc14Stdenv;
          } {
            name = "sipi";
            hardeningDisable = [ "all" ];
            packages = commonPackages;
            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
              export MKSHELL=default
            '' + commonShellHook;
          };
        };
      });
}
