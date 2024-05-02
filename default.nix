{ lib
, stdenv

, cmake
, git
, cacert
, libtool
, pkg-config
, unzip
, xxd

  # darwin
, cctools ? null
, libiconv
, SystemConfiguration ? null

, exiv2
, ffmpeg
, file
, gettext
, glibcLocales
, gperf
, iconv
, inih
, libidn2
, libunistring
, libuuid
, nlohmann_json
, perl
, readline70

  # custom overlays
, abseil-cpp
, libtiff-patched
, protobuf
, opentelemetry-cpp

  # static libraries
, bzip2
, curl
, expat
, libpsl
, libssh2
, libwebp
, lua5_4
, nghttp2
, openssl
, sqlite

  # testing
, gtest
, nginx
, graphicsmagick
, apacheHttpd
, imagemagick
, libxml2
, libxslt
, python311Full
, python311Packages
, iiif-validator

, cxxStandard
}:

let
  src = ./.;
  version = lib.readFile "${src}/version.txt";
in
stdenv.mkDerivation {
  inherit src version;
  pname = "sipi";

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE:STRING=Release"
    "-DEXT_PROVIDED_VERSION:STRING=${version}"
    "-DWITH_CODE_COVERAGE:BOOL=FALSE"
    "-DCMAKE_CXX_STANDARD=${cxxStandard}"
  ];

  outputs = [ "out" ];

  strictDeps = true;
  nativeBuildInputs = [
    cmake
    git
    cacert
    openssl
    pkg-config
    unzip
    xxd
  ] ++ lib.optionals (stdenv.isDarwin) [
    cctools
    libtool
  ];

  buildInputs = [
    curl
    exiv2
    ffmpeg
    file
    gettext
    git
    glibcLocales
    gperf
    inih
    libidn2
    libunistring
    libuuid # uuid und uuid-dev
    lua5_4
    nlohmann_json
    perl
    readline70

    # custom overlays
    abseil-cpp
    libtiff-patched
    protobuf
    opentelemetry-cpp

    # static libraries
    bzip2
    curl
    expat
    libpsl
    libssh2
    libwebp
    nghttp2
    openssl
    sqlite
  ] ++ lib.optionals (stdenv.isDarwin) [
    libiconv
    SystemConfiguration
  ];

  nativeCheckInputs = [
    # testing
    nginx
    graphicsmagick
    apacheHttpd
    imagemagick
    libxml2
    libxslt
    python311Full
    python311Packages.deprecation
    python311Packages.docker
    python311Packages.pip
    python311Packages.psutil
    python311Packages.pytest
    python311Packages.requests
    python311Packages.sphinx
    python311Packages.testcontainers
    python311Packages.wrapt
    iiif-validator
  ];

  checkInputs = [
    gtest
  ];

  enableParallelBuilding = true;

  doCheck = true;

  meta = with lib; {
    homepage = "https://github.com/dasch-swiss/sipi";
    description = ''
      SIPI - Simple Image Presentation Interface.";
    '';
    licencse = licenses.agpl3Only;
    platforms = with platforms; linux ++ darwin;
  };
}
