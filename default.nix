{ lib
, llvmPackages_12
, cmake
, ffmpeg
, file
, gettext
, glibcLocales
, gperf
, libidn
, libuuid
, openssl
, readline70 }:

llvmPackages_12.stdenv.mkDerivation rec {
  pname = "sipi";
  version = "3.8.12";
  
  src = ./.;

  nativeBuildInputs = [ cmake ];
  buildInputs = [
    ffmpeg
    file
    gettext
    glibcLocales
    gperf
    # libacl1-dev
    libidn
    libuuid # uuid und uuid-dev
    # numactl not available for mac
    openssl
    readline70
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
