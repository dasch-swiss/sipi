# Rust e2e + Docker smoke test binaries.
#
# Inputs:
#   pkgs     — nixpkgs (with sipi overlay applied in flake.nix)
#   craneLib — `crane.mkLib pkgs`
#
# Outputs:
#   e2e-tests   — every tests/<name>.rs binary except docker_smoke
#                 (no features); ships a run-e2e.sh driver alongside
#   smoke-test  — the docker_smoke binary, --features docker
#
# Each derivation produces test binaries under $out/bin/<test-target>,
# named after the cargo test target (one per tests/<name>.rs). The
# binaries discover the sipi repo root at runtime via $SIPI_REPO_ROOT
# (see sipi_e2e::repo_root in test/e2e-rust/src/lib.rs) — they cannot
# rely on env!("CARGO_MANIFEST_DIR") because that resolves to the
# Nix sandbox source path.
{ pkgs, craneLib }:

let
  src = craneLib.cleanCargoSource ../test/e2e-rust;

  commonArgs = {
    inherit src;
    strictDeps = true;
    pname = "sipi-e2e";
    version = "0.1.0";

    nativeBuildInputs = with pkgs; [
      cmake          # aws-lc-sys (jsonwebtoken[aws_lc_rs])
      perl           # aws-lc-sys, openssl-sys
      pkg-config     # openssl-sys
    ];
    buildInputs = with pkgs; [ openssl ];
    env = {
      # Pin libclang to the same LLVM major as the C++ stdenv override
      # (`flake.nix` picks `llvmPackages_19.libcxxStdenv`). The
      # unversioned `pkgs.llvmPackages` alias drifts across nixpkgs
      # channels and can land bindgen on a different LLVM ABI than the
      # surrounding C++ build.
      LIBCLANG_PATH = "${pkgs.llvmPackages_19.libclang.lib}/lib";  # bindgen
      # Crane defaults to CARGO_PROFILE=release, which makes
      # `cargoWithProfile test --no-run` emit `cargo test --release`.
      # Release-optimized test binaries hammer the sipi server ~5x
      # faster than debug-profile binaries, exceeding sipi's tolerated
      # connection-drop rate on the JP2 → JPEG decode path and turning
      # several iiif_compliance / resource_limits tests flaky. Pin the
      # cargo profile to `test` to match the default `cargo test`
      # behavior used by the inner-loop dev shell.
      CARGO_PROFILE = "test";
    };
  };

  # Vendored deps build, shared between e2e-tests and smoke-test.
  cargoArtifacts = craneLib.buildDepsOnly commonArgs;

  # Build "cargo test --no-run" and install the resulting test
  # binaries. Crane's `installFromCargoBuildLogHook` filters its
  # extraction with `.profile.test == false`, so it is unsuitable
  # for installing test binaries — it would silently install zero
  # files. Instead we parse cargo's JSON build log directly with
  # `jq`, picking compiler-artifact records where `.profile.test`
  # is true, and copying each `.executable` to `$out/bin/<target>`
  # under its cargo test target name (one binary per `tests/<name>.rs`).
  mkTestBinaries = extraArgs: craneLib.mkCargoDerivation (commonArgs // {
    inherit cargoArtifacts;
    pnameSuffix = "-tests";

    # Disable crane's default post-build install — its filter is
    # the inverse of what we need (it wants non-test binaries).
    doNotPostBuildInstallCargoBinaries = true;

    nativeBuildInputs = commonArgs.nativeBuildInputs ++ [ pkgs.jq ];

    buildPhaseCargoCommand = ''
      cargoBuildLog=$(mktemp cargoBuildLog-XXXXXX.json)
      cargoWithProfile test --no-run ${extraArgs} \
        --message-format json-render-diagnostics > "$cargoBuildLog"
    '';

    installPhaseCommand = ''
      mkdir -p $out/bin
      jq -r '
        select(.reason == "compiler-artifact" and .profile.test == true)
        | select(.executable != null)
        | "\(.target.name)\t\(.executable)"
      ' < "$cargoBuildLog" | while IFS=$'\t' read -r name exe; do
        echo "installing test binary: $name"
        cp "$exe" "$out/bin/$name"
      done

      # Fail loudly if the jq filter extracted zero binaries (e.g. a
      # future cargo / crane / message-format change makes the
      # `.profile.test == true` selector miss). Without this guard the
      # derivation builds successfully with an empty $out/bin and the
      # confusion surfaces only at run-e2e.sh time.
      if [ -z "$(ls -A "$out/bin" 2>/dev/null)" ]; then
        echo "ERROR: no test binaries extracted from cargo build log" >&2
        exit 1
      fi
    '';
  });

  # Bash driver script: iterates every test binary in $out/bin/ except
  # itself and runs each with --test-threads=1. Exits non-zero if any
  # binary failed. The justfile's nix-test-e2e recipe shells out to this.
  runE2eDriver = pkgs.writeShellScript "run-e2e.sh" ''
    # `-e` is intentionally omitted: the loop must continue past
    # per-binary failures to count them all and exit with the
    # aggregate. Per-binary success/failure is captured explicitly
    # via `if ! "$bin" ...`.
    set -uo pipefail
    : "''${SIPI_BIN:?SIPI_BIN must point at the sipi binary under test}"
    : "''${SIPI_REPO_ROOT:?SIPI_REPO_ROOT must point at the sipi repo root}"

    bin_dir=$(dirname "$0")
    failed=0
    total=0

    for bin in "$bin_dir"/*; do
      [ -x "$bin" ] || continue
      name=$(basename "$bin")
      [ "$name" = "run-e2e.sh" ] && continue
      total=$((total + 1))
      echo "==> $name"
      if ! "$bin" --test-threads=1; then
        failed=$((failed + 1))
      fi
    done

    if [ "$failed" -gt 0 ]; then
      echo "FAIL: $failed/$total e2e test binaries failed" >&2
      exit 1
    fi
    echo "OK: $total/$total e2e test binaries passed"
  '';

  e2eTests = (mkTestBinaries "").overrideAttrs (old: {
    postInstall = (old.postInstall or "") + ''
      cp ${runE2eDriver} $out/bin/run-e2e.sh
      chmod +x $out/bin/run-e2e.sh
    '';
  });

  smokeTest = mkTestBinaries "--features docker --test docker_smoke";
in
{
  e2e-tests = e2eTests;
  smoke-test = smokeTest;
}
