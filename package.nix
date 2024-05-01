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
stdenv.mkDerivation rec {
  pname = "sipi";
  version = "3.8.12-dev";

  src = ./.;

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE:STRING=Release"
    "-DEXT_PROVIDED_VERSION:STRING=${version}"
    "-DWITH_CODE_COVERAGE:BOOL=FALSE"
    "-DCMAKE_CXX_STANDARD=${cxxStandard}"
  ];

  outputs = [ "out" ];


  nativeBuildInputs = [
    cmake
    git
    cacert
    openssl
    pkg-config
  ] ++ lib.optionals (stdenv.isDarwin) [
    cctools
    libtool
  ];

  buildInputs = [
    xxd
    unzip

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

  ] ++ lib.optionals (stdenv.isDarwin) [
    libiconv
    SystemConfiguration
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
