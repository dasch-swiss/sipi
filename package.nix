{
  lib,
  clang16Stdenv,
  cmake,
  ffmpeg,
  file,
  gettext,
  glibcLocales,
  gperf,
  libidn,
  libuuid,
  openssl,
  readline70,
}:
clang16Stdenv.mkDerivation {
  pname = "sipi";
  version = "3.8.12";

  src = ./.;

  nativeBuildInputs = [cmake];
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
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
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
