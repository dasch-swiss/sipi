{ lib
, stdenv
, cmake
, curl
, ffmpeg
, file
, gcc13
, gettext
, glibcLocales
, gperf
, iconv
, libidn
, libuuid
, openssl
, perl
, readline70
, unzip
}:
stdenv.mkDerivation {
  pname = "sipi";
  version = "3.8.12";

  src = ./.;

  nativeBuildInputs = [
    cmake
  ];
  
  buildInputs = [
    curl
    ffmpeg
    file
    gcc13
    gettext
    glibcLocales
    gperf
    iconv
    libidn
    libuuid # uuid und uuid-dev
    # numactl not available for mac
    perl
    openssl
    readline70
    unzip
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
  ];

  makeFlags = [ "-j 1" ];

  meta = with lib; {
    homepage = "https://github.com/dasch-swiss/sipi";
    description = ''
      SIPI - Simple Image Presentation Interface.";
    '';
    licencse = licenses.agpl3Only;
    platforms = with platforms; linux ++ darwin;
  };
}
