{ lib
, stdenv
, cmake
, abseil-cpp
, cacert
, curl
, ffmpeg
, file
, gettext
, git
, glibcLocales
, gperf
, iconv
, inih
, libidn
, libuuid
, nlohmann_json
, openssl
, opentelemetry-cpp
, perl
, protobuf
, python311Full
, readline70
, unzip
, cxxStandard
}:
stdenv.mkDerivation {
  pname = "sipi";
  version = "3.8.12";

  src = ./.;

  nativeBuildInputs = [
    cmake git openssl cacert
  ];

  shellHook = ''
    export SSL_CERT_FILE=${cacert}/etc/ssl/certs/ca-bundle.crt
    export GIT_SSL_CAINFO="${cacert}/etc/ssl/certs/ca-bundle.crt"
    export PS1="\\u@\\h | nix-develop> "
  '';
  
  buildInputs = [
    abseil-cpp
    curl
    ffmpeg
    file
    gettext
    git
    glibcLocales
    gperf
    iconv
    inih
    libidn
    libuuid # uuid und uuid-dev
    nlohmann_json
    perl
    openssl
    opentelemetry-cpp
    protobuf
    python311Full
    readline70
    unzip
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
