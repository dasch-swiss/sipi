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
, python311Full
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

, cxxStandard
}:
stdenv.mkDerivation {
  pname = "sipi";
  version = "3.8.12-dev";

  src = ./.;

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

    python311Full
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

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_CXX_STANDARD=${cxxStandard}"
  ];

  makeFlags = [ "-j" "VERBOSE=1" ];

  meta = with lib; {
    homepage = "https://github.com/dasch-swiss/sipi";
    description = ''
      SIPI - Simple Image Presentation Interface.";
    '';
    licencse = licenses.agpl3Only;
    platforms = with platforms; linux ++ darwin;
  };
}
