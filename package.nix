{ lib
, stdenv
, cmake
, pkg-config
, autoconf
, automake
, libtool
, gettext
, m4
, unzip
, xxd
, file
, readline
, iconv
, perl
, fetchurl
, gcovr
, llvmPackages_19
, removeReferencesTo  # provides `remove-references-to` for the postFixup
                      # toolchain-leak scrub (see comment near `postFixup`).
, kakaduArchive  # Store path to the Kakadu zip. Provided by flake.nix via
                 # a fixed-output derivation that calls `gh release download`
                 # against dsp-ci-assets. Requires GH_TOKEN (or a Cachix hit)
                 # at build time.
, cmakeBuildType ? "RelWithDebInfo"
, enableCoverage ? false
, enableTests ? true
, providedVersion ? null
}:

let
  version = if providedVersion != null
    then providedVersion
    else lib.strings.trim (builtins.readFile ./version.txt);

  # Pre-fetched test dependencies (Nix sandbox blocks network during build)
  gtestArchive = fetchurl {
    url = "https://github.com/google/googletest/archive/refs/tags/v1.16.0.zip";
    sha256 = "sha256-qWB8khWGa9QlpyVhDF4Pc57rUIh6V5A99IiRRGzm+zw=";
  };
  approvalTestsHeader = fetchurl {
    url = "https://github.com/approvals/ApprovalTests.cpp/releases/download/v.10.13.0/ApprovalTests.v.10.13.0.hpp";
    sha256 = "c00f6390b81d9924dc646e9d32b61e1e09abda106c13704f714ac349241bb9ff";
  };
in
stdenv.mkDerivation {
  pname = "sipi";
  inherit version;

  src = lib.fileset.toSource {
    root = ./.;
    fileset = lib.fileset.unions [
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

  # `coverage` is an extra output populated by the postCheck hook when
  # both `enableCoverage` and `enableTests` are true. The `debug` output
  # is added automatically by nixpkgs when `separateDebugInfo = true`
  # (below) on Linux — including it explicitly here would cause a
  # `duplicate derivation output 'debug'` error.
  outputs = [ "out" ] ++ lib.optional enableCoverage "coverage";

  nativeBuildInputs = [
    cmake
    pkg-config
    autoconf
    automake
    libtool
    gettext
    m4
    unzip
    xxd
    file  # autoreconf deps for libmagic build
    removeReferencesTo  # postFixup uses `remove-references-to`
  ] ++ lib.optionals enableCoverage [
    gcovr                  # gcovr binary used by postCheck to produce coverage.xml
    llvmPackages_19.llvm   # provides `llvm-cov` on PATH for gcovr's --gcov-executable
  ];

  buildInputs = [
    readline  # Lua's `make linux` / `make macosx` links -lreadline
    iconv     # exiv2 character set conversion (on glibc, provided by libc)
    perl      # OpenSSL's Configure script is Perl
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=${cmakeBuildType}"
  ] ++ lib.optionals enableCoverage [
    "-DCODE_COVERAGE=ON"
  ] ++ lib.optionals enableTests [
    "-DBUILD_TESTING=ON"
    "-DGTEST_LOCAL_ARCHIVE=${gtestArchive}"
    "-DAPPROVAL_TESTS_LOCAL_HEADER=${approvalTestsHeader}"
  ] ++ [
    "-DEXT_PROVIDED_VERSION=${version}"
  ];

  # Kakadu JPEG2000 SDK (proprietary). The flake passes the archive as a
  # sha256-pinned fixed-output derivation that fetches the asset from the
  # dsp-ci-assets GitHub release. ext/kakadu expects the archive at this
  # exact path and filename.
  #
  # `patchShebangs` rewrites `generate_icc_header.sh`'s `#!/bin/bash`
  # shebang to a nix-store path; the script is invoked from CMakeLists.txt
  # at build time, so nixpkgs' automatic shebang patching doesn't cover it.
  preConfigure = ''
    cp ${kakaduArchive} vendor/v8_5-01382N.zip
    patchShebangs generate_icc_header.sh
  '';

  env = {
    CXXFLAGS = "-stdlib=libc++";
    LDFLAGS = "-stdlib=libc++ -Wno-unused-command-line-argument";
  };

  # Match the dev-shell toolchain (`flake.nix` clang devShell sets
  # `hardeningDisable = [ "all" ]`). Older autotools ext/* deps like xz
  # fail to compile under nixpkgs' default hardening set on Linux
  # (format, stackprotector, fortify, pic, bindnow, …). Disabling all
  # hardening mirrors the pre-migration imperative-cmake build that ran
  # inside the dev shell and passed CI.
  hardeningDisable = [ "all" ];

  # `doCheck = enableTests` keeps the test invariant honest: any variant
  # that builds tests also runs them inside the sandbox (`.#default`,
  # `.#dev`). `.#release` sets `enableTests = false` and skips the
  # check phase.
  doCheck = enableTests;

  # cmake setup-hook cd's into `build/` in configurePhase; checkPhase and
  # postCheck run from there.
  checkPhase = ''
    runHook preCheck
    ctest --output-on-failure
    runHook postCheck
  '';

  # Coverage report (multi-output). When `enableCoverage` is true the
  # `$coverage` output is declared (see `outputs` above) and gcovr
  # aggregates `.gcda` counters emitted by the test run into
  # `$coverage/coverage.xml`. Codecov reads this path from
  # `result-coverage/coverage.xml`.
  #
  # ApprovalTests note: if the sandbox rejects writes outside `build/`,
  # `checkPhase` can be scoped via `ctest --output-on-failure -LE approval`
  # and the approval layer kept as a dev-shell invocation.
  postCheck = lib.optionalString enableCoverage ''
    mkdir -p "$coverage"
    gcovr -j $NIX_BUILD_CORES \
      --xml "$coverage/coverage.xml" \
      --root .. \
      --gcov-executable "llvm-cov gcov" \
      --exclude '../test/' \
      --exclude '../fuzz/' \
      --exclude '../ext/' \
      --exclude '../include/'
  '';

  separateDebugInfo = true;

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp sipi $out/bin/

    # Runtime files matching Dockerfile final stage (lines 122-127)
    mkdir -p $out/share/sipi/config $out/share/sipi/scripts $out/share/sipi/server
    cp $src/config/sipi.config.lua      $out/share/sipi/config/
    cp $src/config/sipi.init.lua        $out/share/sipi/config/
    cp $src/server/test.html            $out/share/sipi/server/
    cp $src/scripts/test_functions.lua  $out/share/sipi/scripts/
    cp $src/scripts/send_response.lua   $out/share/sipi/scripts/

    runHook postInstall
  '';

  # Strip build-toolchain references from the final binary.
  #
  # Sipi statically embeds OpenSSL (built via cmake's ExternalProject in
  # `ext/openssl`). OpenSSL bakes its build-time compiler invocation into
  # the binary as a diagnostic string, surfaced by `openssl version -a`:
  #
  #   compiler: /nix/store/<hash>-clang-wrapper-19.1.7/bin/clang -fPIC ...
  #
  # That literal `/nix/store/...-clang-wrapper-19.1.7` substring trips
  # Nix's runtime-dependency scanner, which then pulls clang-wrapper
  # into the runtime closure. Transitively that drags in clang itself,
  # llvm-19.1.7-lib, gcc-15.2.0, binutils, and every `-dev` output of
  # the libc++ stdenv — ~3 GiB of build-time toolchain that the
  # stripped binary doesn't actually load at runtime.
  #
  # On the historical Ubuntu/apt build the same OpenSSL string read
  # `/usr/bin/clang` — short path, no closure bloat. Under Nix the
  # absolute store path is the only realistic CC value, so we scrub
  # the toolchain references post-fixup. Behaviour change is limited
  # to `openssl version -a` no longer reporting the build compiler
  # path; nothing in the runtime path or the `sipi` CLI consumes that
  # field.
  #
  # The closure regression is observable via `nix path-info -Sh`; if the
  # diagnostic string format ever shifts (e.g. a future `ext/*` dep
  # embeds a similar absolute toolchain path) we'd see image growth in
  # CI before it ships. A `disallowedReferences` assertion would be a
  # nice belt-and-braces here, but nixpkgs requires `__structuredAttrs`
  # when combining it with `separateDebugInfo` — out of scope for this
  # change.
  postFixup = ''
    remove-references-to \
      -t ${stdenv.cc} \
      -t ${stdenv.cc.cc} \
      -t ${stdenv.cc.bintools} \
      -t ${stdenv.cc.bintools.bintools} \
      $out/bin/sipi
  '';

  meta = with lib; {
    homepage = "https://github.com/dasch-swiss/sipi";
    description = "SIPI - Simple Image Presentation Interface";
    license = licenses.agpl3Only;
    platforms = with platforms; linux ++ darwin;
  };
}
