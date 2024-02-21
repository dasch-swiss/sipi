{ lib
, cmake
, ffmpeg
, file
, gettext
, gperf
, libacl1-dev
, libidn11-dev
, libnuma-dev
, libreadline-dev
, libmagic-dev
, libssl-dev
, llvmPackages_17
, locales
, openssl
, uuid
, uuid-dev }:

llvmPackages_17.stdenv.mkDerivation rec {
  pname = "sipi";
  version = "3.8.12";
  
  src = ./.;

  nativeBuildInputs = [ cmake ];
  buildInputs = [
    ffmpeg
    file
    gettext
    gperf
    libacl1-dev
    libidn11-dev
    libnuma-dev
    libreadline-dev
    libmagic-dev
    libssl-dev
    locales
    openssl
    uuid
    uuid-dev
  ];

  cmakeFlags = [
    "-DENABLE_TESTING=OFF"
    "-DENABLE_INSTALL=ON"
  ];

  meta = with lib; {
    homepage = "https://github.com/dasch-swiss/sipi";
    description = ''
      SIPI - Simple Image Presentation Interface.";
    '';
    licencse = licenses.agpl3Only;
    platforms = with platforms; linux ++ darwin;
  };
}
