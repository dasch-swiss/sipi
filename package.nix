{ lib
, stdenv
, cmake
, git
, unzip
, xxd

, abseil-cpp
, bzip2
, cacert
, curl
, exiv2
, expat
, ffmpeg
, file
, gettext
, glibcLocales
, gperf
, iconv
, inih
, libidn
, libtiff-patched
, libuuid
, libwebp
, nlohmann_json
, openssl
, opentelemetry-cpp
, perl
, protobuf
, python311Full
, readline70
, sqlite

, cxxStandard
}:
stdenv.mkDerivation {
  pname = "sipi";
  version = "3.8.12";

  src = ./.;

  nativeBuildInputs = [
    cmake git openssl cacert
  ];

  buildInputs = [
    xxd
    unzip

    abseil-cpp
    bzip2
    curl
    exiv2
    expat
    ffmpeg
    file
    gettext
    git
    glibcLocales
    gperf
    iconv
    inih
    libidn
    libtiff-patched
    libuuid # uuid und uuid-dev
    libwebp
    nlohmann_json
    perl
    openssl
    opentelemetry-cpp
    protobuf
    python311Full
    readline70
    sqlite
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
