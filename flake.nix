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

        # Bazelisk reads `.bazelversion`; rules_foreign_cc shells out to the
        # host tools (perl/cmake/pkg-config/autotools); the `kakadu_archive`
        # repository_rule shells out to `gh`; `crane` is used by the
        # `bazel-docker-publish-manifest` recipe.
        commonPackages = with pkgs; [
          bazelisk
          (writeShellScriptBin "bazel" ''exec ${bazelisk}/bin/bazelisk "$@"'')
          perl cmake pkg-config autoconf automake libtool m4
          gh cacert go-containerregistry
          just gcovr lcov llvmPackages_19.llvm
          rustc cargo hurl
          nginx graphicsmagick imagemagick libxml2 libxslt
          # jpylyzer — JP2 conformance validator (Phase 15.11). Runs against
          # regenerated JP2 goldens to confirm the SIPI UUID box reads as an
          # informational `Unknown UUID` and the file otherwise passes
          # ISO/IEC 15444-1 conformance.
          python3Packages.jpylyzer
        ];

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

        mkSipiShell = { name, stdenv }: pkgs.mkShell.override { inherit stdenv; } {
          inherit name;
          hardeningDisable = [ "all" ];
          packages = commonPackages;
          shellHook = ''export PS1="\\u@\\h | ${name}> "'' + commonShellHook;
        };

      in {
        devShells = {
          # Clang + libc++ — matches the toolchains_llvm production toolchain.
          default = mkSipiShell { name = "sipi"; stdenv = pkgs.llvmPackages_19.libcxxStdenv; };
          # GCC + libstdc++ — diagnostic escape hatch; not used by CI.
          gcc = mkSipiShell { name = "sipi-gcc"; stdenv = pkgs.gcc14Stdenv; };
        };
      });
}
