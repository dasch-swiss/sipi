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
, kakaduArchive  # Store path to the Kakadu zip. Provided by flake.nix via
                 # a fixed-output derivation that calls `gh release download`
                 # against dsp-ci-assets. Requires GH_TOKEN (or a Cachix hit)
                 # at build time.
, cmakeBuildType ? "RelWithDebInfo"
, enableCoverage ? false
, enableSanitizers ? false
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
  ] ++ lib.optionals enableSanitizers [
    "-DENABLE_SANITIZERS=ON"
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
  preConfigure = ''
    cp ${kakaduArchive} vendor/v8_5-01382N.zip
  '';

  env = {
    CXXFLAGS = "-stdlib=libc++";
    LDFLAGS = "-stdlib=libc++ -Wno-unused-command-line-argument";
  };

  # Tests are built when enableTests=true but not run during `nix build`.
  # Run tests in the dev shell: `just nix-build && just nix-test`
  doCheck = false;

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

  meta = with lib; {
    homepage = "https://github.com/dasch-swiss/sipi";
    description = "SIPI - Simple Image Presentation Interface";
    license = licenses.agpl3Only;
    platforms = with platforms; linux ++ darwin;
  };
}
