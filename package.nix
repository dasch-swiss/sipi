{
  lib,
  clang14Stdenv,
  cmake,
  ffmpeg,
  file,
  gettext,
  glibcLocales,
  gperf,
  iconv,
  libGLU,
  libidn,
  libuuid,
  openssl,
  readline70,
}:
clang14Stdenv.mkDerivation {
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
    iconv
    # libacl1-dev
    libGLU
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
