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
        sipi = prev.callPackage ./package.nix {
          inherit (final) kakaduArchive;
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

        isLinux = pkgs.stdenv.isLinux;

        # Kakadu archive (FOD from dsp-ci-assets release). Provided by the
        # overlay; reused directly here for mkStaticBuild.
        kakaduArchive = pkgs.kakaduArchive;

        # ── Static builds (Linux only, Zig-in-Nix) ──────────────────────
        mkStaticBuild = { arch, zigTarget }: pkgs.stdenv.mkDerivation {
          pname = "sipi-static-${arch}";
          inherit version;
          src = filteredSrc;

          nativeBuildInputs = with pkgs; [
            cmake zig autoconf automake libtool
            unzip file xxd pkg-config
            perl  # OpenSSL's ExternalProject Configure script uses `#!/usr/bin/env perl`
          ];

          # Zig writes to a global cache dir; in the Nix sandbox HOME points
          # at /homeless-shelter (read-only), so we redirect it into the
          # build directory.
          env = {
            ZIG_GLOBAL_CACHE_DIR = "/build/.zig-cache";
            ZIG_LOCAL_CACHE_DIR = "/build/.zig-local-cache";
          };

          configurePhase = ''
            runHook preConfigure

            mkdir -p "$ZIG_GLOBAL_CACHE_DIR" "$ZIG_LOCAL_CACHE_DIR"

            # generate_icc_header.sh has a `#!/bin/bash` shebang; /bin/bash
            # doesn't exist in the Nix sandbox, so the kernel's exec fails
            # with ENOENT. patchShebangs rewrites the shebang to a store path.
            patchShebangs generate_icc_header.sh

            # Inject the pinned Kakadu archive into vendor/ (ext/kakadu expects
            # it under the exact upstream filename).
            cp ${kakaduArchive} vendor/v8_5-01382N.zip

            cmake -S . -B build \
              -G "Unix Makefiles" \
              -DCMAKE_TOOLCHAIN_FILE=cmake/zig-toolchain.cmake \
              -DZIG_TARGET=${zigTarget} \
              -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
              -DBUILD_TESTING=OFF \
              -DEXT_PROVIDED_VERSION=${version}

            runHook postConfigure
          '';

          buildPhase = ''
            cmake --build build --parallel $NIX_BUILD_CORES
          '';

          installPhase = ''
            mkdir -p $out/bin
            cp build/sipi $out/bin/

            mkdir -p $out/share/sipi/config $out/share/sipi/scripts $out/share/sipi/server
            cp config/sipi.config.lua      $out/share/sipi/config/
            cp config/sipi.init.lua        $out/share/sipi/config/
            cp server/test.html            $out/share/sipi/server/
            cp scripts/test_functions.lua  $out/share/sipi/scripts/
            cp scripts/send_response.lua   $out/share/sipi/scripts/
          '';

          outputs = [ "out" "debug" ];
          postFixup = ''
            mkdir -p $debug/lib/debug
            ${pkgs.binutils-unwrapped}/bin/objcopy --only-keep-debug $out/bin/sipi $debug/lib/debug/sipi.debug
            ${pkgs.binutils-unwrapped}/bin/strip $out/bin/sipi
            ${pkgs.binutils-unwrapped}/bin/objcopy --add-gnu-debuglink=$debug/lib/debug/sipi.debug $out/bin/sipi
          '';
        };

        # ── Release archives (tarball + checksum + debug symbols) ────────
        mkReleaseArchive = arch: let
          static = self.packages.${system}."static-${arch}";
        in pkgs.runCommand "sipi-release-${arch}" { } ''
          mkdir -p $out
          DIR="sipi-v${version}-linux-${arch}"
          mkdir -p work/$DIR/config work/$DIR/scripts work/$DIR/server

          cp ${static}/bin/sipi work/$DIR/sipi

          cp ${static}/share/sipi/config/sipi.config.lua  work/$DIR/config/
          cp ${static}/share/sipi/config/sipi.init.lua     work/$DIR/config/
          cp ${static}/share/sipi/server/test.html         work/$DIR/server/
          cp ${static}/share/sipi/scripts/test_functions.lua work/$DIR/scripts/
          cp ${static}/share/sipi/scripts/send_response.lua  work/$DIR/scripts/

          tar czf $out/$DIR.tar.gz -C work $DIR
          sha256sum $out/$DIR.tar.gz > $out/$DIR.tar.gz.sha256

          cp ${static.debug}/lib/debug/sipi.debug $out/sipi-linux-${arch}.debug
        '';
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

        } // pkgs.lib.optionalAttrs isLinux {
          # Static Linux binaries (Zig toolchain, musl)
          static-amd64 = mkStaticBuild {
            arch = "amd64";
            zigTarget = "x86_64-linux-musl";
          };
          static-arm64 = mkStaticBuild {
            arch = "arm64";
            zigTarget = "aarch64-linux-musl";
          };

          # Release archives (tarball + checksum + debug symbols)
          release-archive-amd64 = mkReleaseArchive "amd64";
          release-archive-arm64 = mkReleaseArchive "arm64";

          # Docker images via Nix dockerTools
          docker = pkgs.dockerTools.buildLayeredImage {
            name = "daschswiss/sipi";
            tag = self.rev or "dev";
            maxLayers = 125;
            contents = with pkgs; [
              sipi
              cacert
              dockerTools.fakeNss
              bashInteractive
              coreutils
              ffmpeg
              curl
            ];
            config = {
              Cmd = [ "${pkgs.sipi}/bin/sipi" "--config=/sipi/config/sipi.config.lua" ];
              ExposedPorts = { "1024/tcp" = { }; };
              WorkingDir = "/sipi";
              Env = [
                "SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
                "LC_ALL=en_US.UTF-8"
                "LANG=en_US.UTF-8"
              ];
            };
            fakeRootCommands = ''
              mkdir -p ./sipi/images/knora
              mkdir -p ./sipi/cache
              mkdir -p ./sipi/config
              mkdir -p ./sipi/scripts
              mkdir -p ./sipi/server
              cp ${pkgs.sipi}/share/sipi/config/sipi.config.lua  ./sipi/config/
              cp ${pkgs.sipi}/share/sipi/config/sipi.init.lua    ./sipi/config/
              cp ${pkgs.sipi}/share/sipi/server/test.html         ./sipi/server/
              cp ${pkgs.sipi}/share/sipi/scripts/test_functions.lua ./sipi/scripts/
              cp ${pkgs.sipi}/share/sipi/scripts/send_response.lua  ./sipi/scripts/
            '';
          };

          docker-stream = pkgs.dockerTools.streamLayeredImage {
            name = "daschswiss/sipi";
            tag = self.rev or "dev";
            maxLayers = 125;
            contents = with pkgs; [
              sipi
              cacert
              dockerTools.fakeNss
              bashInteractive
              coreutils
              ffmpeg
              curl
            ];
            config = {
              Cmd = [ "${pkgs.sipi}/bin/sipi" "--config=/sipi/config/sipi.config.lua" ];
              ExposedPorts = { "1024/tcp" = { }; };
              WorkingDir = "/sipi";
              Env = [
                "SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
                "LC_ALL=en_US.UTF-8"
                "LANG=en_US.UTF-8"
              ];
            };
            fakeRootCommands = ''
              mkdir -p ./sipi/images/knora
              mkdir -p ./sipi/cache
              mkdir -p ./sipi/config
              mkdir -p ./sipi/scripts
              mkdir -p ./sipi/server
              cp ${pkgs.sipi}/share/sipi/config/sipi.config.lua  ./sipi/config/
              cp ${pkgs.sipi}/share/sipi/config/sipi.init.lua    ./sipi/config/
              cp ${pkgs.sipi}/share/sipi/server/test.html         ./sipi/server/
              cp ${pkgs.sipi}/share/sipi/scripts/test_functions.lua ./sipi/scripts/
              cp ${pkgs.sipi}/share/sipi/scripts/send_response.lua  ./sipi/scripts/
            '';
          };
        };

        # ── Dev Shells ─────────────────────────────────────────────────
        devShells = rec {
          default = clang;

          # Clang + libc++: matches Docker, Zig, and macOS builds
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
              apacheHttpd
              imagemagick
              libxml2
              libxslt
            ];

            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
              export MKSHELL=clang
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
              apacheHttpd
              imagemagick
              libxml2
              libxslt
            ];

            shellHook = ''
              export PS1="\\u@\\h | nix-develop-fuzz> "
              export MKSHELL=fuzz
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
              apacheHttpd
              imagemagick
              libxml2
              libxslt
            ];

            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
              export MKSHELL=default
            '';
          };
        };
      });
}
