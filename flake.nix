{
  description = "Sipi — IIIF-compatible media server";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    # Used by `nix/rust-tests.nix` to build the Rust e2e + Docker smoke
    # test binaries reproducibly with vendored crate sources from
    # `test/e2e-rust/Cargo.lock`. Pinned to a release tag so that
    # `nix flake update` does not silently follow upstream's default
    # branch into a major-version bump.
    crane.url = "github:ipetkov/crane?ref=v0.23.3";
  };

  outputs = { self, nixpkgs, flake-utils, crane, ... }:
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

        # New Nix expressions live in ./nix/<topic>.nix and are imported here.
        craneLib  = crane.mkLib pkgs;
        rustTests = import ./nix/rust-tests.nix { inherit pkgs craneLib; };

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

        # ── Docker image (Nix dockerTools) ───────────────────────────────
        # Bake version.txt's content into the binary and use a `v`-
        # prefixed form as the OCI image tag. release-please updates
        # `version.txt` *before* cutting a release tag and tags the
        # commit `v<version>` (e.g. `v4.1.1`). The historical Dockerfile
        # flow set its tag via `git describe --tag` which inherits the
        # `v` prefix from git, producing `daschswiss/sipi:v4.1.1`; we
        # match that 1:1 by prepending `v` here. Without the prefix
        # the published image tag would silently regress from
        # `v4.1.1` to `4.1.1`, breaking deploy automation that pulls
        # tags by literal name.
        #
        # For ad-hoc/custom-version builds, callers can override at the
        # package layer:
        #   pkgs.sipi.override { providedVersion = "..."; }
        # via a custom Nix expression. The `providedVersion` parameter
        # on `package.nix` enables that, and `pkgs.sipi.version` then
        # propagates into both the binary and the OCI tag below.
        sipiForImage = pkgs.sipi;
        imageTag = if sipiForImage.version != ""
                   then "v" + sipiForImage.version
                   else "dev";

        # `created` timestamp in RFC 3339 / extended ISO 8601 form
        # (YYYY-MM-DDTHH:MM:SSZ). `self.lastModifiedDate` is an
        # unseparated YYYYMMDDHHMMSS string; we splice in the separators
        # so `dockerTools`'s post-build `date -d` validation accepts the
        # value (the basic form without separators is rejected by GNU
        # `date`). Avoids the "epoch image" warnings Scout / Docker Hub
        # emit when `created` is left at 1970-01-01. Still deterministic
        # for a given flake.lock.
        imageCreated = let d = self.lastModifiedDate or "19700101000000"; in
          builtins.substring  0 4 d + "-"
          + builtins.substring 4 2 d + "-"
          + builtins.substring 6 2 d
          + "T"
          + builtins.substring  8 2 d + ":"
          + builtins.substring 10 2 d + ":"
          + builtins.substring 12 2 d
          + "Z";

        # Common image config shared by buildLayeredImage + streamLayeredImage.
        # Runtime user is intentionally root — see the comment near
        # `Cmd` in the config block below.
        mkDockerImage = builder: builder {
          name = "daschswiss/sipi";
          tag = imageTag;
          created = imageCreated;
          maxLayers = 125;
          contents = with pkgs; [
            sipiForImage
            cacert
            dockerTools.fakeNss
            bashInteractive
            coreutils
            # ffmpeg-headless is the same upstream ffmpeg compiled with
            # --disable-{ffplay,sdl2,xlib,libxcb,…}. The `ffmpeg` and
            # `ffprobe` binaries are byte-identical between the two
            # variants — same codec set, same demuxer set, same upstream
            # version. Only `ffplay` (a desktop GUI player nobody in the
            # DSP stack invokes) is dropped, along with its X11/SDL/font/
            # audio-output runtime closure (~800 MiB). dsp-ingest's only
            # use is `docker run --entrypoint ffprobe …` for video
            # metadata, which is unchanged.
            ffmpeg-headless
            curl
            tini
            tzdata
          ];
          config = {
            # config.User is intentionally unset — sipi runs as root.
            # Sipi reads artefacts under the SIPI "Image root" (see
            # UBIQUITOUS_LANGUAGE.md — Image and Bitstream files resolved
            # from `{prefix}/{identifier}` requests) from an NFS mount whose
            # ownership is controlled by another service. Switching to a
            # non-root uid requires uid/gid coordination with NFS exports
            # (or `no_root_squash` / `nfs4 idmap` reconfiguration on the
            # exporter side). Tracked in DEV-5920, not done here.
            # Match the historical Dockerfile's Entrypoint/Cmd split:
            # sipi binary lives in Entrypoint (after tini), only the
            # config flag is in Cmd. This is what makes `docker run
            # daschswiss/sipi --help` (or any other CLI invocation) work
            # — Docker overrides Cmd when extra args are given, and we
            # want sipi to receive those args, not tini.
            Entrypoint = [ "${pkgs.tini}/bin/tini" "--" "${sipiForImage}/bin/sipi" ];
            Cmd = [ "--config=/sipi/config/sipi.config.lua" ];
            ExposedPorts = { "1024/tcp" = { }; };
            WorkingDir = "/sipi";
            Env = [
              "SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
              "TZ=Europe/Zurich"
              # nixpkgs' tzdata installs zoneinfo under `/share/zoneinfo`,
              # but glibc's default search path is `/usr/share/zoneinfo`.
              # Set TZDIR explicitly so `TZ=Europe/Zurich` resolves
              # correctly without depending on `/etc/localtime` being
              # bind-mounted from the host. ops-deploy does mount
              # `/etc/localtime:/etc/localtime:ro` from the host, which
              # takes precedence — but for stand-alone runs (smoke
              # tests, dev) this keeps the timezone correct.
              "TZDIR=${pkgs.tzdata}/share/zoneinfo"
              # `C.UTF-8` is built into glibc itself — no `glibcLocales`
              # derivation needed. It covers the only locale category
              # sipi depends on (LC_CTYPE for UTF-8 byte classification,
              # used by exiv2 metadata handling, Lua string functions on
              # UTF-8 input, and std::locale() in CLI11/shttps).
              "LC_ALL=C.UTF-8"
              "LANG=C.UTF-8"
            ];
            Labels = {
              "org.opencontainers.image.source"      = "https://github.com/dasch-swiss/sipi";
              "org.opencontainers.image.revision"    = self.rev or "dirty";
              "org.opencontainers.image.version"     = imageTag;
              "org.opencontainers.image.licenses"    = "AGPL-3.0-only";
              "org.opencontainers.image.title"       = "Sipi";
              "org.opencontainers.image.description" = "IIIF-compatible media server.";
            };
            Healthcheck = {
              Test = [ "CMD" "curl" "-sf" "http://localhost:1024/health" ];
              # All durations in nanoseconds. /health is served by
              # shttps (see sipi/CONTEXT-MAP.md — one-way SIPI → shttps
              # dependency).
              Interval    = 30 * 1000 * 1000 * 1000;
              Timeout     =  5 * 1000 * 1000 * 1000;
              StartPeriod = 10 * 1000 * 1000 * 1000;
              Retries     = 3;
            };
          };
          fakeRootCommands = ''
            mkdir -p ./sipi/images/knora
            mkdir -p ./sipi/cache
            mkdir -p ./sipi/config
            mkdir -p ./sipi/scripts
            mkdir -p ./sipi/server
            cp ${sipiForImage}/share/sipi/config/sipi.config.lua  ./sipi/config/
            cp ${sipiForImage}/share/sipi/config/sipi.init.lua    ./sipi/config/
            cp ${sipiForImage}/share/sipi/server/test.html         ./sipi/server/
            cp ${sipiForImage}/share/sipi/scripts/test_functions.lua ./sipi/scripts/
            cp ${sipiForImage}/share/sipi/scripts/send_response.lua  ./sipi/scripts/

            # Default /etc/localtime → Europe/Zurich, matching the
            # historical Ubuntu image (where dpkg-reconfigure tzdata
            # set the symlink). ops-deploy bind-mounts the host's
            # /etc/localtime over this in production; for stand-alone
            # runs (dev, smoke tests) this keeps tz correct.
            mkdir -p ./etc
            ln -sf ${pkgs.tzdata}/share/zoneinfo/Europe/Zurich ./etc/localtime
          '';
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

          # Rust e2e + Docker smoke test binaries (see nix/rust-tests.nix).
          # Built with vendored crate sources via crane; consumed by
          # `just nix-test-e2e` and `just nix-test-smoke`.
          inherit (rustTests) e2e-tests smoke-test;

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
          # Production Docker image via Nix dockerTools.
          # Runtime: root user (NFS uid/gid coordination deferred to
          # DEV-5920); tini as PID 1; HEALTHCHECK against /health on
          # port 1024; en_US.UTF-8 + sr_RS.UTF-8 locales; tzdata with
          # TZ=Europe/Zurich; OCI labels; non-epoch `created` derived
          # from flake.lock's lastModifiedDate.
          docker        = mkDockerImage pkgs.dockerTools.buildLayeredImage;
          docker-stream = mkDockerImage pkgs.dockerTools.streamLayeredImage;

          # Standalone passthrough for the sipi `debug` output. Used by
          # publish.yml to extract the `.debug` file under the GNU
          # build-id layout (lib/debug/.build-id/<xx>/<yy>.debug) and
          # upload to Sentry. `pkgs.sipi.debug` exists because
          # `package.nix` sets `separateDebugInfo = true` (which adds
          # "debug" to outputs automatically on Linux). The justfile's
          # `nix-docker-build-<arch>` recipe builds this in the same
          # `nix build` invocation as `docker-stream`, so the symlink
          # is realized as a side-effect of the image build.
          sipi-debug = pkgs.sipi.debug;
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
